/**
 * Easing functions ported from phic_renderer/math/easing.py
 * These functions take a value t in [0, 1] and return an eased value
 */

export type EasingFunction = (t: number) => number

export const ease_01 = (t: number): number => t
export const ease_02 = (t: number): number => Math.sin(Math.PI * t / 2)
export const ease_03 = (t: number): number => 1 - Math.cos(Math.PI * t / 2)
export const ease_04 = (t: number): number => 1 - (1 - t) * (1 - t)
export const ease_05 = (t: number): number => t * t
export const ease_06 = (t: number): number => -(Math.cos(Math.PI * t) - 1) / 2

export const ease_07 = (t: number): number =>
  t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2

export const ease_08 = (t: number): number => 1 - Math.pow(1 - t, 3)
export const ease_09 = (t: number): number => Math.pow(t, 3)
export const ease_10 = (t: number): number => 1 - Math.pow(1 - t, 4)
export const ease_11 = (t: number): number => Math.pow(t, 4)

export const ease_12 = (t: number): number =>
  t < 0.5 ? 4 * Math.pow(t, 3) : 1 - Math.pow(-2 * t + 2, 3) / 2

export const ease_13 = (t: number): number =>
  t < 0.5 ? 8 * Math.pow(t, 4) : 1 - Math.pow(-2 * t + 2, 4) / 2

export const ease_14 = (t: number): number => 1 - Math.pow(1 - t, 5)
export const ease_15 = (t: number): number => Math.pow(t, 5)

export const ease_16 = (t: number): number =>
  t === 1 ? 1 : 1 - Math.pow(2, -10 * t)

export const ease_17 = (t: number): number =>
  t === 0 ? 0 : Math.pow(2, 10 * t - 10)

export const ease_18 = (t: number): number => Math.pow(1 - (t - 1) * (t - 1), 0.5)
export const ease_19 = (t: number): number => 1 - Math.pow(1 - t * t, 0.5)

export const ease_20 = (t: number): number => {
  const x = t - 1
  return 1 + 2.70158 * Math.pow(x, 3) + 1.70158 * Math.pow(x, 2)
}

export const ease_21 = (t: number): number =>
  2.70158 * Math.pow(t, 3) - 1.70158 * Math.pow(t, 2)

export const ease_22 = (t: number): number => {
  if (t < 0.5) return (1 - Math.pow(1 - Math.pow(2 * t, 2), 0.5)) / 2
  return (Math.pow(1 - Math.pow(-2 * t + 2, 2), 0.5) + 1) / 2
}

export const ease_23 = (t: number): number => {
  const s = 2.5949095
  if (t < 0.5) {
    const x = 2 * t
    return (x * x * ((s + 1) * x - s)) / 2
  }
  const x = 2 * t - 2
  return (x * x * ((s + 1) * x + s) + 2) / 2
}

export const ease_24 = (t: number): number => {
  if (t === 0) return 0
  if (t === 1) return 1
  return Math.pow(2, -10 * t) * Math.sin((t * 10 - 0.75) * (2 * Math.PI / 3)) + 1
}

export const ease_25 = (t: number): number => {
  if (t === 0) return 0
  if (t === 1) return 1
  return -Math.pow(2, 10 * t - 10) * Math.sin((t * 10 - 10.75) * (2 * Math.PI / 3))
}

export const ease_26 = (t: number): number => {
  if (t < 1 / 2.75) return 7.5625 * t * t
  if (t < 2 / 2.75) {
    const x = t - 1.5 / 2.75
    return 7.5625 * x * x + 0.75
  }
  if (t < 2.5 / 2.75) {
    const x = t - 2.25 / 2.75
    return 7.5625 * x * x + 0.9375
  }
  const x = t - 2.625 / 2.75
  return 7.5625 * x * x + 0.984375
}

export const ease_27 = (t: number): number => 1 - ease_26(1 - t)

export const ease_28 = (t: number): number =>
  t < 0.5
    ? (1 - ease_26(1 - 2 * t)) / 2
    : (1 + ease_26(2 * t - 1)) / 2

export const ease_29 = (t: number): number => {
  if (t === 0) return 0
  if (t === 1) return 1
  const k = (2 * Math.PI) / 4.5
  if (t < 0.5) return -(Math.pow(2, 20 * t - 10) * Math.sin((20 * t - 11.125) * k)) / 2
  return (Math.pow(2, -20 * t + 10) * Math.sin((20 * t - 11.125) * k)) / 2 + 1
}

/**
 * Map easing type ID to easing function
 * Matches Python's easing_from_type function
 */
export function easingFromType(tp: number): EasingFunction {
  const map: Record<number, EasingFunction> = {
    0: ease_01,
    1: ease_01,
    2: ease_02,
    3: ease_03,
    4: ease_04,
    5: ease_05,
    6: ease_06,
    7: ease_07,
    8: ease_08,
    9: ease_09,
    10: ease_10,
    11: ease_11,
    12: ease_12,
    13: ease_13,
    14: ease_14,
    15: ease_15,
    16: ease_16,
    17: ease_17,
    18: ease_18,
    19: ease_19,
    20: ease_20,
    21: ease_21,
    22: ease_22,
    23: ease_23,
    24: ease_24,
    25: ease_25,
    26: ease_26,
    27: ease_27,
    28: ease_28,
    29: ease_29,
  }
  return map[tp] ?? ease_01
}

/**
 * Global easing shift for RPE easingType (some exporters are 1-based)
 */
export let rpeEasingShift = 0

export function setRpeEasingShift(shift: number): void {
  rpeEasingShift = Math.floor(shift)
}

/**
 * Cubic Bezier curve evaluation
 * Solves for y given x using binary search
 * Control points: (0,0), (x1,y1), (x2,y2), (1,1)
 */
export function cubicBezierYForX(
  x1: number,
  y1: number,
  x2: number,
  y2: number,
  x: number,
  iters = 18
): number {
  const bx = (u: number): number => {
    const a = 1 - u
    return 3 * a * a * u * x1 + 3 * a * u * u * x2 + u * u * u
  }

  const by = (u: number): number => {
    const a = 1 - u
    return 3 * a * a * u * y1 + 3 * a * u * u * y2 + u * u * u
  }

  let lo = 0.0
  let hi = 1.0

  for (let i = 0; i < iters; i++) {
    const mid = (lo + hi) * 0.5
    if (bx(mid) < x) {
      lo = mid
    } else {
      hi = mid
    }
  }

  return by((lo + hi) * 0.5)
}
