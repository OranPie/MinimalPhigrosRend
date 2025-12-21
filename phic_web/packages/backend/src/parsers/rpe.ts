/**
 * RPE (RePhiEdit/Rhythm Plus Editor) format parser
 * Ported from phic_renderer/formats/rpe_impl.py
 */

import type { ParsedChart, RuntimeLine, RuntimeNote } from '@phic-web/shared'
import {
  PiecewiseEased,
  IntegralTrack,
  SumTrack,
  PiecewiseColor,
  PiecewiseText,
  type EasedSeg,
  type ColorSeg,
  type TextSeg,
  type Seg1D,
  clamp,
  lerp,
  hsvToRgb,
  easingFromType,
  cubicBezierYForX,
  rpeEasingShift,
} from '@phic-web/shared'

import { precomputeTEnter } from './visibility.js'

/**
 * Convert beat notation to float value
 * Supports [a,b,c], {bar, num, den}, or numeric values
 */
function beatToValue(b: any): number {
  if (Array.isArray(b) && b.length === 3) {
    const [a, n, d] = b
    return Number(a) + Number(n) / Number(d)
  }
  if (typeof b === 'object' && b !== null && 'bar' in b && 'num' in b && 'den' in b) {
    return Number(b.bar) + Number(b.num) / Number(b.den)
  }
  return Number(b)
}

/**
 * BPM segment for beat-to-second conversion
 */
interface BpmSeg {
  beat0: number
  bpm: number
  sec_prefix: number
}

/**
 * BPM mapping system for converting beats to seconds
 */
class BpmMap {
  private segs: BpmSeg[]

  constructor(segs: BpmSeg[]) {
    this.segs = segs
  }

  static build(bpmList: any[]): BpmMap {
    const items: Array<[number, number]> = []
    for (const e of bpmList) {
      const b0 = beatToValue(e.startTime)
      const bpm = Number(e.bpm)
      items.push([b0, bpm])
    }
    items.sort((a, b) => a[0] - b[0])

    const segs: BpmSeg[] = []
    let sec_prefix = 0.0
    for (let i = 0; i < items.length; i++) {
      const [b0, bpm] = items[i]
      segs.push({ beat0: b0, bpm, sec_prefix })
      if (i + 1 < items.length) {
        const b1 = items[i + 1][0]
        sec_prefix += (b1 - b0) * 60.0 / bpm
      }
    }
    return new BpmMap(segs)
  }

  beatToSec(beatVal: number, bpmfactor: number = 1.0): number {
    if (!this.segs.length) return 0.0

    // Binary search for last seg with beat0 <= beatVal
    let lo = 0
    let hi = this.segs.length
    while (lo + 1 < hi) {
      const mid = Math.floor((lo + hi) / 2)
      if (this.segs[mid].beat0 <= beatVal) {
        lo = mid
      } else {
        hi = mid
      }
    }
    const s = this.segs[lo]
    return (s.sec_prefix + (beatVal - s.beat0) * 60.0 / s.bpm) * bpmfactor
  }
}

/**
 * Build eased track from RPE events
 */
function buildRpeEasedTrack(
  events: any[],
  bpmMap: BpmMap,
  bpmfactor: number,
  defaultValue: number = 0.0
): PiecewiseEased {
  const evs = Array.from(events || [])
  if (!evs.length) {
    return new PiecewiseEased([], defaultValue)
  }
  evs.sort((a, b) => beatToValue(a.startTime) - beatToValue(b.startTime))

  const segs: EasedSeg[] = []
  for (const e of evs) {
    const b0 = beatToValue(e.startTime)
    const b1 = beatToValue(e.endTime)
    const t0 = bpmMap.beatToSec(b0, bpmfactor)
    const t1 = bpmMap.beatToSec(b1, bpmfactor)
    const v0 = Number(e.start ?? 0.0)
    const v1 = Number(e.end ?? 0.0)

    const L = Number(e.easingLeft ?? 0.0) || 0.0
    const R = Number(e.easingRight ?? 1.0) || 1.0

    const bez = Number(e.bezier ?? 0) || 0
    let easing_f: (p: number) => number

    if (bez === 1 && Array.isArray(e.bezierPoints) && e.bezierPoints.length === 4) {
      const [x1, y1, x2, y2] = e.bezierPoints.map(Number)
      easing_f = (p: number) => cubicBezierYForX(x1, y1, x2, y2, p)
    } else {
      const tp = (Number(e.easingType ?? 0) || 0) + rpeEasingShift
      easing_f = easingFromType(tp)
    }

    segs.push({ t0, t1, v0, v1, easing: easing_f, L, R })
  }

  // Add initial constant segment if first segment doesn't start at 0
  if (segs.length && segs[0].t0 > 0) {
    const ease_01 = easingFromType(1) // linear
    segs.unshift({ t0: 0.0, t1: segs[0].t0, v0: segs[0].v0, v1: segs[0].v0, easing: ease_01, L: 0, R: 1 })
  }

  return new PiecewiseEased(segs, defaultValue)
}

