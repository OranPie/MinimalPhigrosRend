/**
 * Official Phigros chart format parser
 * Ported from phic_renderer/formats/official_impl.py
 */

import type { ParsedChart, RuntimeLine, RuntimeNote } from '@phic-web/shared'
import {
  IntegralTrack,
  PiecewiseEased,
  type EasedSeg,
  type Seg1D,
  ease_01,
  hsvToRgb,
} from '@phic-web/shared'

import { precomputeTEnter } from './visibility.js'

/**
 * Convert official format time units to seconds
 * Official unit = 1.875 / BPM
 * Ported from official_impl.py:12-13
 */
export function officialUnitSec(bpm: number): number {
  return 1.875 / bpm
}

/**
 * Convert official time units to seconds
 */
export function uToSec(u: number, bpm: number): number {
  return u * officialUnitSec(bpm)
}

/**
 * Build scroll position track from speed events
 * Ported from official_impl.py:20-53
 *
 * For official format:
 * dF/dt = speed.value (where F is floorPosition in height-units)
 * Pixel scroll S(t) = F(t) * Uh_px
 */
export function buildOfficialScrollPx(
  speedEvents: any[],
  bpm: number,
  Uh_px: number
): IntegralTrack {
  const evs = [...speedEvents].sort((a, b) =>
    parseFloat(a.startTime || 0) - parseFloat(b.startTime || 0)
  )

  if (evs.length === 0) {
    return new IntegralTrack([])
  }

  const segs: Seg1D[] = []
  let prefix = 0.0

  for (const e of evs) {
    const t0 = uToSec(parseFloat(e.startTime), bpm)
    const t1 = uToSec(parseFloat(e.endTime), bpm)
    const v = parseFloat(e.value) * Uh_px  // (heightUnits/sec) * px/heightUnit => px/sec

    const seg: Seg1D = {
      t0,
      t1,
      v0: v,
      v1: v,
      prefix
    }

    const dt = Math.max(0.0, t1 - t0)
    prefix += 0.5 * (seg.v0 + seg.v1) * dt
    segs.push(seg)
  }

  // Extend to t=0 if needed
  if (segs.length > 0 && segs[0].t0 > 0) {
    const v0 = segs[0].v0
    segs.unshift({ t0: 0.0, t1: segs[0].t0, v0, v1: v0, prefix: 0.0 })

    // Rebuild prefix
    prefix = 0.0
    for (let i = 0; i < segs.length; i++) {
      const s = segs[i]
      segs[i] = { ...s, prefix }
      const dt = Math.max(0.0, s.t1 - s.t0)
      prefix += 0.5 * (s.v0 + s.v1) * dt
    }
  }

  return new IntegralTrack(segs)
}

/**
 * Build position tracks (X and Y) from move events
 * Ported from official_impl.py:56-83
 */
export function buildOfficialPosTracks(
  moveEvents: any[],
  bpm: number,
  fmt: number,
  W: number,
  H: number
): [PiecewiseEased, PiecewiseEased] {
  const evs = [...moveEvents].sort((a, b) =>
    parseFloat(a.startTime || 0) - parseFloat(b.startTime || 0)
  )

  if (evs.length === 0) {
    // Default center
    return [
      new PiecewiseEased([], W * 0.5),
      new PiecewiseEased([], H * 0.5)
    ]
  }

  if (fmt !== 3) {
    throw new Error(`Minimal renderer supports official formatVersion=3 only (got ${fmt})`)
  }

  const sx: EasedSeg[] = []
  const sy: EasedSeg[] = []

  for (const e of evs) {
    const t0 = uToSec(parseFloat(e.startTime), bpm)
    const t1 = uToSec(parseFloat(e.endTime), bpm)
    const x0 = parseFloat(e.start) * W
    const x1 = parseFloat(e.end) * W

    // Official format: bottom-left is (0,0), +Y is upward
    // Pygame/Web: top-left is (0,0), +Y is downward
    // So: y_screen = H * (1 - y_official)
    const y0 = H * (1.0 - parseFloat(e.start2))
    const y1 = H * (1.0 - parseFloat(e.end2))

    sx.push({ t0, t1, v0: x0, v1: x1, easing: ease_01, L: 0.0, R: 1.0 })
    sy.push({ t0, t1, v0: y0, v1: y1, easing: ease_01, L: 0.0, R: 1.0 })
  }

  // Extend to t=0
  if (sx.length > 0 && sx[0].t0 > 0) {
    sx.unshift({ t0: 0.0, t1: sx[0].t0, v0: sx[0].v0, v1: sx[0].v0, easing: ease_01, L: 0.0, R: 1.0 })
    sy.unshift({ t0: 0.0, t1: sy[0].t0, v0: sy[0].v0, v1: sy[0].v0, easing: ease_01, L: 0.0, R: 1.0 })
  }

  return [
    new PiecewiseEased(sx, W * 0.5),
    new PiecewiseEased(sy, H * 0.5)
  ]
}

