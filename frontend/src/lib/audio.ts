/**
 * UI sound synthesiser (Web Audio, no samples).
 *
 * click():     a Karplus–Strong plucked string voiced like a guitar neck pickup:
 *              a short noise burst excites a feedback delay tuned to the note,
 *              a low-pass in the loop gives the warm, rounded neck tone, and a
 *              comb notch models the pickup's position along the string.
 * validate():  a 100 Hz sub-bass punch (sine with a fast pitch drop) through a
 *              400 Hz peaking cut ("scoop"), so it reads as weight, not boom.
 * reject():    the same punch, lower and shorter, for failed checks.
 *
 * The AudioContext is created lazily on the first user gesture.
 */

let ctx: AudioContext | null = null;
let master: GainNode | null = null;
let enabled = true;
let volume = 0.6;
export type SoundPack = "guitar" | "soft" | "mechanical" | "scifi" | "chiptune" | "marimba";
let pack: SoundPack = "guitar";
export const SOUND_PACKS: { key: SoundPack; label: string; hint: string }[] = [
  { key: "guitar", label: "Studio Guitar", hint: "Warm plucked strings" },
  { key: "soft", label: "Soft", hint: "Quiet rounded blips" },
  { key: "mechanical", label: "Mechanical", hint: "Crisp keyboard-switch clicks" },
  { key: "scifi", label: "Sci-Fi", hint: "Bright synth chirps" },
  { key: "chiptune", label: "Chiptune", hint: "8-bit square-wave beeps" },
  { key: "marimba", label: "Marimba", hint: "Wooden mallet tones" },
];
export function setSoundPack(p: SoundPack) {
  pack = p;
}
export function setVolume(v: number) {
  volume = Math.max(0, Math.min(1, v));
  if (master) master.gain.value = volume * 0.85;
}

function graph(): { ctx: AudioContext; master: GainNode } | null {
  if (!enabled || typeof window === "undefined") return null;
  if (!ctx) {
    ctx = new AudioContext({ latencyHint: "interactive" });
    master = ctx.createGain();
    master.gain.value = volume * 0.85;
    const limiter = ctx.createDynamicsCompressor();
    limiter.threshold.value = -6;
    limiter.ratio.value = 12;
    master.connect(limiter).connect(ctx.destination);
  }
  if (ctx.state === "suspended") void ctx.resume();
  return { ctx, master: master! };
}

export function setSoundEnabled(on: boolean) {
  enabled = on;
}

/** A short tone with an attack/decay envelope: the building block of the synth packs. */
function tone(type: OscillatorType, f0: number, f1: number, length: number, level: number, delay = 0, filter?: number) {
  const g = graph();
  if (!g) return;
  const { ctx, master } = g;
  const now = ctx.currentTime + delay;
  const osc = ctx.createOscillator();
  osc.type = type;
  osc.frequency.setValueAtTime(f0, now);
  if (f1 !== f0) osc.frequency.exponentialRampToValueAtTime(f1, now + length * 0.8);
  const env = ctx.createGain();
  env.gain.setValueAtTime(0.0001, now);
  env.gain.exponentialRampToValueAtTime(level, now + 0.004);
  env.gain.exponentialRampToValueAtTime(0.0001, now + length);
  let node: AudioNode = osc;
  if (filter) {
    const lp = ctx.createBiquadFilter();
    lp.type = "lowpass";
    lp.frequency.value = filter;
    node = osc.connect(lp);
  }
  node.connect(env).connect(master);
  osc.start(now);
  osc.stop(now + length + 0.02);
}

/** Band-passed noise tick: switch clicks. */
function tick(centre: number, length: number, level: number, delay = 0) {
  const g = graph();
  if (!g) return;
  const { ctx, master } = g;
  const now = ctx.currentTime + delay;
  const buf = ctx.createBuffer(1, Math.ceil(ctx.sampleRate * length), ctx.sampleRate);
  const d = buf.getChannelData(0);
  for (let i = 0; i < d.length; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / d.length, 3);
  const src = ctx.createBufferSource();
  src.buffer = buf;
  const bp = ctx.createBiquadFilter();
  bp.type = "bandpass";
  bp.frequency.value = centre;
  bp.Q.value = 1.4;
  const env = ctx.createGain();
  env.gain.value = level;
  src.connect(bp).connect(env).connect(master);
  src.start(now);
}

/** Marimba: a sine fundamental plus the bar's 4th partial, both decaying fast. */
function mallet(f: number, level: number, delay = 0) {
  tone("sine", f, f, 0.35, level, delay);
  tone("sine", f * 4, f * 4, 0.08, level * 0.35, delay);
}