/**
 * Parse RGB color from RPE data
 */
function parseRgb3(v: any): [number, number, number] {
  if (Array.isArray(v) && v.length >= 3) {
    try {
      const r = Math.floor(Number(v[0]))
      const g = Math.floor(Number(v[1]))
      const b = Math.floor(Number(v[2]))
      return [clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255)]
    } catch {
      return [255, 255, 255]
    }
  }
  return [255, 255, 255]
}

/**
 * Parse optional RGB color
 */
function parseRgb3Opt(v: any): [number, number, number] | null {
  if (v == null) return null
  try {
    return parseRgb3(v)
  } catch {
    return null
  }
}

/**
 * Build color track from RPE events
 */
function buildRpeColorTrack(
  events: any[],
  bpmMap: BpmMap,
  bpmfactor: number,
  defaultColor: [number, number, number]
): PiecewiseColor {
  const evs = Array.from(events || [])
  if (!evs.length) {
    return new PiecewiseColor([], defaultColor)
  }
  evs.sort((a, b) => beatToValue(a.startTime) - beatToValue(b.startTime))

  const segs: ColorSeg[] = []
  for (const e of evs) {
    const b0 = beatToValue(e.startTime)
    const b1 = beatToValue(e.endTime)
    const t0 = bpmMap.beatToSec(b0, bpmfactor)
    const t1 = bpmMap.beatToSec(b1, bpmfactor)
    const c0 = parseRgb3(e.start ?? defaultColor)
    const c1 = parseRgb3(e.end ?? c0)

    const L = Number(e.easingLeft ?? 0.0) || 0.0
    const R = Number(e.easingRight ?? 1.0) || 1.0

    const bez = Number(e.bezier ?? 0) || 0
    let easing_f: (p: number) => number

    if (bez === 1 && Array.isArray(e.bezierPoints) && e.bezierPoints.length === 4) {
      const [x1, y1, x2, y2] = e.bezierPoints.map(Number)
      easing_f = (p: number) => cubicBezierYForX(x1, y1, x2, y2, p)
    } else {
      const tp = (Number(e.easingType ?? 0) || 0) + rpeEasingShift
      easing_f = easingFromType(tp)
    }

    segs.push({ t0, t1, c0, c1, easing: easing_f, L, R })
  }

  return new PiecewiseColor(segs, defaultColor)
}

/**
 * Build text track from RPE events
 */
function buildRpeTextTrack(
  events: any[],
  bpmMap: BpmMap,
  bpmfactor: number
): PiecewiseText {
  const evs = Array.from(events || [])
  if (!evs.length) {
    return new PiecewiseText([], '')
  }
  evs.sort((a, b) => beatToValue(a.startTime) - beatToValue(b.startTime))

  const segs: TextSeg[] = []
  for (const e of evs) {
    const b0 = beatToValue(e.startTime)
    const b1 = beatToValue(e.endTime)
    const t0 = bpmMap.beatToSec(b0, bpmfactor)
    const t1 = bpmMap.beatToSec(b1, bpmfactor)
    const s0 = String(e.start ?? '')
    const s1 = String(e.end ?? s0)
    segs.push({ t0, t1, s0, s1 })
  }

  return new PiecewiseText(segs, '')
}

