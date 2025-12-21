import { describe, it, expect } from 'vitest'
import { clamp, lerp, hsvToRgb } from '../src/math/util'
import { ease_01, ease_06, easingFromType } from '../src/math/easing'
import { PiecewiseEased, IntegralTrack } from '../src/math/tracks'

describe('Math Utilities', () => {
  describe('clamp', () => {
    it('should clamp values within range', () => {
      expect(clamp(5, 0, 10)).toBe(5)
      expect(clamp(-5, 0, 10)).toBe(0)
      expect(clamp(15, 0, 10)).toBe(10)
    })
  })

  describe('lerp', () => {
    it('should interpolate between values', () => {
      expect(lerp(0, 100, 0.0)).toBe(0)
      expect(lerp(0, 100, 0.5)).toBe(50)
      expect(lerp(0, 100, 1.0)).toBe(100)
    })
  })

  describe('hsvToRgb', () => {
    it('should convert HSV to RGB', () => {
      const [r, g, b] = hsvToRgb(0, 1, 1) // Red
      expect(r).toBe(255)
      expect(g).toBe(0)
      expect(b).toBe(0)
    })
  })
})

describe('Easing Functions', () => {
  it('ease_01 (linear) should return input', () => {
    expect(ease_01(0.5)).toBe(0.5)
  })

  it('ease_06 (sine in-out) should work', () => {
    const result = ease_06(0.5)
    expect(result).toBeCloseTo(0.5, 3)
  })

  it('easingFromType should return correct function', () => {
    const easing = easingFromType(1)
    expect(easing(0.5)).toBe(0.5) // Linear
  })
})

describe('Piecewise Track System', () => {
  it('PiecewiseEased should interpolate correctly', () => {
    const track = new PiecewiseEased(
      [
        {
          t0: 0,
          t1: 1,
          v0: 0,
          v1: 100,
          easing: ease_01,
          L: 0,
          R: 1,
        },
      ],
      0
    )

    expect(track.eval(0.0)).toBe(0)
    expect(track.eval(0.5)).toBe(50)
    expect(track.eval(1.0)).toBe(100)
  })

  it('IntegralTrack should compute integral correctly', () => {
    const track = new IntegralTrack([
      {
        t0: 0,
        t1: 1,
        v0: 10, // Constant velocity of 10
        v1: 10,
        prefix: 0,
      },
    ])

    // Integral of constant velocity 10 over time 1 = 10
    expect(track.integral(1.0)).toBeCloseTo(10, 3)

    // Integral at time 0.5 = 0.5 * 10 = 5
    expect(track.integral(0.5)).toBeCloseTo(5, 3)
  })
})