export function click(frequency = 196, velocity = 0.5) {
  const r = frequency / 196;  // callers vary pitch around 196 Hz
  if (pack === "soft") return tone("sine", 520 * r, 480 * r, 0.09, velocity * 0.35, 0, 1800);
  if (pack === "mechanical") return (tick(3200 * Math.sqrt(r), 0.018, velocity * 1.1), tick(1400, 0.03, velocity * 0.5, 0.012));
  if (pack === "scifi") return tone("sawtooth", 900 * r, 1800 * r, 0.07, velocity * 0.22, 0, 3500);
  if (pack === "chiptune") return tone("square", 660 * r, 660 * r, 0.05, velocity * 0.16);
  if (pack === "marimba") return mallet(392 * r, velocity * 0.55);
  const g = graph();
  if (!g) return;
  const { ctx, master } = g;
  const now = ctx.currentTime;
  const period = 1 / frequency;

  // excitation: 1 period of filtered noise
  const burst = ctx.createBuffer(1, Math.ceil(ctx.sampleRate * period), ctx.sampleRate);
  const data = burst.getChannelData(0);
  for (let i = 0; i < data.length; i++) data[i] = Math.random() * 2 - 1;
  const source = ctx.createBufferSource();
  source.buffer = burst;

  const delay = ctx.createDelay(0.05);
  delay.delayTime.value = period;
  const loopFilter = ctx.createBiquadFilter();
  loopFilter.type = "lowpass";
  loopFilter.frequency.value = 2200; // neck pickup: darker than bridge
  const feedback = ctx.createGain();
  feedback.gain.value = 0.965;

  // pickup at ~1/4 of the string length → comb notch at 4× the fundamental
  const pickup = ctx.createBiquadFilter();
  pickup.type = "notch";
  pickup.frequency.value = frequency * 4;
  pickup.Q.value = 2;

  const out = ctx.createGain();
  out.gain.setValueAtTime(velocity, now);
  out.gain.exponentialRampToValueAtTime(0.0001, now + 0.45);

  source.connect(delay);
  delay.connect(loopFilter).connect(feedback).connect(delay);
  delay.connect(pickup).connect(out).connect(master);
  source.start(now);
  source.stop(now + period);
  setTimeout(() => {
    feedback.disconnect();
    out.disconnect();
  }, 600);
}

function punch(base: number, length: number, level: number) {
  const g = graph();
  if (!g) return;
  const { ctx, master } = g;
  const now = ctx.currentTime;

  const osc = ctx.createOscillator();
  osc.type = "sine";
  osc.frequency.setValueAtTime(base * 2.2, now);
  osc.frequency.exponentialRampToValueAtTime(base, now + 0.045);

  const click = ctx.createOscillator(); // short transient so it reads on laptop speakers
  click.type = "triangle";
  click.frequency.setValueAtTime(base * 8, now);

  const scoop = ctx.createBiquadFilter();
  scoop.type = "peaking";
  scoop.frequency.value = 400;
  scoop.Q.value = 1.2;
  scoop.gain.value = -10;

  const env = ctx.createGain();
  env.gain.setValueAtTime(0.0001, now);
  env.gain.exponentialRampToValueAtTime(level, now + 0.006);
  env.gain.exponentialRampToValueAtTime(0.0001, now + length);

  const clickEnv = ctx.createGain();
  clickEnv.gain.setValueAtTime(level * 0.25, now);
  clickEnv.gain.exponentialRampToValueAtTime(0.0001, now + 0.02);

  osc.connect(scoop);
  click.connect(clickEnv).connect(scoop);
  scoop.connect(env).connect(master);
  osc.start(now);
  click.start(now);
  osc.stop(now + length + 0.05);
  click.stop(now + 0.03);
}

export function validate() {
  if (pack === "soft") return (tone("sine", 660, 660, 0.18, 0.3), tone("sine", 990, 990, 0.25, 0.25, 0.09));
  if (pack === "mechanical") return (tick(2600, 0.02, 0.9), tick(2600, 0.02, 0.9, 0.07));
  if (pack === "scifi") return tone("sawtooth", 600, 2400, 0.3, 0.2, 0, 4000);
  if (pack === "chiptune") return [523, 659, 784].forEach((f, i) => tone("square", f, f, 0.08, 0.14, i * 0.07));
  if (pack === "marimba") return (mallet(523, 0.5), mallet(784, 0.45, 0.1));
  punch(100, 0.38, 0.9);
}
export function reject() {
  if (pack === "soft") return tone("sine", 330, 220, 0.3, 0.3);
  if (pack === "mechanical") return tick(700, 0.06, 1.1);
  if (pack === "scifi") return tone("sawtooth", 500, 120, 0.35, 0.22, 0, 2000);
  if (pack === "chiptune") return [392, 262].forEach((f, i) => tone("square", f, f, 0.12, 0.14, i * 0.1));
  if (pack === "marimba") return (mallet(330, 0.5), mallet(247, 0.45, 0.12));
  punch(62, 0.22, 0.7);
}
