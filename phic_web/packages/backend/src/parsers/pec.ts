/**
 * PEC (Phigros Editor Chart) format parser
 * Ported from phic_renderer/formats/pec_impl.py
 */

import type { ParsedChart, RuntimeLine, RuntimeNote } from '@phic-web/shared'
import {
  PiecewiseEased,
  IntegralTrack,
  type EasedSeg,
  type Seg1D,
  clamp,
  easingFromType,
} from '@phic-web/shared'
import { precomputeTEnter } from './visibility.js'

/**
 * BPM segment for beat-to-second conversion
 */
interface BpmSeg {
  beat0: number
  bpm: number
  sec_prefix: number
}

/**
 * BPM mapping for PEC format
 */
class BpmMap {
  private segs: BpmSeg[]

  constructor(segs: BpmSeg[]) {
    this.segs = segs
  }

  static build(items: Array<[number, number]>): BpmMap {
    const arr = items.map(([b, v]) => [Number(b), Number(v)] as [number, number])
    arr.sort((a, b) => a[0] - b[0])
    const segs: BpmSeg[] = []
    let sec_prefix = 0.0
    for (let i = 0; i < arr.length; i++) {
      const [b0, bpm] = arr[i]
      segs.push({ beat0: b0, bpm, sec_prefix })
      if (i + 1 < arr.length) {
        const b1 = arr[i + 1][0]
        sec_prefix += (b1 - b0) * 60.0 / Math.max(1e-9, bpm)
      }
    }
    return new BpmMap(segs)
  }

  beatToSec(beat: number): number {
    if (!this.segs.length) return 0.0
    const segs = this.segs
    let lo = 0
    let hi = segs.length
    while (lo + 1 < hi) {
      const mid = Math.floor((lo + hi) / 2)
      if (segs[mid].beat0 <= beat) {
        lo = mid
      } else {
        hi = mid
      }
    }
    const s = segs[lo]
    return s.sec_prefix + (beat - s.beat0) * 60.0 / Math.max(1e-9, s.bpm)
  }
}

/**
 * Convert PEC X coordinate to pixels
 */
function pecXToPx(x: number, W: number): number {
  // PEC coordinate system (center-origin):
  // center = (0,0), left=-1024, right=1024
  // Some packs store absolute coordinates in [0,2048]; auto-detect and shift
  let fx = Number(x)
  if (fx >= 1024.0 || fx <= -1024.0) {
    fx = fx - 1024.0
  }
  const sx = W / 2048.0
  return (fx + 1024.0) * sx
}

/**
 * Convert PEC Y coordinate to pixels
 */
function pecYToPx(y: number, H: number): number {
  // PEC coordinate system (center-origin):
  // center = (0,0), bottom=-700, top=700
  // Some packs store absolute coordinates in [0,1400]; auto-detect and shift
  let fy = Number(y)
  if (fy >= 700.0 || fy <= -700.0) {
    fy = fy - 700.0
  }
  const sy = H / 1400.0
  return (H * 0.5) - fy * sy
}

/**
 * Load PEC format chart from text
 */
