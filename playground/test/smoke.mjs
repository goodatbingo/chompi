// Smoke tests for Tape Bench's engines. Each test boots an engine the way the
// AudioWorklet does, copies factory card files onto its RAM-disk SD card from
// firmware/card-profiles, and checks the firmware audibly does its job.
// Run with `make test` (or `node test/smoke.mjs`).

import { readFile, readdir } from 'node:fs/promises';
import assert from 'node:assert/strict';

const web = new URL('../web/', import.meta.url);
const cards = new URL('../../firmware/card-profiles/', import.meta.url);
const encoder = new TextEncoder();

async function boot(name) {
  let views = null;
  const imports = {
    env: { emscripten_notify_memory_growth: () => { views = null; } },
    wasi_snapshot_preview1: { fd_close: () => 0, fd_seek: () => 0, fd_write: () => 0 },
  };
  const { instance } = await WebAssembly.instantiate(await readFile(new URL(`${name}.wasm`, web)), imports);
  const x = instance.exports;
  x._initialize();
  assert.equal(x[`${name}_init`](), 0, 'SD card format/mount failed');

  const bytes = () => new Uint8Array(x.memory.buffer);
  const setPath = (p) => bytes().set(encoder.encode(p + '\0'), x.sd_path());
  const e = {
    x,
    async card(profile, files) {
      for (const f of files) {
        const data = await readFile(new URL(`${profile}/${f}`, cards));
        const parts = f.split('/');
        for (let i = 1; i < parts.length; i++) { setPath(parts.slice(0, i).join('/')); x.sd_mkdir(); }
        setPath(f);
        assert.equal(x.sd_open_write(), 0, `open ${f}`);
        const n = x.sd_chunk_size();
        for (let o = 0; o < data.length; o += n) {
          const c = data.subarray(o, o + n);
          new Uint8Array(x.memory.buffer, x.sd_chunk(), c.length).set(c);
          assert.equal(x.sd_write(c.length), 0, `write ${f}`);
        }
        x.sd_close();
      }
      views = null;
    },
    // Render `seconds` of audio; `input(t)` feeds line in. Returns main-out L.
    render(seconds, input) {
      const blocks = Math.round((seconds * 48000) / 128);
      const out = new Float32Array(blocks * 128);
      const inFn = x[`${name}_in`];
      for (let b = 0; b < blocks; b++) {
        if (!views) {
          const buf = x.memory.buffer;
          views = {
            outL: new Float32Array(buf, x[`${name}_out`](2), 128),
            inL: inFn ? new Float32Array(buf, inFn(2), 128) : null,
            inR: inFn ? new Float32Array(buf, inFn(3), 128) : null,
          };
        }
        if (views.inL) for (let i = 0; i < 128; i++) {
          const v = input ? input((b * 128 + i) / 48000) : 0;
          views.inL[i] = v; views.inR[i] = v;
        }
        x[`${name}_process`](128);
        if (!views) continue; // memory grew mid-block; skip this read
        out.set(views.outL, b * 128);
      }
      for (const v of out) assert.ok(Number.isFinite(v), 'non-finite output');
      return out;
    },
  };
  return e;
}

const rms = (a) => Math.sqrt(a.reduce((s, v) => s + v * v, 0) / a.length);
const tail = (a, frac = 0.5) => a.subarray(Math.floor(a.length * (1 - frac)));
const sine = (hz, amp = 0.3) => (t) => amp * Math.sin(2 * Math.PI * hz * t);
// Energy above ~4 kHz relative to total, from first differences.
const brightness = (a) => {
  let d = 0;
  for (let i = 1; i < a.length; i++) d += (a[i] - a[i - 1]) ** 2;
  return Math.sqrt(d / a.length) / (rms(a) || 1);
};
// Count pitch steps: changes in zero-crossing rate between 50 ms windows.
const pitchSteps = (a) => {
  const win = 2400, rates = [];
  for (let w = 0; w + win <= a.length; w += win) {
    let z = 0;
    for (let i = w + 1; i < w + win; i++) if ((a[i] < 0) !== (a[i - 1] < 0)) z++;
    rates.push(z);
  }
  let steps = 0;
  for (let i = 1; i < rates.length; i++) if (Math.abs(rates[i] - rates[i - 1]) >= 4) steps++;
  return steps;
};

const tapeBankA = (await readdir(new URL('tape-2.0/', cards))).filter((f) => /^jammi_a1(_double)?\.wav$/.test(f));
const waveTables = (await readdir(new URL('wave-1.0/', cards))).filter((f) => f.endsWith('.wav'));
const tempoFiles = [];
for (const d of ['chromatic', 'slice', 'buffer']) for (const f of await readdir(new URL(`tempo-1.0/${d}/`, cards))) tempoFiles.push(`${d}/${f}`);

