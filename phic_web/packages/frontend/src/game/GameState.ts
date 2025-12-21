/**
 * Game state management
 */

import type { RuntimeLine, RuntimeNote, NoteState, ParsedChart } from '@phic-web/shared'
import { Judge } from '../runtime/Judge.js'
import { InputHandler } from '../runtime/InputHandler.js'

/**
 * Game configuration
 */
export interface GameConfig {
  /** Screen width */
  width: number
  /** Screen height */
  height: number
  /** Chart playback speed (default: 1.0) */
  chartSpeed?: number
  /** Audio offset in seconds (default: 0) */
  audioOffset?: number
  /** BGM volume (0-1, default: 0.8) */
  bgmVolume?: number
  /** Start time in seconds (for testing) */
  startTime?: number
  /** End time in seconds (for testing) */
  endTime?: number
  /** Enable auto-play (for testing/recording) */
  autoPlay?: boolean
}

/**
 * Game state during playback
 */
export class GameState {
  // Chart data
  public lines: RuntimeLine[]
  public notes: RuntimeNote[]
  public noteStates: NoteState[]

  // Systems
  public judge: Judge
  public inputHandler: InputHandler

  // Timing
  public startTimeMs: number = 0
  public currentTime: number = 0
  public chartSpeed: number = 1.0
  public audioOffset: number = 0

  // Playback state
  public isPaused: boolean = false
  public isPlaying: boolean = false
  public useBgmClock: boolean = false

  // Note tracking
  public idxNext: number = 0

  // Stats
  public totalNotes: number = 0
  public chartEndTime: number = 0

  constructor(chart: ParsedChart, config: GameConfig) {
    this.lines = chart.lines
    this.notes = chart.notes
    this.chartSpeed = config.chartSpeed ?? 1.0
    this.audioOffset = config.audioOffset ?? chart.offset ?? 0

    // Create note states
    this.noteStates = this.notes.map(note => ({
      note,
      judged: false,
      hit: false,
      miss: false,
      holding: false,
      released_early: false,
      hold_grade: null,
      hold_finalized: false,
      hold_failed: false,
      next_hold_fx_ms: 0,
    }))

    // Initialize systems
    this.judge = new Judge()
    this.inputHandler = new InputHandler(config.width)

    // Calculate total notes (excluding fake notes)
    this.totalNotes = this.notes.filter(n => !n.fake).length

    // Calculate chart end time
    this.chartEndTime = Math.max(
      ...this.notes.map(n => n.kind === 3 ? n.t_end : n.t_hit)
    )
  }

  /**
   * Reset game state for replay
   */
  public reset(): void {
    // Reset note states
    for (const state of this.noteStates) {
      state.judged = false
      state.hit = false
      state.miss = false
      state.holding = false
      state.released_early = false
      state.hold_grade = null
      state.hold_finalized = false
      state.hold_failed = false
      state.next_hold_fx_ms = 0
    }

    // Reset systems
    this.judge.reset()
    this.inputHandler.reset()

    // Reset timing
    this.idxNext = 0
    this.currentTime = 0
    this.isPaused = false
  }

  /**
   * Pause the game
   */
  public pause(): void {
    this.isPaused = true
  }

  /**
   * Resume the game
   */
  public resume(): void {
    this.isPaused = false
  }

  /**
   * Get current score (0-1,000,000)
   */
  public getScore(): number {
    return this.judge.getScore(this.totalNotes)
  }

  /**
   * Get current combo
   */
  public getCombo(): number {
    return this.judge.combo
  }

  /**
   * Get current accuracy (0-100)
   */
  public getAccuracy(): number {
    return this.judge.getAccuracy()
  }

  /**
   * Check if game is complete
   */
  public isComplete(): boolean {
    return this.currentTime > this.chartEndTime + 1.0 // 1 second after last note
  }
}
