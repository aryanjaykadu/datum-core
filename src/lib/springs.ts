/**
 * Mass-spring motion. Each preset is a damped harmonic oscillator
 * (m·x'' + c·x' + k·x = 0). `springBezier` fits a cubic-bezier to its step
 * response so CSS transitions follow the same physics as JS animations.
 */

export interface Spring {
  mass: number;
  stiffness: number;
  damping: number;
}

export const SPRINGS = {
  panel: { mass: 1, stiffness: 280, damping: 30 },
  press: { mass: 0.6, stiffness: 520, damping: 26 },
  overlay: { mass: 1, stiffness: 200, damping: 24 },
} satisfies Record<string, Spring>;

/** Displacement 0→1 of a spring released from rest at time t (seconds). */
export function springStep({ mass, stiffness, damping }: Spring, t: number): number {
  const w0 = Math.sqrt(stiffness / mass);
  const zeta = damping / (2 * Math.sqrt(stiffness * mass));
  if (zeta < 1) {
    const wd = w0 * Math.sqrt(1 - zeta * zeta);
    return 1 - Math.exp(-zeta * w0 * t) * (Math.cos(wd * t) + ((zeta * w0) / wd) * Math.sin(wd * t));
  }
  return 1 - Math.exp(-w0 * t) * (1 + w0 * t);
}

/** Time until the spring stays within 0.1 % of rest. */
export function settleTime(spring: Spring): number {
  const w0 = Math.sqrt(spring.stiffness / spring.mass);
  const zeta = spring.damping / (2 * Math.sqrt(spring.stiffness * spring.mass));
  return Math.min(Math.log(1000) / (Math.min(zeta, 1) * w0), 2);
}

/** Least-squares fit of cubic-bezier(x1, y1, x2, y2) to the step response. */
export function springBezier(spring: Spring): { css: string; durationMs: number } {
  const T = settleTime(spring);
  const samples = Array.from({ length: 24 }, (_, i) => {
    const u = (i + 1) / 25;
    return [u, springStep(spring, u * T)] as const;
  });
  const bezier = (p1: number, p2: number, s: number) => 3 * (1 - s) ** 2 * s * p1 + 3 * (1 - s) * s * s * p2 + s ** 3;
  let best = { err: Infinity, x1: 0.2, y1: 1, x2: 0.3, y2: 1 };
  for (let x1 = 0.05; x1 <= 0.6; x1 += 0.05)
    for (let y1 = 0.6; y1 <= 1.6; y1 += 0.1)
      for (let x2 = 0.2; x2 <= 0.9; x2 += 0.1) {
        const y2 = 1;
        let err = 0;
        for (const [u, target] of samples) {
          let lo = 0;
          let hi = 1;
          for (let n = 0; n < 14; n++) {
            const mid = (lo + hi) / 2;
            if (bezier(x1, x2, mid) < u) lo = mid;
            else hi = mid;
          }
          const s = (lo + hi) / 2; // x(s) = u, solved by bisection (x is monotonic for x1, x2 in [0, 1])
          err += (bezier(y1, y2, s) - target) ** 2;
        }
        if (err < best.err) best = { err, x1, y1, x2, y2 };
      }
  const f = (v: number) => v.toFixed(3);
  return { css: `cubic-bezier(${f(best.x1)}, ${f(best.y1)}, ${f(best.x2)}, ${f(best.y2)})`, durationMs: Math.round(T * 1000) };
}

/** Publish every preset as CSS custom properties (--spring-panel, --spring-panel-ms, ...). */
export function installSpringVariables(root: HTMLElement = document.documentElement): void {
  for (const [name, spring] of Object.entries(SPRINGS)) {
    const { css, durationMs } = springBezier(spring);
    root.style.setProperty(`--spring-${name}`, css);
    root.style.setProperty(`--spring-${name}-ms`, `${durationMs}ms`);
  }
}

/** Cubic ease-out used by the score counter. */
export const easeOutCubic = (t: number) => 1 - (1 - t) ** 3;