/**
 * Build scroll track from RPE speed events (layered)
 */
function buildRpeScrollPx(
  speedEventsLayers: any[][],
  bpmMap: BpmMap,
  bpmfactor: number,
  pxPerUnitPerSec: number
): IntegralTrack {
  // Collect all events from all layers
  const allEvs: any[] = []
  for (const layer of speedEventsLayers) {
    for (const e of (layer || [])) {
      allEvs.push(e)
    }
  }
  if (!allEvs.length) {
    return new IntegralTrack([])
  }

  // Build segments by cutting at all boundaries
  const cuts = new Set<number>([0.0])
  for (const e of allEvs) {
    const b0 = beatToValue(e.startTime)
    const b1 = beatToValue(e.endTime)
    cuts.add(bpmMap.beatToSec(b0, bpmfactor))
    cuts.add(bpmMap.beatToSec(b1, bpmfactor))
  }
  let cutList = Array.from(cuts).sort((a, b) => a - b)
  if (!cutList.length) cutList = [0.0]
  if (cutList.length === 1) {
    cutList.push(cutList[0] + 1e6)
  } else {
    cutList.push(cutList[cutList.length - 1] + 1e6)
  }

  const sampleLayerValue = (layerEvents: any[], tMid: number): number => {
    const evs = Array.from(layerEvents || [])
    if (!evs.length) return 0.0

    evs.sort((a, b) => {
      const t0a = bpmMap.beatToSec(beatToValue(a.startTime), bpmfactor)
      const t0b = bpmMap.beatToSec(beatToValue(b.startTime), bpmfactor)
      return t0a - t0b
    })

    let val = 0.0
    let anyCover = false
    let lastBefore: any = null

    for (const e of evs) {
      const t0 = bpmMap.beatToSec(beatToValue(e.startTime), bpmfactor)
      const t1 = bpmMap.beatToSec(beatToValue(e.endTime), bpmfactor)
      if (tMid < t0) break
      lastBefore = e
      if (tMid >= t0 && tMid < t1) {
        const s0 = Number(e.start ?? 0.0)
        const s1 = Number(e.end ?? s0)
        const u = (tMid - t0) / Math.max(1e-9, t1 - t0)
        val += lerp(s0, s1, clamp(u, 0, 1))
        anyCover = true
      }
    }

    if (anyCover) return val

    // Hold most recent value
    if (lastBefore !== null) {
      const s0 = Number(lastBefore.start ?? 0.0)
      const s1 = Number(lastBefore.end ?? s0)
      return s1
    }
    const first = evs[0]
    return Number(first.start ?? 0.0)
  }

  const segs: Seg1D[] = []
  let prefix = 0.0
  for (let i = 0; i < cutList.length - 1; i++) {
    const t0 = cutList[i]
    const t1 = cutList[i + 1]
    if (t1 <= t0) continue
    const tm = (t0 + t1) * 0.5
    let vUnit = 0.0
    for (const layer of speedEventsLayers) {
      vUnit += sampleLayerValue(layer, tm)
    }
    const v = vUnit * pxPerUnitPerSec
    segs.push({ t0, t1, v0: v, v1: v, prefix })
    prefix += v * (t1 - t0)
  }

  return new IntegralTrack(segs)
}

/**
 * Load RPE format chart
 */