/**
 * Build rotation track from rotate events
 * Ported from official_impl.py:86-100
 */
export function buildOfficialRotTrack(
  rotEvents: any[],
  bpm: number
): PiecewiseEased {
  const evs = [...rotEvents].sort((a, b) =>
    parseFloat(a.startTime || 0) - parseFloat(b.startTime || 0)
  )

  if (evs.length === 0) {
    return new PiecewiseEased([], 0.0)
  }

  const segs: EasedSeg[] = []

  for (const e of evs) {
    const t0 = uToSec(parseFloat(e.startTime), bpm)
    const t1 = uToSec(parseFloat(e.endTime), bpm)
    // Negate rotation and convert to radians
    const a0 = -parseFloat(e.start) * Math.PI / 180.0
    const a1 = -parseFloat(e.end) * Math.PI / 180.0

    segs.push({ t0, t1, v0: a0, v1: a1, easing: ease_01, L: 0.0, R: 1.0 })
  }

  // Extend to t=0
  if (segs.length > 0 && segs[0].t0 > 0) {
    segs.unshift({
      t0: 0.0,
      t1: segs[0].t0,
      v0: segs[0].v0,
      v1: segs[0].v0,
      easing: ease_01,
      L: 0.0,
      R: 1.0
    })
  }

  return new PiecewiseEased(segs, 0.0)
}

/**
 * Build alpha/opacity track from disappear events
 * Ported from official_impl.py:103-117
 */
export function buildOfficialAlphaTrack(
  dispEvents: any[],
  bpm: number
): PiecewiseEased {
  const evs = [...dispEvents].sort((a, b) =>
    parseFloat(a.startTime || 0) - parseFloat(b.startTime || 0)
  )

  if (evs.length === 0) {
    return new PiecewiseEased([], 1.0)
  }

  const segs: EasedSeg[] = []

  for (const e of evs) {
    const t0 = uToSec(parseFloat(e.startTime), bpm)
    const t1 = uToSec(parseFloat(e.endTime), bpm)
    const a0 = parseFloat(e.start)
    const a1 = parseFloat(e.end)

    segs.push({ t0, t1, v0: a0, v1: a1, easing: ease_01, L: 0.0, R: 1.0 })
  }

  // Extend to t=0
  if (segs.length > 0 && segs[0].t0 > 0) {
    segs.unshift({
      t0: 0.0,
      t1: segs[0].t0,
      v0: segs[0].v0,
      v1: segs[0].v0,
      easing: ease_01,
      L: 0.0,
      R: 1.0
    })
  }

  return new PiecewiseEased(segs, 1.0)
}

/**
 * Load official format chart
 * Ported from official_impl.py:120-204
 */
