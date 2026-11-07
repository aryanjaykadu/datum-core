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

function graph(): { ctx: AudioContext; master: GainNode } | null {
  if (!enabled || typeof window === "undefined") return null;
  if (!ctx) {
    ctx = new AudioContext({ latencyHint: "interactive" });
    master = ctx.createGain();
    master.gain.value = 0.5;
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

export function click(frequency = 196, velocity = 0.5) {
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

export const validate = () => punch(100, 0.38, 0.9);
export const reject = () => punch(62, 0.22, 0.7);
