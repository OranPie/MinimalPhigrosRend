/**
 * Input handler for multi-touch and gesture recognition
 * Ported and enhanced from phic_renderer/renderer/pygame/manual_judgement.py
 */

import type { RuntimeLine, RuntimeNote, NoteState } from '@phic-web/shared'
import { evalLineState, noteWorldPos } from './kinematics.js'
import { Judge, type JudgeGrade } from './Judge.js'

/**
 * Pointer state for multi-touch tracking
 */
interface PointerState {
  id: number
  x: number
  y: number
  startX: number
  startY: number
  startTime: number
  lastX: number
  lastY: number
  lastTime: number
  downTime: number
  isDown: boolean
}

/**
 * Gesture types
 */
export type GestureType = 'tap' | 'hold' | 'drag' | 'flick'

/**
 * Gesture event
 */
export interface GestureEvent {
  type: GestureType
  pointerId: number
  x: number
  y: number
  time: number
  /** Velocity for flick gestures (pixels per second) */
  velocity?: number
}

/**
 * Input handler configuration
 */
export interface InputHandlerConfig {
  /** Judge width as ratio of screen width (default: 0.12 = 12%) */
  judgeWidthRatio?: number
  /** Minimum velocity for flick detection (px/s, default: 500) */
  flickVelocityThreshold?: number
  /** Maximum time for tap (ms, default: 200) */
  maxTapDuration?: number
  /** Minimum time for hold to register (ms, default: 100) */
  minHoldDuration?: number
}

/**
 * Hit result from input processing
 */
export interface HitResult {
  noteState: NoteState
  grade: JudgeGrade
  x: number
  y: number
  rotation: number
}

/**
 * Input handler for web-based multi-touch input
 */
export class InputHandler {
  private pointers: Map<number, PointerState> = new Map()
  private config: Required<InputHandlerConfig>
  private judgeWidthPx: number

  constructor(
    private screenWidth: number,
    config?: InputHandlerConfig
  ) {
    this.config = {
      judgeWidthRatio: config?.judgeWidthRatio ?? 0.12,
      flickVelocityThreshold: config?.flickVelocityThreshold ?? 500,
      maxTapDuration: config?.maxTapDuration ?? 200,
      minHoldDuration: config?.minHoldDuration ?? 100,
    }
    this.judgeWidthPx = this.screenWidth * this.config.judgeWidthRatio
  }

  /**
   * Update screen dimensions
   */
  public setScreenSize(width: number): void {
    this.screenWidth = width
    this.judgeWidthPx = width * this.config.judgeWidthRatio
  }

  /**
   * Handle pointer down event
   */
  public onPointerDown(e: PointerEvent): void {
    const now = performance.now()
    this.pointers.set(e.pointerId, {
      id: e.pointerId,
      x: e.clientX,
      y: e.clientY,
      startX: e.clientX,
      startY: e.clientY,
      startTime: now,
      lastX: e.clientX,
      lastY: e.clientY,
      lastTime: now,
      downTime: now,
      isDown: true,
    })
  }

  /**
   * Handle pointer move event
   */
  public onPointerMove(e: PointerEvent): void {
    const pointer = this.pointers.get(e.pointerId)
    if (!pointer) return

    pointer.lastX = pointer.x
    pointer.lastY = pointer.y
    pointer.lastTime = performance.now()
    pointer.x = e.clientX
    pointer.y = e.clientY
  }

  /**
   * Handle pointer up event and detect gestures
   */
  public onPointerUp(e: PointerEvent, currentTime: number): GestureEvent | null {
    const pointer = this.pointers.get(e.pointerId)
    if (!pointer) return null

    const now = performance.now()
    const duration = now - pointer.downTime
    const dx = e.clientX - pointer.startX
    const dy = e.clientY - pointer.startY
    const distance = Math.sqrt(dx * dx + dy * dy)

    // Calculate velocity from recent movement
    const dt = Math.max(1, now - pointer.lastTime)
    const vx = (e.clientX - pointer.lastX) / dt * 1000
    const vy = (e.clientY - pointer.lastY) / dt * 1000
    const velocity = Math.sqrt(vx * vx + vy * vy)

    pointer.isDown = false
    this.pointers.delete(e.pointerId)

    // Detect gesture type
    if (velocity > this.config.flickVelocityThreshold && distance > 30) {
      // Flick: fast swipe motion
      return {
        type: 'flick',
        pointerId: e.pointerId,
        x: e.clientX,
        y: e.clientY,
        time: currentTime,
        velocity,
      }
    } else if (duration < this.config.maxTapDuration && distance < 20) {
      // Tap: quick press and release
      return {
        type: 'tap',
        pointerId: e.pointerId,
        x: e.clientX,
        y: e.clientY,
        time: currentTime,
      }
    }

    return null
  }

  /**
   * Get all active pointers
   */
  public getActivePointers(): PointerState[] {
    return Array.from(this.pointers.values()).filter(p => p.isDown)
  }

  public isDown(pointerId?: number | null): boolean {
    if (pointerId === null || pointerId === undefined) {
      return this.getActivePointers().length > 0
    }
    const p = this.pointers.get(pointerId)
    return Boolean(p && p.isDown)
  }

  /**
   * Check if a pointer is holding (for drag notes)
   */
  public isPointerHolding(pointerId: number, minDuration: number = 50): boolean {
    const pointer = this.pointers.get(pointerId)
    if (!pointer || !pointer.isDown) return false
    return (performance.now() - pointer.downTime) >= minDuration
  }