export function loadOfficial(
  data: any,
  W: number,
  H: number
): ParsedChart {
  const fmt = parseInt(data.formatVersion || 3)
  const offset = parseFloat(data.offset || 0.0)

  // Official format units
  const Uw = 0.05625 * W
  const Uh = 0.6 * H

  const linesOut: RuntimeLine[] = []
  const notesOut: RuntimeNote[] = []

  const jls = data.judgeLineList || []

  for (let i = 0; i < jls.length; i++) {
    const jl = jls[i]
    const bpm = parseFloat(jl.bpm || 120.0)

    // Build tracks
    const [px, py] = buildOfficialPosTracks(jl.judgeLineMoveEvents || [], bpm, fmt, W, H)
    const rot = buildOfficialRotTrack(jl.judgeLineRotateEvents || [], bpm)
    const alpha = buildOfficialAlphaTrack(jl.judgeLineDisappearEvents || [], bpm)
    const scroll = buildOfficialScrollPx(jl.speedEvents || [], bpm, Uh)

    // Color per line (rainbow based on index)
    const rgb = hsvToRgb((i / Math.max(1, jls.length)) % 1.0, 0.65, 0.95)

    const name = String(jl.name || '')
    const eventCounts = {
      move: (jl.judgeLineMoveEvents || []).length,
      rot: (jl.judgeLineRotateEvents || []).length,
      alpha: (jl.judgeLineDisappearEvents || []).length,
      speed: (jl.speedEvents || []).length,
    }

    linesOut.push({
      lid: i,
      pos_x: px,
      pos_y: py,
      rot,
      alpha,
      scroll_px: scroll,
      color_rgb: rgb as [number, number, number],
      name,
      event_counts: eventCounts,
      anchor: [0.5, 0.5],
      is_gif: false,
      father: -1,
      rotate_with_father: true,
    })

    // Parse notes
    const nidBase = i * 100000
    let nid = nidBase

    const addNote = (n: any, above: boolean) => {
      const kind = parseInt(n.type)
      const t_hit = uToSec(parseFloat(n.time), bpm)
      const holdU = parseFloat(n.holdTime || 0.0)
      const t_end = (kind === 3 && holdU > 0)
        ? t_hit + uToSec(holdU, bpm)
        : t_hit

      const note: RuntimeNote = {
        nid: nid++,
        line_id: i,
        kind,
        above,
        fake: false,
        t_hit,
        t_end,
        x_local_px: parseFloat(n.positionX || 0.0) * Uw,
        y_offset_px: 0.0,
        speed_mul: parseFloat(n.speed || 1.0),
        size_px: 1.0,
        alpha01: 1.0,
        tint_rgb: [255, 255, 255],
        tint_hitfx_rgb: null,
        scroll_hit: 0.0,
        scroll_end: 0.0,
        hitsound_path: null,
        t_enter: -1e9,
        mh: false,
      }

      notesOut.push(note)
    }

    // Y-axis is flipped for official format, so above/below semantics are reversed
    for (const n of jl.notesAbove || []) {
      addNote(n, false)
    }
    for (const n of jl.notesBelow || []) {
      addNote(n, true)
    }
  }

  // Cache scroll samples
  const lineMap = new Map(linesOut.map(ln => [ln.lid, ln]))

  for (const n of notesOut) {
    const ln = lineMap.get(n.line_id)!
    n.scroll_hit = (ln.scroll_px as IntegralTrack).integral(n.t_hit)

    if (n.kind === 3 && n.t_end > n.t_hit) {
      try {
        const dur = Math.max(0.0, n.t_end - n.t_hit)
        const sp = Math.max(0.0, n.speed_mul)
        n.scroll_end = n.scroll_hit + sp * dur * Uh
        n.speed_mul = 1.0
      } catch {
        n.scroll_end = (ln.scroll_px as IntegralTrack).integral(n.t_end)
      }
    } else {
      n.scroll_end = (ln.scroll_px as IntegralTrack).integral(n.t_end)
    }
  }

  // Precompute note enter times (align with pygame visibility.py)
  precomputeTEnter(linesOut, notesOut, W, H)

  // Sort notes by hit time
  notesOut.sort((a, b) => a.t_hit - b.t_hit)

  // Calculate duration
  const maxTime = Math.max(
    ...notesOut.map(n => n.t_end),
    0
  )

  return {
    lines: linesOut,
    notes: notesOut,
    duration: maxTime,
    bpm: parseFloat(jls[0]?.bpm || 120),
    offset,
    metadata: {
      format: 'official',
    }
  }
}

