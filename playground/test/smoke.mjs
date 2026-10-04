// Smoke test for web/tape_fx.wasm: loads it the same way the AudioWorklet does
// and checks that each control audibly does what the firmware says it does.
// Run with `make test` (or `node test/smoke.mjs`).

import { readFile } from 'node:fs/promises';
import assert from 'node:assert/strict';

const bytes = await readFile(new URL('../web/tape_fx.wasm', import.meta.url));

async function engine() {
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const x = instance.exports;
  x._initialize();
  x.tape_fx_init();
  const n = x.tape_fx_max_block();
  const view = (fn) => new Float32Array(x.memory.buffer, fn(), n);
  return {
    x,
    n,
    sr: x.tape_fx_sample_rate(),
    inL: view(x.tape_fx_in_l),
    inR: view(x.tape_fx_in_r),
    outL: view(x.tape_fx_out_l),
    outR: view(x.tape_fx_out_r),
  };
}

// Run `seconds` of a sine through the engine; return output RMS over the last half.
function run(e, { hz, amp = 0.3, seconds = 1, setup = () => {} }) {
  setup(e.x);
  const blocks = Math.ceil((seconds * e.sr) / e.n);
  let sum = 0, count = 0, phase = 0;
  const out = [];
  for (let b = 0; b < blocks; b++) {
    for (let i = 0; i < e.n; i++) {
      const s = amp * Math.sin(phase);
      phase += (2 * Math.PI * hz) / e.sr;
      e.inL[i] = s;
      e.inR[i] = s;
    }
    e.x.tape_fx_process(e.n);
    for (let i = 0; i < e.n; i++) {
      assert.ok(Number.isFinite(e.outL[i]) && Number.isFinite(e.outR[i]), 'non-finite output');
      out.push(e.outL[i]);
      if (b >= blocks / 2) { sum += e.outL[i] ** 2; count++; }
    }
  }
  return { rms: Math.sqrt(sum / count), out };
}

const tests = {
  async 'runs at the firmware sample rate'() {
    const e = await engine();
    assert.equal(e.sr, 48000);
    assert.equal(e.n, 128);
  },

  async 'bypass passes input through untouched'() {
    const e = await engine();
    e.x.tape_fx_set_bypass(1);
    e.inL.forEach((_, i) => { e.inL[i] = Math.sin(i); e.inR[i] = Math.cos(i); });
    e.x.tape_fx_process(e.n);
    assert.deepEqual([...e.outL], [...e.inL]);
    assert.deepEqual([...e.outR], [...e.inR]);
  },

  async 'default knobs (magic wand reset) leave level roughly unchanged'() {
    const { rms } = run(await engine(), { hz: 440 });
    const inRms = 0.3 / Math.SQRT2;
    assert.ok(rms > inRms * 0.5 && rms < inRms * 1.5, `rms ${rms}`);
  },

  async 'filter knob down darkens: a 5 kHz tone is cut hard'() {
    const open = run(await engine(), { hz: 5000 }).rms;
    const dark = run(await engine(), { hz: 5000, setup: (x) => x.tape_fx_set_filter(0.1) }).rms;
    assert.ok(dark < open * 0.1, `open ${open} dark ${dark}`);
  },

  async 'filter knob up is a high-pass: a 100 Hz tone is cut hard'() {
    const open = run(await engine(), { hz: 100 }).rms;
    const thin = run(await engine(), { hz: 100, setup: (x) => x.tape_fx_set_filter(1) }).rms;
    assert.ok(thin < open * 0.25, `open ${open} thin ${thin}`);
  },

  async 'saturate adds harmonics to a quiet sine'() {
    // Compare how far each output is from a pure sine fit by peak/RMS (crest factor).
    // A clean sine is ~1.414; soft clipping squares it off toward 1.
    const crest = ({ out }) => {
      const tail = out.slice(out.length / 2);
      const peak = Math.max(...tail.map(Math.abs));
      const rms = Math.sqrt(tail.reduce((a, s) => a + s * s, 0) / tail.length);
      return peak / rms;
    };
    const clean = crest(run(await engine(), { hz: 220, amp: 0.5 }));
    const hot = crest(run(await engine(), { hz: 220, amp: 0.5, setup: (x) => x.tape_fx_set_saturate(1) }));
    assert.ok(clean > 1.35, `clean crest ${clean}`);
    assert.ok(hot < 1.2, `saturated crest ${hot}`);
  },

  async 'warble modulates pitch: zero crossings drift'() {
    // Count zero crossings per 100 ms window; warble's moving delay bends the
    // pitch so the counts spread out, while the dry signal stays constant.
    const spread = ({ out }) => {
      const win = 4800, counts = [];
      for (let w = win * 5; w + win <= out.length; w += win) {
        let c = 0;
        for (let i = w + 1; i < w + win; i++) if ((out[i - 1] < 0) !== (out[i] < 0)) c++;
        counts.push(c);
      }
      return Math.max(...counts) - Math.min(...counts);
    };
    const dry = spread(run(await engine(), { hz: 1000, seconds: 6 }));
    const wobbly = spread(run(await engine(), { hz: 1000, seconds: 6, setup: (x) => x.tape_fx_set_warble(1) }));
    assert.ok(dry <= 2, `dry spread ${dry}`);
    assert.ok(wobbly > 4, `warble spread ${wobbly}`);
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
