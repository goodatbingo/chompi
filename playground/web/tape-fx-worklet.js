// AudioWorklet that runs the TAPE FX stage (tape_fx.wasm) on the audio thread.
// The main thread sends the wasm bytes in processorOptions and knob values
// over the port.

class TapeFxProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.fx = null;
    this.pending = [];
    this.port.onmessage = (e) => this.handle(e.data);
    WebAssembly.instantiate(options.processorOptions.wasm, {}).then(({ instance }) => {
      const x = instance.exports;
      x._initialize();
      x.tape_fx_init();
      const n = x.tape_fx_max_block();
      const view = (fn) => new Float32Array(x.memory.buffer, fn(), n);
      this.fx = {
        x, n,
        inL: view(x.tape_fx_in_l), inR: view(x.tape_fx_in_r),
        outL: view(x.tape_fx_out_l), outR: view(x.tape_fx_out_r),
      };
      this.pending.forEach((m) => this.handle(m));
      this.pending = [];
      this.port.postMessage({ type: 'ready', sampleRate: x.tape_fx_sample_rate() });
    }, (err) => this.port.postMessage({ type: 'error', message: String(err) }));
  }

  handle(msg) {
    if (!this.fx) { this.pending.push(msg); return; }
    const x = this.fx.x;
    switch (msg.type) {
      case 'filter': x.tape_fx_set_filter(msg.value); break;
      case 'resonance': x.tape_fx_set_resonance(msg.value); break;
      case 'saturate': x.tape_fx_set_saturate(msg.value); break;
      case 'warble': x.tape_fx_set_warble(msg.value); break;
      case 'bypass': x.tape_fx_set_bypass(msg.value ? 1 : 0); break;
    }
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const input = inputs[0];
    if (!this.fx || !out.length) return true;
    const { x, inL, inR, outL, outR } = this.fx;
    const frames = out[0].length;
    const srcL = input[0];
    const srcR = input[1] || input[0];
    if (srcL) { inL.set(srcL); inR.set(srcR); } else { inL.fill(0); inR.fill(0); }
    x.tape_fx_process(frames);
    out[0].set(outL.subarray(0, frames));
    if (out[1]) out[1].set(outR.subarray(0, frames));
    return true;
  }
}

registerProcessor('tape-fx', TapeFxProcessor);
