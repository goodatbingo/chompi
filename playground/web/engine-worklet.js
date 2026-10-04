// Hosts one firmware engine (tape.wasm, wave.wasm or tempo.wasm) on the
// audio thread. The main thread sends:
//   { type: 'card', files: [{ path, data }] }   copy files onto the SD card
//   { type: 'call', fn, args, id? }             call an exported function;
//                                               with an id, the result is posted back
//   { type: 'dispose' }                         stop, so the engine's memory can be freed
// Audio input feeds the codec's line-in channels; the main outputs come out.

// AudioWorkletGlobalScope has no TextEncoder; card paths are plain ASCII.
const ascii = (str) => Uint8Array.from(str, (c) => c.charCodeAt(0) & 0x7f);

class EngineProcessor extends AudioWorkletProcessor {
  constructor({ processorOptions }) {
    super();
    this.prefix = processorOptions.prefix;
    this.x = null;
    this.queue = [];
    this.port.onmessage = (e) => (this.x ? this.handle(e.data) : this.queue.push(e.data));
    const imports = {
      env: { emscripten_notify_memory_growth: () => { this.views = null; } },
      // Only reached by libc's stdio teardown; nothing prints.
      wasi_snapshot_preview1: { fd_close: () => 0, fd_seek: () => 0, fd_write: () => 0 },
    };
    WebAssembly.instantiate(processorOptions.wasm, imports).then(({ instance }) => {
      this.x = instance.exports;
      this.x._initialize();
      const status = this.x[`${this.prefix}_init`]();
      this.port.postMessage({ type: 'ready', status });
      this.queue.forEach((m) => this.handle(m));
      this.queue = [];
    }, (err) => this.port.postMessage({ type: 'error', message: String(err) }));
  }

  // Float32 views onto the engine's host buffers. Rebuilt after memory grows.
  io() {
    if (!this.views) {
      const x = this.x, p = this.prefix, buf = x.memory.buffer;
      const view = (ptr) => new Float32Array(buf, ptr, 128);
      const out = x[`${p}_out`], inp = x[`${p}_in`];
      this.views = {
        inL: inp ? view(inp(2)) : null, inR: inp ? view(inp(3)) : null, mic: inp ? view(inp(0)) : null,
        outL: view(out(2)), outR: view(out(3)),
      };
    }
    return this.views;
  }

  writeFile(path, data) {
    const x = this.x;
    const bytes = new Uint8Array(data);
    const setPath = (p) => new Uint8Array(x.memory.buffer).set(ascii(p + '\0'), x.sd_path());
    const parts = path.split('/');
    for (let i = 1; i < parts.length; i++) { setPath(parts.slice(0, i).join('/')); x.sd_mkdir(); }
    setPath(path);
    if (x.sd_open_write() !== 0) throw new Error(`Could not create ${path} on the card`);
    const n = x.sd_chunk_size();
    for (let o = 0; o < bytes.length; o += n) {
      const chunk = bytes.subarray(o, o + n);
      new Uint8Array(x.memory.buffer, x.sd_chunk(), chunk.length).set(chunk);
      if (x.sd_write(chunk.length) !== 0) throw new Error(`The card is full while writing ${path}`);
    }
    x.sd_close();
    this.views = null;
  }

  handle(msg) {
    try {
      if (msg.type === 'dispose') {
        this.dead = true;
        this.x = null;
      } else if (msg.type === 'card') {
        msg.files.forEach((f) => this.writeFile(f.path, f.data));
        if (msg.id !== undefined) this.port.postMessage({ type: 'result', id: msg.id, value: msg.files.length });
      } else if (msg.type === 'call') {
        const value = this.x[msg.fn](...(msg.args || []));
        this.views = null; // a call can grow memory (boot, sample loads)
        if (msg.id !== undefined) this.port.postMessage({ type: 'result', id: msg.id, value });
      }
    } catch (err) {
      this.port.postMessage({ type: 'result', id: msg.id, error: String(err.message || err) });
    }
  }

  process(inputs, outputs) {
    if (this.dead) return false;
    const out = outputs[0];
    if (!this.x || !out.length) return true;
    const v = this.io();
    const input = inputs[0];
    if (v.inL) {
      if (input && input[0]) {
        const l = input[0], r = input[1] || input[0];
        v.inL.set(l); v.inR.set(r);
        for (let i = 0; i < 128; i++) v.mic[i] = (l[i] + r[i]) * 0.5;
      } else { v.inL.fill(0); v.inR.fill(0); v.mic.fill(0); }
    }
    this.x[`${this.prefix}_process`](out[0].length);
    const w = this.io(); // processing can grow memory
    out[0].set(w.outL);
    if (out[1]) out[1].set(w.outR);
    return true;
  }
}

registerProcessor('firmware-engine', EngineProcessor);