  /**
   * Calculate note's X position at given time
   */
  private noteXAtTime(
    lines: RuntimeLine[],
    note: RuntimeNote,
    t: number
  ): number {
    const line = lines[note.line_id]
    const state = evalLineState(line, t)
    const [x] = noteWorldPos(
      state.x,
      state.y,
      state.rot,
      state.scroll,
      note,
      note.scroll_hit,
      false
    )
    return x
  }

  /**
   * Check if note is within judge width of pointer
   */
  private isInJudgeWidth(
    lines: RuntimeLine[],
    note: RuntimeNote,
    t: number,
    pointerX: number | null
  ): boolean {
    if (pointerX === null) return true

    try {
      const noteX = this.noteXAtTime(lines, note, t)
      return Math.abs(pointerX - noteX) <= this.judgeWidthPx * 0.5
    } catch {
      return true
    }
  }

  /**
   * Find best candidate note for judgement
   */
  public findBestCandidate(
    states: NoteState[],
    idxNext: number,
    allowKinds: Set<number>,
    t: number,
    pointerX: number | null,
    lines: RuntimeLine[],
    judge: Judge
  ): NoteState | null {
    let bestState: NoteState | null = null
    let bestDt = Infinity

    // Search window around current index
    const start = Math.max(0, idxNext - 80)
    const end = Math.min(states.length, idxNext + 900)

    for (let i = start; i < end; i++) {
      const state = states[i]
      if (state.judged || state.note.fake) continue

      const note = state.note
      if (!allowKinds.has(note.kind)) continue

      const dt = Math.abs(t - note.t_hit)
      if (dt > judge.windows.BAD) continue

      if (!this.isInJudgeWidth(lines, note, t, pointerX)) continue

      if (dt < bestDt) {
        bestDt = dt
        bestState = state
      }
    }

    return bestState
  }

  /**
   * Process gesture and attempt to judge notes
   */
  public processGesture(
    gesture: GestureEvent,
    states: NoteState[],
    idxNext: number,
    lines: RuntimeLine[],
    judge: Judge
  ): HitResult | null {
    let allowKinds: Set<number>

    if (gesture.type === 'tap') {
      allowKinds = new Set([1]) // Tap notes
    } else if (gesture.type === 'flick') {
      allowKinds = new Set([4]) // Flick notes
    } else {
      return null
    }

    const candidate = this.findBestCandidate(
      states,
      idxNext,
      allowKinds,
      gesture.time,
      gesture.x,
      lines,
      judge
    )

    if (!candidate) return null

    const note = candidate.note
    const result = judge.tryHit(candidate, gesture.time)
    if (!result) return null

    // Calculate world position for visual feedback
    const line = lines[note.line_id]
    const lineState = evalLineState(line, gesture.time)
    const [x, y] = noteWorldPos(
      lineState.x,
      lineState.y,
      lineState.rot,
      lineState.scroll,
      note,
      note.scroll_hit,
      false
    )

    return {
      noteState: candidate,
      grade: result.grade,
      x,
      y,
      rotation: lineState.rot,
    }
  }

  /**
   * Process continuous drag notes (held pointer)
   */
  public processDragNotes(
    t: number,
    states: NoteState[],
    idxNext: number,
    lines: RuntimeLine[],
    judge: Judge
  ): HitResult[] {
    const results: HitResult[] = []
    const activePointers = this.getActivePointers()

    for (const pointer of activePointers) {
      if (!this.isPointerHolding(pointer.id, 50)) continue

      const candidate = this.findBestCandidate(
        states,
        idxNext,
        new Set([2]), // Drag notes
        t,
        pointer.x,
        lines,
        judge
      )

      if (!candidate) continue

      const note = candidate.note
      const dt = Math.abs(t - note.t_hit)
      if (dt > judge.windows.PERFECT) continue

      const result = judge.tryHit(candidate, t)
      if (!result) continue

      const line = lines[note.line_id]
      const lineState = evalLineState(line, t)
      const [x, y] = noteWorldPos(
        lineState.x,
        lineState.y,
        lineState.rot,
        lineState.scroll,
        note,
        note.scroll_hit,
        false
      )

      results.push({
        noteState: candidate,
        grade: result.grade,
        x,
        y,
        rotation: lineState.rot,
      })
    }

    return results
  }

  /**
   * Process hold note start (on pointer down)
   */
  public processHoldStart(
    pointerId: number,
    pointerX: number,
    t: number,
    states: NoteState[],
    idxNext: number,
    lines: RuntimeLine[],
    judge: Judge
  ): HitResult | null {
    const candidate = this.findBestCandidate(
      states,
      idxNext,
      new Set([3]), // Hold notes
      t,
      pointerX,
      lines,
      judge
    )

    if (!candidate) return null

    const note = candidate.note
    const dt = Math.abs(t - note.t_hit)
    const grade = dt <= judge.windows.PERFECT ? 'PERFECT' :
                  dt <= judge.windows.BAD ? 'GOOD' : null

    if (!grade) return null

    // Mark as holding (will be finalized on release)
    candidate.hit = true
    candidate.holding = true
    candidate.hold_grade = grade
    candidate.hold_pointer_id = pointerId

    const line = lines[note.line_id]
    const lineState = evalLineState(line, t)
    const [x, y] = noteWorldPos(
      lineState.x,
      lineState.y,
      lineState.rot,
      lineState.scroll,
      note,
      note.scroll_hit,
      false
    )

    return {
      noteState: candidate,
      grade,
      x,
      y,
      rotation: lineState.rot,
    }
  }

  /**
   * Clear all pointers
   */
  public reset(): void {
    this.pointers.clear()
  }
}