export function loadPECText(text: string, W: number, H: number): ParsedChart {
  const rawLines = text.split('\n').map(ln => ln.trim())
  const filteredLines = rawLines.filter(ln => ln && !ln.startsWith('//'))

  if (!filteredLines.length) {
    return {
      lines: [],
      notes: [],
      duration: 0,
      bpm: 120,
      offset: 0,
      metadata: { format: 'pec' },
    }
  }

  // First line is offset in milliseconds
  let offsetMs: number
  try {
    offsetMs = parseInt(filteredLines[0].trim())
  } catch {
    offsetMs = 0
  }
  const offset = offsetMs / 1000.0

  // Parse BPM items (lines starting with "bp ")
  const bpmItems: Array<[number, number]> = []
  for (const ln of filteredLines.slice(1)) {
    if (ln.startsWith('bp ')) {
      const parts = ln.split(/\s+/)
      if (parts.length >= 3) {
        try {
          bpmItems.push([Number(parts[1]), Number(parts[2])])
        } catch {}
      }
    }
  }

  if (!bpmItems.length) {
    bpmItems.push([0.0, 120.0])
  }
  const bpmMap = BpmMap.build(bpmItems)

  // Find max line ID
  let maxLine = -1
  const notesCmds: Array<[string, string[]]> = []
  const evCmds: Array<[string, string[]]> = []

  for (const ln of filteredLines.slice(1)) {
    if (!ln) continue
    const parts = ln.split(/\s+/)
    if (!parts.length) continue
    const head = parts[0]

    if (head.startsWith('n') || head === '#' || head === '&') {
      notesCmds.push([head, parts.slice(1)])
      continue
    }
    evCmds.push([head, parts.slice(1)])
  }

  for (const [head, parts] of evCmds) {
    try {
      if (['cv', 'cp', 'cd', 'ca', 'cm', 'cr', 'cf'].includes(head)) {
        if (parts.length) {
          maxLine = Math.max(maxLine, parseInt(parts[0]))
        }
      }
    } catch {}
  }
  for (const [head, parts] of notesCmds) {
    try {
      if (head.startsWith('n') && parts.length) {
        maxLine = Math.max(maxLine, parseInt(parts[0]))
      }
    } catch {}
  }

  let lineCount = Math.max(0, maxLine + 1)
  if (lineCount > 30) lineCount = 30

  const pxPerUnitPerSec = 120.0 * (H / 900.0)

  /**
   * Build tracks for a single judgment line
   */
  const buildTracksForLine = (lid: number): {
    px: PiecewiseEased
    py: PiecewiseEased
    pr: PiecewiseEased
    pa: PiecewiseEased
    scroll: IntegralTrack
  } => {
    let curX = 0.0
    let curY = 0.0
    let curRot = 0.0
    let curAlpha = 255.0
    let curSpeed = 1.0

    const xSegs: EasedSeg[] = []
    const ySegs: EasedSeg[] = []
    const rSegs: EasedSeg[] = []
    const aSegs: EasedSeg[] = []
    const speedKeys: Array<[number, number]> = []

    let tCur = 0.0

    const emitConst = (t0: number, t1: number) => {
      if (t1 <= t0 + 1e-9) return
      const ease = easingFromType(0)
      xSegs.push({ t0, t1, v0: curX, v1: curX, easing: ease, L: 0, R: 1 })
      ySegs.push({ t0, t1, v0: curY, v1: curY, easing: ease, L: 0, R: 1 })
      rSegs.push({ t0, t1, v0: curRot, v1: curRot, easing: ease, L: 0, R: 1 })
      aSegs.push({ t0, t1, v0: curAlpha, v1: curAlpha, easing: ease, L: 0, R: 1 })
    }

    // Collect and sort events for this line
    const events: Array<[number, string, string[]]> = []
    for (const [h, p] of evCmds) {
      if (!p.length) continue
      try {
        if (parseInt(p[0]) !== lid) continue
      } catch {
        continue
      }
      let bt: number | null = null
      try {
        if (['cv', 'cp', 'cd', 'ca'].includes(h) && p.length >= 2) {
          bt = Number(p[1])
        } else if (['cm', 'cr', 'cf'].includes(h) && p.length >= 2) {
          bt = Number(p[1])
        }
      } catch {
        bt = null
      }
      if (bt === null) continue
      events.push([bpmMap.beatToSec(bt), h, p])
    }
    events.sort((a, b) => a[0] - b[0])

    // Process events
    for (const [t0, h, p] of events) {
      if (t0 > tCur) {
        emitConst(tCur, t0)
        tCur = t0
      }

      // cp: set position instantly
      if (h === 'cp' && p.length >= 4) {
        try {
          curX = Number(p[2])
          curY = Number(p[3])
        } catch {}
        continue
      }

      // cd: set rotation instantly
      if (h === 'cd' && p.length >= 3) {
        try {
          curRot = Number(p[2])
        } catch {}
        continue
      }

      // ca: set alpha instantly
      if (h === 'ca' && p.length >= 3) {
        try {
          let v = Number(p[2])
          if (v < 0) v = 0.0
          curAlpha = clamp(v, 0.0, 255.0)
        } catch {}
        continue
      }

      // cv: set speed
      if (h === 'cv' && p.length >= 3) {
        try {
          curSpeed = Number(p[2])
        } catch {}
        speedKeys.push([t0, curSpeed])
        continue
      }

      // cm: move transition
      if (h === 'cm' && p.length >= 6) {
        try {
          const t1 = bpmMap.beatToSec(Number(p[2]))
          const x1 = Number(p[3])
          const y1 = Number(p[4])
          const et = parseInt(p[5])

          if (t1 > t0 + 1e-9) {
            const ease = easingFromType(et)
            xSegs.push({ t0, t1, v0: curX, v1: x1, easing: ease, L: 0, R: 1 })
            ySegs.push({ t0, t1, v0: curY, v1: y1, easing: ease, L: 0, R: 1 })
            rSegs.push({ t0, t1, v0: curRot, v1: curRot, easing: easingFromType(0), L: 0, R: 1 })
            aSegs.push({ t0, t1, v0: curAlpha, v1: curAlpha, easing: easingFromType(0), L: 0, R: 1 })
            curX = x1
            curY = y1
            tCur = t1
          }
        } catch {}
        continue
      }

      // cr: rotate transition
      if (h === 'cr' && p.length >= 5) {
        try {
          const t1 = bpmMap.beatToSec(Number(p[2]))
          const r1 = Number(p[3])
          const et = parseInt(p[4])

          if (t1 > t0 + 1e-9) {
            const ease = easingFromType(et)
            rSegs.push({ t0, t1, v0: curRot, v1: r1, easing: ease, L: 0, R: 1 })
            xSegs.push({ t0, t1, v0: curX, v1: curX, easing: easingFromType(0), L: 0, R: 1 })
            ySegs.push({ t0, t1, v0: curY, v1: curY, easing: easingFromType(0), L: 0, R: 1 })
            aSegs.push({ t0, t1, v0: curAlpha, v1: curAlpha, easing: easingFromType(0), L: 0, R: 1 })
            curRot = r1
            tCur = t1
          }
        } catch {}
        continue
      }

      // cf: fade transition
      if (h === 'cf' && p.length >= 4) {
        try {
          const t1 = bpmMap.beatToSec(Number(p[2]))
          let a1 = Number(p[3])
          const et = p.length >= 5 ? parseInt(p[4]) : 0

          if (a1 < 0) a1 = 0.0
          a1 = clamp(a1, 0.0, 255.0)

          if (t1 > t0 + 1e-9) {
            const ease = easingFromType(et)
            aSegs.push({ t0, t1, v0: curAlpha, v1: a1, easing: ease, L: 0, R: 1 })
            xSegs.push({ t0, t1, v0: curX, v1: curX, easing: easingFromType(0), L: 0, R: 1 })
            ySegs.push({ t0, t1, v0: curY, v1: curY, easing: easingFromType(0), L: 0, R: 1 })
            rSegs.push({ t0, t1, v0: curRot, v1: curRot, easing: easingFromType(0), L: 0, R: 1 })
            curAlpha = a1
            tCur = t1
          }
        } catch {}
        continue
      }
    }

    // Find end time hint from notes
    let endHint = 0.0
    for (const [head, parts] of notesCmds) {
      if (head.startsWith('n') && parts.length) {
        try {
          if (parseInt(parts[0]) !== lid) continue
        } catch {
          continue
        }
        try {
          if (head === 'n2' && parts.length >= 2) {
            endHint = Math.max(endHint, bpmMap.beatToSec(Number(parts[2])))
          } else if (parts.length >= 2) {
            endHint = Math.max(endHint, bpmMap.beatToSec(Number(parts[1])))
          }
        } catch {}
      }
    }
    const endTime = Math.max(endHint + 5.0, tCur + 2.0)
    emitConst(tCur, endTime)

    const px = new PiecewiseEased(xSegs, 0.0)
    const py = new PiecewiseEased(ySegs, 0.0)
    const pr = new PiecewiseEased(rSegs, 0.0)
    const pa = new PiecewiseEased(aSegs, 255.0)

    // Build scroll track
    if (!speedKeys.length) {
      speedKeys.push([0.0, curSpeed])
    }
    speedKeys.sort((a, b) => a[0] - b[0])
    const cuts = Array.from(new Set([0.0, ...speedKeys.map(([t]) => t), endTime])).sort((a, b) => a - b)
    const segs: Seg1D[] = []
    let prefix = 0.0
    for (let i = 0; i < cuts.length - 1; i++) {
      const t0 = cuts[i]
      const t1 = cuts[i + 1]
      if (t1 <= t0) continue
      let v = speedKeys[0][1]
      for (const [tt, vv] of speedKeys) {
        if (tt <= t0 + 1e-9) {
          v = vv
        } else {
          break
        }
      }
      const vpx = v * pxPerUnitPerSec
      segs.push({ t0, t1, v0: vpx, v1: vpx, prefix })
      prefix += vpx * (t1 - t0)
    }
    const scroll = new IntegralTrack(segs)

    return { px, py, pr, pa, scroll }
  }

  const tracksByLine = Array.from({ length: lineCount }, (_, i) => buildTracksForLine(i))

  // Build RuntimeLines
  const linesOut: RuntimeLine[] = []
  for (let lid = 0; lid < lineCount; lid++) {
    const { px, py, pr, pa, scroll } = tracksByLine[lid]
    const posX = (t: number) => pecXToPx(px.eval(t), W)
    const posY = (t: number) => pecYToPx(py.eval(t), H)
    const rot = (t: number) => pr.eval(t) * Math.PI / 180.0
    const alpha01 = (t: number) => {
      const v = pa.eval(t)
      if (v <= 1.000001) return clamp(v, 0.0, 1.0)
      return clamp(v / 255.0, 0.0, 1.0)
    }

    linesOut.push({
      lid,
      pos_x: posX,
      pos_y: posY,
      rot,
      alpha: alpha01,
      scroll_px: scroll,
      color_rgb: [255, 255, 255],
      color: null,
      scale_x: null,
      scale_y: null,
      text: null,
      texture_path: null,
      anchor: [0.5, 0.5],
      is_gif: false,
      gif_progress: null,
      father: -1,
      rotate_with_father: true,
      name: '',
      event_counts: {},
    })
  }

  // Parse notes
  const notesOut: RuntimeNote[] = []
  let nid = 0
  let pendingNote: any = null

  for (const [head, parts] of notesCmds) {
    if (head.startsWith('n')) {
      pendingNote = null
      if (!parts.length) continue

      const tpStr = head.slice(1)
      let tp: number
      try {
        tp = parseInt(tpStr)
      } catch {
        continue
      }
      if (![1, 2, 3, 4].includes(tp)) continue

      let lid: number
      try {
        lid = parseInt(parts[0])
      } catch {
        continue
      }
      if (lid < 0 || lid >= lineCount) continue

      try {
        let b0: number, b1: number, x: number, direction: number, fake: boolean

        if (tp === 2) {
          // Hold note
          b0 = Number(parts[1])
          b1 = Number(parts[2])
          x = Number(parts[3])
          direction = parseInt(parts[4])
          fake = parseInt(parts[5]) === 1
        } else {
          // Other note types
          b0 = Number(parts[1])
          b1 = b0
          x = Number(parts[2])
          direction = parseInt(parts[3])
          fake = parseInt(parts[4]) === 1
        }

        const tHit = bpmMap.beatToSec(b0)
        const tEnd = bpmMap.beatToSec(b1)
        const above = direction === 1

        pendingNote = {
          line_id: lid,
          kind: tp === 2 ? 3 : tp, // 2=hold -> kind=3
          t_hit: tHit,
          t_end: tp === 2 ? tEnd : tHit,
          x_local_px: x * (W / 2048.0),
          above,
          fake,
          speed_mul: 1.0,
          size_px: 1.0,
        }
      } catch {}
      continue
    }

    // # modifier: speed multiplier
    if (head === '#' && pendingNote !== null) {
      try {
        if (parts.length) {
          pendingNote.speed_mul = Number(parts[0])
        }
      } catch {}
      continue
    }

    // & modifier: size + finalize note
    if (head === '&' && pendingNote !== null) {
      try {
        if (parts.length) {
          pendingNote.size_px = Number(parts[0])
        }
      } catch {}

      // Calculate scroll positions
      const ln = linesOut[pendingNote.line_id]
      const scrollHit = (ln.scroll_px as IntegralTrack).integral(pendingNote.t_hit)
      const scrollEnd = pendingNote.kind === 3 ? (ln.scroll_px as IntegralTrack).integral(pendingNote.t_end) : scrollHit

      const note: RuntimeNote = {
        nid,
        line_id: pendingNote.line_id,
        kind: pendingNote.kind,
        above: pendingNote.above,
        fake: pendingNote.fake,
        t_hit: pendingNote.t_hit,
        t_end: pendingNote.t_end,
        x_local_px: pendingNote.x_local_px,
        y_offset_px: 0.0,
        speed_mul: pendingNote.speed_mul,
        size_px: pendingNote.size_px,
        alpha01: 1.0,
        tint_rgb: [255, 255, 255],
        tint_hitfx_rgb: null,
        scroll_hit: scrollHit,
        scroll_end: scrollEnd,
        hitsound_path: null,
        t_enter: 0,
        mh: false,
      }
      notesOut.push(note)
      nid++
      pendingNote = null
    }
  }

  // Sort notes by hit time
  notesOut.sort((a, b) => a.t_hit - b.t_hit)

  // Precompute note enter times (align with pygame visibility.py)
  precomputeTEnter(linesOut, notesOut, W, H)

  // Calculate duration
  const maxNoteTime = notesOut.length > 0 ? Math.max(...notesOut.map(n => n.t_end)) : 0
  const duration = maxNoteTime + 2.0

  return {
    lines: linesOut,
    notes: notesOut,
    duration,
    bpm: 120,
    offset,
    metadata: {
      format: 'pec',
    },
  }
}

/**
 * Load PEC format chart from data (expects text string)
 */
export function loadPEC(
  data: any,
  W: number,
  H: number
): ParsedChart {
  if (typeof data === 'string') {
    return loadPECText(data, W, H)
  }
  throw new Error('PEC format expects text string data')
}

/**
 * Detect if data is PEC format
 */
export function isPECFormat(data: any): boolean {
  // PEC is text-based, check for typical PEC commands
  if (typeof data !== 'string') return false
  const lines = data.split('\n').filter((ln: string) => ln.trim() && !ln.startsWith('//'))
  if (!lines.length) return false

  // Check for PEC-specific commands (bp, cv, cp, cd, ca, cm, cr, cf)
  const hasBp = lines.some((ln: string) => ln.trim().startsWith('bp '))
  const hasNotes = lines.some((ln: string) => /^n[1-4]\s/.test(ln.trim()))

  return hasBp || hasNotes
}
