/**
 * Track interpolation system ported from phic_renderer/math/tracks.py
 * Used for piecewise easing animations and scroll calculations
 */

import { clamp, lerp } from './util.js'
import type { EasingFunction } from './easing.js'

/**
 * A single eased segment
 */
export interface EasedSeg {
  t0: number
  t1: number
  v0: number
  v1: number
  easing: EasingFunction
  L: number  // Clip window start (0..1)
  R: number  // Clip window end (0..1)
}

/**
 * Piecewise eased track - interpolates values with easing functions
 * Ported from phic_renderer/math/tracks.py:19-52
 */
export class PiecewiseEased {
  private segs: EasedSeg[]
  private default: number
  private i: number = 0

  constructor(segs: EasedSeg[], defaultValue: number = 0.0) {
    this.segs = segs
    this.default = defaultValue
  }

  private _seek(t: number): void {
    if (this.segs.length === 0) {
      this.i = 0
      return
    }

    // Seek forward
    while (this.i + 1 < this.segs.length && t >= this.segs[this.i].t1) {
      this.i++
    }

    // Seek backward
    while (this.i > 0 && t < this.segs[this.i].t0) {
      this.i--
    }
  }

  /**
   * Evaluate the track at time t
   */
  eval(t: number): number {
    if (this.segs.length === 0) {
      return this.default
    }

    this._seek(t)
    const s = this.segs[this.i]

    if (t <= s.t0) return s.v0
    if (t >= s.t1) return s.v1

    const p_raw = (t - s.t0) / (s.t1 - s.t0)

    // Apply clip window [L, R]
    let p: number
    if (p_raw <= s.L) {
      p = 0.0
    } else if (p_raw >= s.R) {
      p = 1.0
    } else {
      p = (p_raw - s.L) / Math.max(1e-9, s.R - s.L)
    }

    p = clamp(p, 0.0, 1.0)
    const e = s.easing(p)
    return lerp(s.v0, s.v1, e)
  }
}

/**
 * Sum of multiple tracks
 * Ported from phic_renderer/math/tracks.py:54-62
 */
export class SumTrack {
  private tracks: PiecewiseEased[]
  private default: number

  constructor(tracks: PiecewiseEased[], defaultValue: number = 0.0) {
    this.tracks = tracks
    this.default = defaultValue
  }

  eval(t: number): number {
    if (this.tracks.length === 0) {
      return this.default
    }
    return this.tracks.reduce((sum, track) => sum + track.eval(t), 0)
  }
}

/**
 * A single 1D segment for integral calculation
 */
export interface Seg1D {
  t0: number
  t1: number
  v0: number  // Velocity at t0
  v1: number  // Velocity at t1
  prefix: number  // Accumulated integral from 0 to t0
}

/**
 * Integral track - computes integral of piecewise linear velocity
 * Used for scroll position calculation
 * Ported from phic_renderer/math/tracks.py:66-106
 */
export class IntegralTrack {
  private segs: Seg1D[]
  private i: number = 0

  constructor(segs: Seg1D[]) {
    this.segs = segs
  }

  private _seek(t: number): void {
    if (this.segs.length === 0) {
      this.i = 0
      return
    }

    // Seek forward
    while (this.i + 1 < this.segs.length && t >= this.segs[this.i].t1) {
      this.i++
    }

    // Seek backward
    while (this.i > 0 && t < this.segs[this.i].t0) {
      this.i--
    }
  }

  /**
   * Compute integral from 0 to t
   * Uses trapezoidal rule for piecewise linear integration
   */
  integral(t: number): number {
    if (this.segs.length === 0) {
      return 0.0
    }

    this._seek(t)
    const s = this.segs[this.i]

    if (t <= s.t0) {
      return s.prefix
    }

    if (t >= s.t1) {
      const dt = s.t1 - s.t0
      const area = 0.5 * (s.v0 + s.v1) * dt
      return s.prefix + area
    }

    // Partial segment
    const dt = t - s.t0
    const full = s.t1 - s.t0
    const u = dt / Math.max(1e-9, full)
    const vt = lerp(s.v0, s.v1, u)
    const area = 0.5 * (s.v0 + vt) * dt
    return s.prefix + area
  }

