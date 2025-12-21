/**
 * Kinematics system for note positioning
 * Ported from phic_renderer/runtime/kinematics.py
 */

import type { RuntimeLine, RuntimeNote } from '@phic-web/shared'
import { clamp, IntegralTrack } from '@phic-web/shared'

/**
 * Options for kinematics calculations
 */
export interface KinematicsOptions {
  /** Force alpha for all lines */
  forceLineAlpha01?: number
  /** Force alpha for specific lines by ID */
  forceLineAlpha01ByLid?: Record<number, number>
  /** Note flow speed multiplier (affects approach speed) */
  noteFlowSpeedMultiplier?: number
  /** Hold notes keep head on line (don't pass through) */
  holdKeepHead?: boolean
  /** Speed multiplier affects travel for non-hold notes */
  noteSpeedMulAffectsTravel?: boolean
}

/**
 * Line state at a given time
 * Returns: [x, y, rotation, alpha01, scroll, alphaRaw]
 */
export interface LineState {
  x: number
  y: number
  rot: number
  alpha01: number
  scroll: number
  alphaRaw: number
}

/**
 * Evaluate line state at time t
 * Ported from kinematics.py:10-32
 */
export function evalLineState(
  line: RuntimeLine,
  t: number,
  options?: KinematicsOptions
): LineState {
  // Evaluate position, rotation, alpha
  const x = typeof line.pos_x === 'function'
    ? line.pos_x(t)
    : line.pos_x.eval(t)

  const y = typeof line.pos_y === 'function'
    ? line.pos_y(t)
    : line.pos_y.eval(t)

  const rot = typeof line.rot === 'function'
    ? line.rot(t)
    : line.rot.eval(t)

  let aRaw = typeof line.alpha === 'function'
    ? line.alpha(t)
    : line.alpha.eval(t)

  // Evaluate scroll (integral of velocity)
  const s = (line.scroll_px as IntegralTrack).integral(t)

  // Clamp alpha to [0, 1]
  let a01 = clamp(Math.abs(aRaw), 0.0, 1.0)

  // Apply forced alpha overrides
  if (options?.forceLineAlpha01ByLid && line.lid in options.forceLineAlpha01ByLid) {
    const forced = clamp(options.forceLineAlpha01ByLid[line.lid], 0.0, 1.0)
    a01 = forced
    aRaw = forced
  }

  if (options?.forceLineAlpha01 !== undefined) {
    const forced = clamp(options.forceLineAlpha01, 0.0, 1.0)
    a01 = forced
    aRaw = forced
  }

  return {
    x,
    y,
    rot,
    alpha01: a01,
    scroll: s,
    alphaRaw: aRaw,
  }
}

/**
 * Calculate world position of a note
 * Ported from kinematics.py:34-67
 *
 * @param lineX - Line x position
 * @param lineY - Line y position
 * @param rot - Line rotation in radians
 * @param scrollNow - Current scroll position
 * @param note - Note to position
 * @param scrollTarget - Target scroll position (note.scroll_hit or note.scroll_end)
 * @param forTail - Whether this is for the tail of a hold note
 * @param options - Kinematics options
 * @returns [x, y] world position
 */
export function noteWorldPos(
  lineX: number,
  lineY: number,
  rot: number,
  scrollNow: number,
  note: RuntimeNote,
  scrollTarget: number,
  forTail: boolean = false,
  options?: KinematicsOptions
): [number, number] {
  // Tangent and normal vectors
  const tx = Math.cos(rot)
  const ty = Math.sin(rot)
  const nx = -Math.sin(rot)
  const ny = Math.cos(rot)

  // Direction (above = positive, below = negative)
  const sgn = note.above ? 1.0 : -1.0

  // Local x offset along the tangent
  const xLocal = note.x_local_px

  // Local y offset along the normal (based on scroll delta)
  let dy = scrollTarget - scrollNow

  // Apply note flow speed multiplier
  if (options?.noteFlowSpeedMultiplier !== undefined) {
    dy *= options.noteFlowSpeedMultiplier
  }

  // Hold keep head: prevent head from passing through line
  if (options?.holdKeepHead && note.kind === 3 && !forTail) {
    if (dy < 0.0) {
      dy = 0.0
    }
  }

  // Apply speed multiplier
  let mult = 1.0
  if (forTail && note.kind === 3) {
    // Hold tail: always use speed_mul
    mult = Math.max(0.0, note.speed_mul)
  } else if (!forTail && note.kind !== 3 && options?.noteSpeedMulAffectsTravel) {
    // Non-hold notes: only if noteSpeedMulAffectsTravel is enabled (RPE behavior)
    mult = Math.max(0.0, note.speed_mul)
  }

  const yLocal = sgn * dy * mult + note.y_offset_px

  // Transform to world coordinates
  const x = lineX + tx * xLocal + nx * yLocal
  const y = lineY + ty * xLocal + ny * yLocal

  return [x, y]
}