const tests = {
  // ------------------------------------------------------------------ TAPE
  async 'TAPE: silent at rest, boot tone on a key, quiet after release'() {
    const e = await boot('tape');
    assert.ok(rms(e.render(0.2)) < 1e-6);
    e.x.tape_slot(15);
    e.x.tape_note(60, 100, 1);
    const on = rms(e.render(0.5));
    e.x.tape_note(60, 0, 0);
    e.render(2);
    const off = rms(e.render(0.3));
    assert.ok(on > 0.005, `note rms ${on}`);
    assert.ok(off < on * 0.1, `release rms ${off} vs ${on}`);
  },

  async 'TAPE: streams a factory sample from the SD card'() {
    const e = await boot('tape');
    await e.card('tape-2.0', tapeBankA);
    e.x.tape_rescan();
    assert.equal(e.x.tape_file_exists(0), 1, 'jammi_a1.wav not found by the firmware');
    e.x.tape_slot(1);
    e.render(0.1);
    e.x.tape_note(60, 100, 1);
    assert.ok(rms(e.render(1)) > 0.005);
  },

  async 'TAPE: filter knob down darkens the output'() {
    const bright = async (filter) => {
      const e = await boot('tape');
      e.x.tape_set(5, filter);
      e.render(0.3);
      e.x.tape_slot(15);
      e.x.tape_note(72, 100, 1);
      return brightness(tail(e.render(0.6)));
    };
    const open = await bright(0.5), dark = await bright(0.08);
    assert.ok(dark < open * 0.5, `brightness open ${open} dark ${dark}`);
  },

  async 'TAPE: delay keeps ringing after the key is released'() {
    const ring = async (magic) => {
      const e = await boot('tape');
      e.x.tape_set(0, magic); e.x.tape_set(1, magic);
      e.render(0.3);
      e.x.tape_slot(15);
      e.x.tape_note(60, 100, 1); e.render(0.3); e.x.tape_note(60, 0, 0);
      e.render(0.4);
      return rms(e.render(0.6));
    };
    const dry = await ring(0), wet = await ring(0.8);
    assert.ok(wet > dry * 3, `tail dry ${dry} wet ${wet}`);
  },

  async 'TAPE: looper records line in and plays it back'() {
    const e = await boot('tape');
    e.x.tape_monitor_mode(1);
    e.x.tape_input_source(1);
    e.render(0.2, sine(440));
    // LooperEngine acts on a button once it has been held for kButtonTimeout.
    const press = (fn) => { e.x[fn](1); e.render(0.05, sine(440)); e.x[fn](0); };
    press('tape_looper_record'); // start the first recording
    e.render(1, sine(440));
    press('tape_looper_record'); // close the loop; it plays and overdubs
    e.render(0.2, sine(440));
    const state = e.x.tape_looper_state();
    assert.ok(state & 1, `looper not playing (state ${state})`);
    assert.ok(!(state & 8), 'looper still empty');
    const replay = rms(e.render(0.8)); // input removed: only the loop remains
    assert.ok(replay > 0.005, `loop playback rms ${replay}`);
  },

  // ------------------------------------------------------------------ WAVE
  async 'WAVE: the firmware loader finds all factory wavetables'() {
    const e = await boot('wave');
    await e.card('wave-1.0', waveTables);
    assert.equal(e.x.wave_boot(), waveTables.length);
  },

  async 'WAVE: notes play, and table and cycle change the timbre'() {
    const e = await boot('wave');
    await e.card('wave-1.0', waveTables);
    e.x.wave_boot();
    e.x.wave_note(57, 1);
    const a = tail(e.render(0.5));
    e.x.wave_set(1, 1);
    const b = tail(e.render(0.5));
    e.x.wave_table(3);
    const c = tail(e.render(0.5));
    assert.ok(rms(a) > 0.003, `note rms ${rms(a)}`);
    const ba = brightness(a), bb = brightness(b), bc = brightness(c);
    assert.ok(Math.abs(bb - ba) / ba > 0.05 || Math.abs(bc - bb) / bb > 0.05, `timbre unchanged: ${ba} ${bb} ${bc}`);
    e.x.wave_note(57, 0);
    e.render(1);
    assert.ok(rms(e.render(0.2)) < rms(a) * 0.05);
  },

  // ------------------------------------------------------------------ TEMPO
  async 'TEMPO: loads the factory card and plays chromatic and slice samples'() {
    const e = await boot('tempo');
    await e.card('tempo-1.0', [...tempoFiles, 'options.json']);
    e.x.tempo_boot();
    for (let s = 1; s <= 14; s++) {
      assert.ok(e.x.tempo_valid_sample(0, s), `chromatic slot ${s} not loaded`);
      assert.ok(e.x.tempo_valid_sample(1, s), `slice slot ${s} not loaded`);
    }
    e.x.tempo_sample(0, 0);
    e.x.tempo_note(60, 1);
    assert.ok(rms(e.render(0.4)) > 0.003, 'chromatic note silent');
    e.x.tempo_note(60, 0); e.render(1);
    e.x.tempo_engine(1); e.x.tempo_sample(1, 0);
    e.x.tempo_note(60, 1);
    assert.ok(rms(e.render(0.4)) > 0.003, 'slice key silent');
  },

  async 'TEMPO: the arpeggiator steps through held keys, faster with the clock'() {
    const steps = async (clock) => {
      const e = await boot('tempo');
      await e.card('tempo-1.0', [...tempoFiles, 'options.json']);
      e.x.tempo_boot();
      e.x.tempo_tempo(clock);
      e.x.tempo_play(1); e.x.tempo_play(0);
      for (const n of [60, 64, 67]) e.x.tempo_note(n, 1);
      return pitchSteps(e.render(4));
    };
    const slow = await steps(160), fast = await steps(480);
    assert.ok(slow >= 4, `slow clock gave ${slow} steps`);
    assert.ok(fast > slow * 1.5, `fast clock ${fast} steps vs slow ${slow}`);
  },
};

let failed = 0;
for (const [name, fn] of Object.entries(tests)) {
  try {
    await fn();
    console.log(`ok   ${name}`);
  } catch (err) {
    failed++;
    console.log(`FAIL ${name}\n     ${err.message}`);
  }
}
process.exit(failed ? 1 : 0);