export function loadRPE(
  data: any,
  W: number,
  H: number
): ParsedChart {
  const meta = data.META ?? {}
  const offsetMs = Number(meta.offset ?? 0.0)
  const offset = offsetMs / 1000.0

  const bpmMap = BpmMap.build(data.BPMList ?? [])
  const jls = data.judgeLineList ?? []

  const sx = W / 1350.0
  const sy = H / 900.0

  // Speed unit -> px/s scaling (1.0 speed ≈ 120 px/s in base 900p)
  const pxPerUnitPerSec = 120.0 * sy

  const linesOut: RuntimeLine[] = []
  const notesOut: RuntimeNote[] = []

  const fathers: number[] = []
  const rotWithFathers: boolean[] = []

  for (let i = 0; i < jls.length; i++) {
    const jl = jls[i]
    const bpmfactor = Number(jl.bpmfactor ?? 1.0) || 1.0

    const layers = jl.eventLayers ?? []
    const moveXTracks: PiecewiseEased[] = []
    const moveYTracks: PiecewiseEased[] = []
    const rotTracks: PiecewiseEased[] = []
    const alphaTracks: PiecewiseEased[] = []
    let speedLayers: any[][] = []

    for (const layer of layers) {
      if (!layer) continue
      moveXTracks.push(buildRpeEasedTrack(layer.moveXEvents ?? [], bpmMap, bpmfactor, 0.0))
      moveYTracks.push(buildRpeEasedTrack(layer.moveYEvents ?? [], bpmMap, bpmfactor, 0.0))
      rotTracks.push(buildRpeEasedTrack(layer.rotateEvents ?? [], bpmMap, bpmfactor, 0.0))
      alphaTracks.push(buildRpeEasedTrack(layer.alphaEvents ?? [], bpmMap, bpmfactor, 255.0))
      speedLayers.push(layer.speedEvents ?? [])
    }

    // Compatibility: some charts store speed at judgeLine level
    if (!speedLayers.length || speedLayers.every(ly => !(ly ?? []).length)) {
      const jlSpeed = jl.speedEvents
      if (Array.isArray(jlSpeed) && jlSpeed.length) {
        speedLayers = [jlSpeed]
      }
    }

    const evc = {
      moveX: layers.reduce((sum: number, ly: any) => sum + (ly?.moveXEvents?.length ?? 0), 0),
      moveY: layers.reduce((sum: number, ly: any) => sum + (ly?.moveYEvents?.length ?? 0), 0),
      rot: layers.reduce((sum: number, ly: any) => sum + (ly?.rotateEvents?.length ?? 0), 0),
      alpha: layers.reduce((sum: number, ly: any) => sum + (ly?.alphaEvents?.length ?? 0), 0),
      speed: layers.reduce((sum: number, ly: any) => sum + (ly?.speedEvents?.length ?? 0), 0),
    }
    const name = String(jl.name ?? '')

    const moveX = new SumTrack(moveXTracks, 0.0)
    const moveY = new SumTrack(moveYTracks, 0.0)
    const rotDeg = new SumTrack(rotTracks, 0.0)
    const alphaRaw = new SumTrack(alphaTracks, 255.0)

    // Convert RPE coords -> pixel center
    // x_px = (x+675)/1350 * W == (x+675)*sx
    // y_px = 1-(y+450)/900 * H == (450 - y)*sy
    const posX = (t: number) => (moveX.eval(t) + 675.0) * sx
    const posY = (t: number) => (450.0 - moveY.eval(t)) * sy
    const rot = (t: number) => (rotDeg.eval(t) * Math.PI / 180.0)
    const alpha01 = (t: number) => {
      const v = alphaRaw.eval(t)
      if (v <= 1.000001) return clamp(v, 0.0, 1.0)
      return clamp(v / 255.0, 0.0, 1.0)
    }

    const scroll = buildRpeScrollPx(speedLayers, bpmMap, bpmfactor, pxPerUnitPerSec)

    const rgb = hsvToRgb((i / Math.max(1, jls.length)) % 1.0, 0.65, 0.95)

    const ext = jl.extended ?? {}
    let colorTrack: PiecewiseColor | null = null
    try {
      const ce = ext.colorEvents
      if (Array.isArray(ce) && ce.length) {
        colorTrack = buildRpeColorTrack(ce, bpmMap, bpmfactor, rgb)
      }
    } catch {}

    let scaleXTrack: PiecewiseEased | null = null
    let scaleYTrack: PiecewiseEased | null = null
    try {
      const sxev = ext.scaleXEvents
      if (Array.isArray(sxev) && sxev.length) {
        scaleXTrack = buildRpeEasedTrack(sxev, bpmMap, bpmfactor, 1.0)
      }
    } catch {}
    try {
      const syev = ext.scaleYEvents
      if (Array.isArray(syev) && syev.length) {
        scaleYTrack = buildRpeEasedTrack(syev, bpmMap, bpmfactor, 1.0)
      }
    } catch {}

    let textTrack: PiecewiseText | null = null
    try {
      const tev = ext.textEvents
      if (Array.isArray(tev) && tev.length) {
        textTrack = buildRpeTextTrack(tev, bpmMap, bpmfactor)
      }
    } catch {}

    let gifTrack: PiecewiseEased | null = null
    try {
      const gev = ext.gifEvents
      if (Array.isArray(gev) && gev.length) {
        gifTrack = buildRpeEasedTrack(gev, bpmMap, bpmfactor, 0.0)
      }
    } catch {}

    let texPath: string | null = null
    try {
      const tp = jl.Texture
      if (tp != null) {
        const tpStr = String(tp)
        if (tpStr && tpStr !== 'line.png') {
          texPath = tpStr
        }
      }
    } catch {}

    let anchor: [number, number] = [0.5, 0.5]
    try {
      const av = jl.anchor
      if (Array.isArray(av) && av.length >= 2) {
        anchor = [Number(av[0]), Number(av[1])]
      }
    } catch {}

    let isGif = false
    try {
      isGif = Boolean(jl.isGif)
    } catch {}

    let father = -1
    try {
      father = Number(jl.father ?? -1)
    } catch {
      father = -1
    }

    let rotateWithFather = true
    try {
      rotateWithFather = Boolean(jl.rotateWithFather ?? true)
    } catch {}

    fathers.push(father)
    rotWithFathers.push(rotateWithFather)

    linesOut.push({
      lid: i,
      pos_x: posX,
      pos_y: posY,
      rot,
      alpha: alpha01,
      scroll_px: scroll,
      color_rgb: rgb,
      color: colorTrack,
      scale_x: scaleXTrack,
      scale_y: scaleYTrack,
      text: textTrack,
      texture_path: texPath,
      anchor,
      is_gif: isGif,
      gif_progress: gifTrack,
      father,
      rotate_with_father: rotateWithFather,
      name,
      event_counts: evc,
    })

    // Parse notes
    const nidBase = i * 100000
    let nid = nidBase
    for (const n of (jl.notes ?? [])) {
      // RPE note type mapping: 1=Tap, 2=Hold, 3=Flick, 4=Drag
      // Internal mapping: 1=Tap, 2=Drag, 3=Hold, 4=Flick
      let rpeType: number
      try {
        rpeType = Number(n.type ?? 1)
      } catch {
        rpeType = 1
      }
      let kind: number
      if (rpeType === 2) kind = 3
      else if (rpeType === 3) kind = 4
      else if (rpeType === 4) kind = 2
      else kind = 1

      const b0 = beatToValue(n.startTime ?? [0, 0, 1])
      const b1 = beatToValue(n.endTime ?? n.startTime ?? [0, 0, 1])
      const tHit = bpmMap.beatToSec(b0, bpmfactor)
      const tEnd = bpmMap.beatToSec(b1, bpmfactor)

      // If duration > 0, treat as hold
      if (tEnd > tHit + 1e-9) {
        kind = 3
      }

      // RPE above=1 means "front" side, invert for our internal representation
      let aboveRaw: number
      try {
        aboveRaw = Number(n.above ?? 1)
      } catch {
        aboveRaw = 1
      }
      const above = aboveRaw !== 1
      const fake = Number(n.isFake ?? 0) === 1

      const posxUnits = Number(n.positionX ?? 0.0)
      const yOffsetUnits = Number(n.yOffset ?? 0.0)
      const size = Number(n.size ?? 1.0)
      const speedMul = Number(n.speed ?? 1.0)

      const na = n.alpha
      const alphaNote = na == null ? 1.0 : clamp(Number(na) / 255.0, 0.0, 1.0)

      const hs = n.hitsound ?? null

      let tintVal = n.tint ?? n.color
      const tintRgb = tintVal != null ? parseRgb3(tintVal) : [255, 255, 255] as [number, number, number]

      const tintHitfxRgb = parseRgb3Opt(n.tintHitEffects)

      // Calculate scroll positions
      const scrollHit = scroll.integral(tHit)
      const scrollEnd = kind === 3 ? scroll.integral(tEnd) : scrollHit

      const note: RuntimeNote = {
        nid,
        line_id: i,
        kind,
        above,
        fake,
        t_hit: tHit,
        t_end: kind === 3 ? tEnd : tHit,
        x_local_px: posxUnits * sx,
        y_offset_px: yOffsetUnits * sy,
        speed_mul: speedMul,
        size_px: size,
        alpha01: alphaNote,
        tint_rgb: tintRgb,
        tint_hitfx_rgb: tintHitfxRgb,
        scroll_hit: scrollHit,
        scroll_end: scrollEnd,
        hitsound_path: hs,
        t_enter: 0,
        mh: false,
      }
      notesOut.push(note)
      nid++
    }
  }

  // Compose father/child judge lines
  const baseX = linesOut.map(ln => ln.pos_x)
  const baseY = linesOut.map(ln => ln.pos_y)
  const baseR = linesOut.map(ln => ln.rot)

  const stateMark = new Array(linesOut.length).fill(0) // 0=unvisited, 1=visiting, 2=done
  const cache = new Map<number, [(t: number) => number, (t: number) => number, (t: number) => number]>()

  const buildComp = (lid: number): [(t: number) => number, (t: number) => number, (t: number) => number] => {
    if (cache.has(lid)) {
      return cache.get(lid)!
    }
    if (lid < 0 || lid >= linesOut.length) {
      const z: [(t: number) => number, (t: number) => number, (t: number) => number] = [
        () => 0.0,
        () => 0.0,
        () => 0.0,
      ]
      return z
    }
    if (stateMark[lid] === 1) {
      throw new Error(`RPE father cycle detected at line ${lid}`)
    }
    if (stateMark[lid] === 2) {
      return cache.get(lid)!
    }
    stateMark[lid] = 1

    let f = fathers[lid] ?? -1
    try {
      f = Number(f)
    } catch {
      f = -1
    }

    const bx = baseX[lid]
    const by = baseY[lid]
    const br = baseR[lid]

    // Helper to convert PiecewiseEased | function to function
    const toFunc = (track: PiecewiseEased | ((t: number) => number)): ((t: number) => number) => {
      if (typeof track === 'function') {
        return track
      } else {
        return (t: number) => track.eval(t)
      }
    }

    let x: (t: number) => number
    let y: (t: number) => number
    let r: (t: number) => number

    if (f < 0 || f >= linesOut.length) {
      x = toFunc(bx)
      y = toFunc(by)
      r = toFunc(br)
    } else {
      const [px, py, pr] = buildComp(f)
      const bxFunc = toFunc(bx)
      const byFunc = toFunc(by)
      const brFunc = toFunc(br)
      x = (t: number) => bxFunc(t) + px(t)
      y = (t: number) => byFunc(t) + py(t)
      if (rotWithFathers[lid]) {
        r = (t: number) => brFunc(t) + pr(t)
      } else {
        r = brFunc
      }
    }

    cache.set(lid, [x, y, r])
    stateMark[lid] = 2
    return [x, y, r]
  }

  for (let lid = 0; lid < linesOut.length; lid++) {
    const [x, y, r] = buildComp(lid)
    linesOut[lid].pos_x = x
    linesOut[lid].pos_y = y
    linesOut[lid].rot = r
  }

  // Precompute note enter times (align with pygame visibility.py)
  precomputeTEnter(linesOut, notesOut, W, H)

  // Sort notes by hit time
  notesOut.sort((a, b) => a.t_hit - b.t_hit)

  // Calculate duration
  const maxNoteTime = notesOut.length > 0 ? Math.max(...notesOut.map(n => n.t_end)) : 0
  const duration = maxNoteTime + 2.0

  return {
    lines: linesOut,
    notes: notesOut,
    duration,
    bpm: 120, // RPE doesn't have a single BPM
    offset,
    metadata: {
      format: 'rpe',
      name: meta.name,
      artist: meta.composer,
      charter: meta.charter,
      difficulty: meta.level,
    },
  }
}

/**
 * Detect if data is RPE format
 */
export function isRPEFormat(data: any): boolean {
  return data && (
    data.META ||
    data.judgeLineList instanceof Array &&
    data.judgeLineList[0]?.bpmfactor !== undefined
  )
}
