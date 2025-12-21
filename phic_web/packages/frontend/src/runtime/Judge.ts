/**
 * Judge system for hit detection and scoring
 * Ported from phic_renderer/runtime/judge.py with enhancements
 */

import type { NoteState } from '@phic-web/shared'

/**
 * Judge grade types
 */
export type JudgeGrade = 'PERFECT' | 'GOOD' | 'BAD' | 'MISS'

/**
 * Judge result with timing information
 */
export interface JudgeResult {
  grade: JudgeGrade
  /** Time difference from perfect timing (negative = early, positive = late) */
  offset: number
  /** Accuracy value (0.0 to 1.0) */
  accuracy: number
}

/**
 * Judge weight for accuracy calculation
 */
export const JUDGE_WEIGHT: Record<JudgeGrade, number> = {
  PERFECT: 1.0,
  GOOD: 0.65,
  BAD: 0.0,
  MISS: 0.0,
}

/**
 * Judge timing windows (in seconds)
 */
export interface JudgeWindows {
  PERFECT: number
  GOOD: number
  BAD: number
}

/**
 * Default Phigros timing windows
 */
export const DEFAULT_WINDOWS: JudgeWindows = {
  PERFECT: 0.045,  // ±45ms
  GOOD: 0.090,     // ±90ms
  BAD: 0.150,      // ±150ms
}

/**
 * Judge system with improved timing and accuracy tracking
 */
export class Judge {
  // Timing windows
  public readonly windows: JudgeWindows

  // Score tracking
  public combo: number = 0
  public maxCombo: number = 0
  private accSum: number = 0.0      // Cumulative accuracy weight
  private judgedCount: number = 0   // Number of judged notes

  // Timing offset tracking (for calibration)
  private offsetHistory: number[] = []
  private readonly maxOffsetHistory: number = 100

  /**
   * Create a new Judge instance
   * @param windows - Custom timing windows (optional)
   */
  constructor(windows?: Partial<JudgeWindows>) {
    this.windows = { ...DEFAULT_WINDOWS, ...windows }
  }

  /**
   * Increment combo
   */
  private bump(): void {
    this.combo++
    this.maxCombo = Math.max(this.maxCombo, this.combo)
  }

  public bumpCombo(): void {
    this.bump()
  }

  /**
   * Break combo
   */
  public breakCombo(): void {
    this.combo = 0
  }

  /**
   * Try to judge a hit at the given time
   * @param noteState - Note state to judge
   * @param t - Current time in seconds
   * @returns Judge result if within timing window, null otherwise
   */
  public tryHit(noteState: NoteState, t: number): JudgeResult | null {
    const tNote = noteState.note.t_hit
    const dt = t - tNote
    const absDt = Math.abs(dt)

    let grade: JudgeGrade | null = null
    let accuracy = 0.0

    if (absDt <= this.windows.PERFECT) {
      grade = 'PERFECT'
      // Accuracy gradient within PERFECT window: 1.0 at center, ~0.9 at edge
      const normalizedDt = absDt / this.windows.PERFECT
      accuracy = 1.0 - normalizedDt * 0.1
      this.bump()
    } else if (absDt <= this.windows.GOOD) {
      grade = 'GOOD'
      accuracy = JUDGE_WEIGHT.GOOD
      this.bump()
    } else if (absDt <= this.windows.BAD) {
      grade = 'BAD'
      accuracy = JUDGE_WEIGHT.BAD
      this.breakCombo()
    } else {
      // Outside timing window
      return null
    }

    // Update note state
    noteState.judged = true
    noteState.hit = true

    // Update score
    this.accSum += accuracy
    this.judgedCount++

    // Track offset for calibration
    this.offsetHistory.push(dt)
    if (this.offsetHistory.length > this.maxOffsetHistory) {
      this.offsetHistory.shift()
    }

    return {
      grade,
      offset: dt,
      accuracy,
    }
  }

  /**
   * Get the grade for a given timing without modifying state
   * @param tNote - Note hit time
   * @param t - Current time
   * @returns Grade or null if outside timing window
   */
  public gradeWindow(tNote: number, t: number): JudgeGrade | null {
    const absDt = Math.abs(t - tNote)
    if (absDt <= this.windows.PERFECT) return 'PERFECT'
    if (absDt <= this.windows.GOOD) return 'GOOD'
    if (absDt <= this.windows.BAD) return 'BAD'
    return null
  }

  /**
   * Mark a note as missed
   * @param noteState - Note state to mark as missed
   */
  public markMiss(noteState: NoteState): void {
    noteState.judged = true
    noteState.miss = true
    this.breakCombo()
    this.accSum += JUDGE_WEIGHT.MISS
    this.judgedCount++
  }

  public applyHoldResult(grade: JudgeGrade): void {
    this.accSum += JUDGE_WEIGHT[grade] ?? 0.0
    this.judgedCount++
  }

  /**
   * Get current accuracy percentage (0-100)
   */
  public getAccuracy(): number {
    if (this.judgedCount === 0) return 100.0
    return (this.accSum / this.judgedCount) * 100.0
  }

  /**
   * Get current score (normalized to 1,000,000)
   */
  public getScore(totalNotes: number): number {
    if (totalNotes === 0) return 0
    const maxPossible = totalNotes * JUDGE_WEIGHT.PERFECT
    return Math.floor((this.accSum / maxPossible) * 1000000)
  }

  /**
   * Get median timing offset for calibration
   * @returns Median offset in seconds (negative = consistently early, positive = consistently late)
   */
  public getMedianOffset(): number {
    if (this.offsetHistory.length === 0) return 0

    const sorted = [...this.offsetHistory].sort((a, b) => a - b)
    const mid = Math.floor(sorted.length / 2)

    if (sorted.length % 2 === 0) {
      return (sorted[mid - 1] + sorted[mid]) / 2
    } else {
      return sorted[mid]
    }
  }

  /**
   * Get suggested calibration offset in milliseconds
   * This can be used to adjust the audio offset
   */
  public getSuggestedCalibration(): number {
    return Math.round(this.getMedianOffset() * 1000)
  }

  /**
   * Reset judge state
   */
  public reset(): void {
    this.combo = 0
    this.maxCombo = 0
    this.accSum = 0.0
    this.judgedCount = 0
    this.offsetHistory = []
  }

  /**
   * Get judge statistics
   */
  public getStats() {
    return {
      combo: this.combo,
      maxCombo: this.maxCombo,
      accuracy: this.getAccuracy(),
      judgedCount: this.judgedCount,
      medianOffset: this.getMedianOffset(),
      suggestedCalibration: this.getSuggestedCalibration(),
    }
  }
}