  /**
   * Return absolute instantaneous speed (|v|) at time t.
   * Used by visibility precompute to skip scanning when scroll speed is ~0.
   */
  speedAbsAt(t: number): number | null {
    if (this.segs.length === 0) return null

    // Find segment where t belongs.
    for (const s of this.segs) {
      if (t < s.t0) break
      if (t <= s.t1) return Math.abs(s.v0)
    }

    // After last segment: use last velocity.
    const last = this.segs[this.segs.length - 1]
    return Math.abs(last.v1 ?? last.v0)
  }
}

/**
 * A single color segment
 */
export interface ColorSeg {
  t0: number
  t1: number
  c0: [number, number, number]  // RGB tuple
  c1: [number, number, number]
  easing: EasingFunction
  L: number
  R: number
}

/**
 * Piecewise color interpolation
 * Ported from phic_renderer/math/tracks.py:109-158
 */
export class PiecewiseColor {
  private segs: ColorSeg[]
  private default: [number, number, number]
  private i: number = 0

  constructor(segs: ColorSeg[], defaultColor: [number, number, number] = [255, 255, 255]) {
    this.segs = segs
    this.default = defaultColor
  }

  private _seek(t: number): void {
    if (this.segs.length === 0) {
      this.i = 0
      return
    }

    while (this.i + 1 < this.segs.length && t >= this.segs[this.i].t1) {
      this.i++
    }

    while (this.i > 0 && t < this.segs[this.i].t0) {
      this.i--
    }
  }

  eval(t: number): [number, number, number] {
    if (this.segs.length === 0) {
      return this.default
    }

    this._seek(t)
    const s = this.segs[this.i]

    if (t <= s.t0) return s.c0
    if (t >= s.t1) return s.c1

    const p_raw = (t - s.t0) / (s.t1 - s.t0)

    let p: number
    if (p_raw <= s.L) {
      p = 0.0
    } else if (p_raw >= s.R) {
      p = 1.0
    } else {
      p = (p_raw - s.L) / Math.max(1e-9, s.R - s.L)
    }

    p = clamp(p, 0.0, 1.0)
    const e = s.easing(p)

    const r = Math.floor(lerp(s.c0[0], s.c1[0], e))
    const g = Math.floor(lerp(s.c0[1], s.c1[1], e))
    const b = Math.floor(lerp(s.c0[2], s.c1[2], e))

    return [clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255)]
  }
}

/**
 * A single text segment
 */
export interface TextSeg {
  t0: number
  t1: number
  s0: string
  s1: string
}

/**
 * Piecewise text selection
 * Ported from phic_renderer/math/tracks.py:161-198
 */
export class PiecewiseText {
  private segs: TextSeg[]
  private default: string
  private i: number = 0

  constructor(segs: TextSeg[], defaultText: string = '') {
    this.segs = segs
    this.default = defaultText
  }

  private _seek(t: number): void {
    if (this.segs.length === 0) {
      this.i = 0
      return
    }

    while (this.i + 1 < this.segs.length && t >= this.segs[this.i].t1) {
      this.i++
    }

    while (this.i > 0 && t < this.segs[this.i].t0) {
      this.i--
    }
  }

  eval(t: number): string {
    if (this.segs.length === 0) {
      return this.default
    }

    this._seek(t)
    const s = this.segs[this.i]

    if (t <= s.t0) return s.s0
    if (t >= s.t1) return s.s1

    if (s.s0 === s.s1) return s.s0

    // Switch at midpoint
    const mid = (s.t0 + s.t1) * 0.5
    return t < mid ? s.s0 : s.s1
  }
}
