import type { RuntimeLine, RuntimeNote } from '@phic-web/shared'
import { IntegralTrack, clamp } from '@phic-web/shared'

interface LineState {
  x: number
  y: number
  rot: number
  scroll: number
}

function evalLineStateSimple(line: RuntimeLine, t: number): LineState {
  const x = typeof line.pos_x === 'function' ? line.pos_x(t) : line.pos_x.eval(t)
  const y = typeof line.pos_y === 'function' ? line.pos_y(t) : line.pos_y.eval(t)
  const rot = typeof line.rot === 'function' ? line.rot(t) : line.rot.eval(t)

  let scroll = 0.0
  if (typeof line.scroll_px === 'function') {
    scroll = line.scroll_px(t)
  } else {
    scroll = (line.scroll_px as IntegralTrack).integral(t)
  }

  return { x, y, rot, scroll }
}

function noteWorldPosSimple(
  lineX: number,
  lineY: number,
  rot: number,
  scrollNow: number,
  note: RuntimeNote,
  scrollTarget: number,
  forTail: boolean
): [number, number] {
  const tx = Math.cos(rot)
  const ty = Math.sin(rot)
  const nx = -Math.sin(rot)
  const ny = Math.cos(rot)

  const sgn = note.above ? 1.0 : -1.0
  const xLocal = note.x_local_px

  const dy = scrollTarget - scrollNow

  let mult = 1.0
  if (forTail && note.kind === 3) {
    mult = Math.max(0.0, note.speed_mul)
  }

  const yLocal = sgn * dy * mult + (note.y_offset_px ?? 0.0)

  const x = lineX + tx * xLocal + nx * yLocal
  const y = lineY + ty * xLocal + ny * yLocal
  return [x, y]
}

function scrollSpeedAbsPxPerSec(scrollTrack: any, t: number): number | null {
  if (!scrollTrack) return null
  if (typeof scrollTrack === 'function') return null
  try {
    if (scrollTrack instanceof IntegralTrack) {
      return scrollTrack.speedAbsAt(t)
    }
  } catch {
    // ignore
  }
  return null
}

function noteVisibleOnScreen(
  lines: RuntimeLine[],
  note: RuntimeNote,
  t: number,
  W: number,
  H: number,
  margin: number,
  baseW: number,
  baseH: number
): boolean {
  const ln = lines[note.line_id]
  const st = evalLineStateSimple(ln, t)
  const [x, y] = noteWorldPosSimple(st.x, st.y, st.rot, st.scroll, note, note.scroll_hit, false)

  const w = baseW * (note.size_px ?? 1.0)
  const h = baseH * (note.size_px ?? 1.0)

  return (
    x + w / 2 >= -margin &&
    x - w / 2 <= W + margin &&
    y + h / 2 >= -margin &&
    y - h / 2 <= H + margin
  )
}

/**
 * Port of phic_renderer/runtime/visibility.py precompute_t_enter.
 * Finds the invisible->visible boundary when scanning backwards from t_hit.
 */
export function precomputeTEnter(
  lines: RuntimeLine[],
  notes: RuntimeNote[],
  W: number,
  H: number,
  lookbackDefault: number = 256.0,
  dt: number = 1 / 30
): void {
  const baseW = Math.max(1, Math.floor(0.06 * W))
  const baseH = Math.max(1, Math.floor(0.018 * H))
  const dt0 = Math.max(1e-4, dt)
  const maxExpandIters = 32

  const margin = Math.max(120, Math.floor(0.18 * Math.max(W, H)))

  for (const n of notes) {
    if (n.fake) {
      n.t_enter = -1e9
      continue
    }

    const tHit = Number(n.t_hit)
    if (!isFinite(tHit)) {
      n.t_enter = -1e9
      continue
    }

    const v = scrollSpeedAbsPxPerSec((lines[n.line_id] as any)?.scroll_px, tHit)
    if (v != null && v <= 1e-4) {
      n.t_enter = -1e9
      continue
    }

    const lookback = lookbackDefault

    // Find a visible point, prefer t_hit
    let tVis = tHit
    const visAtHit = noteVisibleOnScreen(lines, n, tVis, W, H, margin, baseW, baseH)

    if (!visAtHit) {
      let step = dt0
      let found = false
      for (let i = 0; i < maxExpandIters; i++) {
        const t2 = tHit - step
        if (t2 < tHit - lookback) break
        if (noteVisibleOnScreen(lines, n, t2, W, H, margin, baseW, baseH)) {
          tVis = t2
          found = true
          break
        }
        step *= 2.0
      }
      if (!found) {
        n.t_enter = tHit - lookback
        continue
      }
    }

    // Exponential search backwards: find first invisible
    let hi = tVis // visible
    let lo: number | null = null // invisible
    let step = dt0

    for (let i = 0; i < maxExpandIters; i++) {
      const t2 = hi - step
      if (t2 < tHit - lookback) break

      if (noteVisibleOnScreen(lines, n, t2, W, H, margin, baseW, baseH)) {
        hi = t2
        step *= 2.0
      } else {
        lo = t2
        break
      }
    }

    if (lo == null) {
      n.t_enter = tHit - lookback
      continue
    }

    // Binary search boundary (lo invisible, hi visible)
    let lo2 = lo
    let hi2 = hi
    for (let i = 0; i < 20; i++) {
      const mid = (lo2 + hi2) * 0.5
      if (noteVisibleOnScreen(lines, n, mid, W, H, margin, baseW, baseH)) {
        hi2 = mid
      } else {
        lo2 = mid
      }
    }

    n.t_enter = Number(clamp(hi2, -1e9, 1e9))
  }
}
