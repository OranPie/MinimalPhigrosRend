/**
 * Main game loop integrating renderer, audio, input, and judge systems
 * Ported from phic_renderer/renderer/pygame_backend.py
 */

import type { ParsedChart } from '@phic-web/shared'
import { PixiRenderer } from '../renderer/PixiRenderer.js'
import { AudioManager } from '../audio/AudioManager.js'
import { GameState, type GameConfig } from './GameState.js'
import type { Respack } from '../loaders/RespackLoader.js'
import type { JudgeGrade } from '../runtime/Judge.js'
import { evalLineState, noteWorldPos } from '../runtime/kinematics.js'

/**
 * Game loop options
 */
export interface GameLoopOptions extends GameConfig {
  /** Background image URL */
  backgroundUrl?: string
  /** BGM URL */
  bgmUrl?: string
  /** Resource pack for note textures, hitfx and hitsounds */
  respack?: Respack
  /** Multiplier for note base size */
  noteSizeMult?: number
  /** Target FPS (default: 120) */
  targetFps?: number
}

/**
 * Main game loop class
 */
export class GameLoop {
  private renderer: PixiRenderer
  private audio: AudioManager
  private state: GameState

  private hitsoundLastMs: Map<string, number> = new Map()

  private rafId: number | null = null
  private startTimeMs: number = 0
  private prevAutoplayTime: number = -1e9

  private config: GameLoopOptions

  constructor(
    canvas: HTMLCanvasElement,
    chart: ParsedChart,
    options: GameLoopOptions
  ) {
    this.config = {
      ...options,
      chartSpeed: options.chartSpeed ?? 1.0,
      audioOffset: options.audioOffset ?? chart.offset ?? 0,
      bgmVolume: options.bgmVolume ?? 0.8,
      startTime: options.startTime ?? 0,
      autoPlay: options.autoPlay ?? false,
      targetFps: options.targetFps ?? 120,
    }

    // Initialize systems
    this.renderer = new PixiRenderer(canvas, {
      width: this.config.width,
      height: this.config.height,
      antialias: true,
      backgroundColor: 0x0a0a0e,
    })

    this.audio = new AudioManager()

    this.state = new GameState(chart, this.config)
  }

  /**
   * Initialize and start the game
   */
  public async start(): Promise<void> {
    console.log('[GameLoop] Initializing renderer...')
    // Initialize renderer
    await this.renderer.init()
    console.log('[GameLoop] Renderer initialized')

    if (this.config.respack) {
      this.renderer.setRespack(this.config.respack)
    }

    if (this.config.noteSizeMult !== undefined) {
      this.renderer.setNoteSizeMult(this.config.noteSizeMult)
    }

    // Load background if provided
    if (this.config.backgroundUrl) {
      console.log('[GameLoop] Loading background...')
      await this.renderer.setBackground(
        this.config.backgroundUrl,
        10, // blur radius
        0.5 // dim alpha
      )
      console.log('[GameLoop] Background loaded')
    }

    console.log('[GameLoop] Initializing audio...')
    // Initialize audio
    await this.audio.init()
    console.log('[GameLoop] Audio initialized')

    // Play BGM if provided
    if (this.config.bgmUrl) {
      console.log('[GameLoop] Playing BGM...')
      const chartSpeed = this.config.chartSpeed ?? 1.0
      const audioOffset = this.config.audioOffset ?? 0
      const startTime = this.config.startTime ?? 0
      const startPosSec = audioOffset + (startTime / chartSpeed)
      await this.audio.playMusicFile(
        this.config.bgmUrl,
        this.config.bgmVolume ?? 0.8,
        startPosSec > 0 ? startPosSec : 0
      )
      this.state.useBgmClock = true
      console.log('[GameLoop] BGM playing')
    }

    // Set start time
    this.startTimeMs = performance.now()
    const startTime = this.config.startTime ?? 0
    if (startTime > 0 && !this.state.useBgmClock) {
      this.startTimeMs -= startTime * 1000
    }

    this.state.isPlaying = true

    console.log('[GameLoop] Starting render loop...')
    // Start game loop
    this.rafId = requestAnimationFrame(this.update.bind(this))
    console.log('[GameLoop] All systems started!')
  }

  /**
   * Main update loop
   */
  private update(timestamp: number): void {
    if (!this.state.isPlaying) return

    // Handle pause
    if (this.state.isPaused) {
      this.rafId = requestAnimationFrame(this.update.bind(this))
      return
    }

    // Calculate current time
    let currentTime: number
    const chartSpeed = this.config.chartSpeed ?? 1.0
    const audioOffset = this.config.audioOffset ?? 0

    if (this.state.useBgmClock) {
      // Use audio time as clock source
      const audioTime = this.audio.getMusicPosition()
      if (audioTime !== null) {
        currentTime = (audioTime - audioOffset) * chartSpeed
      } else {
        currentTime = this.state.currentTime
      }
    } else {
      // Use performance.now() as clock source
      const elapsed = (timestamp - this.startTimeMs) / 1000 // Convert to seconds
      currentTime = (elapsed - audioOffset) * chartSpeed
    }

    this.state.currentTime = currentTime

    if (this.config.autoPlay) {
      this.autoplayStep(currentTime)
    } else {
      this.holdMaintenance(currentTime)
    }

    this.holdFinalize(currentTime)

    if (this.config.respack) {
      this.holdTickFx(currentTime)
    }

    // Check for end condition
    const endTime = this.config.endTime
    if (endTime !== undefined && currentTime > endTime) {
      this.stop()
      return
    }

    if (this.state.isComplete()) {
      this.stop()
      return
    }

    // Update miss detection (notes that weren't hit in time)
    this.detectMisses(currentTime)

    // Render frame
    this.renderer.render(
      currentTime,
      this.state.lines,
      this.state.noteStates,
      this.state.idxNext
    )

    // Update UI
    this.renderer.updateUI(
      this.state.getScore(),
      this.state.getCombo(),
      this.state.getAccuracy()
    )

    // Continue loop
    this.rafId = requestAnimationFrame(this.update.bind(this))
  }

  /**
   * Detect missed notes
   */
  private detectMisses(currentTime: number): void {
    const missWindow = 0.160 // 160ms miss window

    // Scan ahead from idxNext
    for (let i = this.state.idxNext; i < this.state.noteStates.length; i++) {
      const noteState = this.state.noteStates[i]
      const note = noteState.note

      // Skip already judged or fake notes
      if (noteState.judged || note.fake) continue

      // Skip hold notes (they have different logic)
      if (note.kind === 3) continue

      // Check if note passed the miss window
      const timePassed = currentTime - note.t_hit
      if (timePassed > missWindow) {
        noteState.judged = true
        noteState.miss = true
        noteState.miss_t = currentTime
        this.state.judge.markMiss(noteState)
      } else {
        // Notes are sorted by time, so we can stop here
        break
      }
    }

    // Update idxNext to skip processed notes
    while (
      this.state.idxNext < this.state.noteStates.length &&
      this.state.noteStates[this.state.idxNext].judged
    ) {
      this.state.idxNext++
    }
  }

  /**
   * Handle pointer down (for manual input)
   */
  public onPointerDown(event: PointerEvent): void {
    this.state.inputHandler.onPointerDown(event)

    if (this.config.autoPlay) {
      return
    }

    // Try to hit hold notes
    const result = this.state.inputHandler.processHoldStart(
      event.pointerId,
      event.clientX,
      this.state.currentTime,
      this.state.noteStates,
      this.state.idxNext,
      this.state.lines,
      this.state.judge
    )

    if (result) {
      if (result.noteState.note.kind === 3) {
        this.state.judge.bumpCombo()
        result.noteState.next_hold_fx_ms = Math.floor(this.state.currentTime * 1000) + 200
      }
      const rgba = this.pickHitfxRgba(result.grade, result.noteState.note.tint_hitfx_rgb)
      this.renderer.addHitEffect(result.x, result.y, result.rotation, result.noteState.note.kind, rgba, this.state.currentTime)
      if (!result.noteState.note.fake) {
        this.playHitsoundForNoteKind(result.noteState.note.kind, performance.now())
      }
    }
  }

  /**
   * Handle pointer up (for manual input)
   */
  public onPointerUp(event: PointerEvent): void {
    const gesture = this.state.inputHandler.onPointerUp(event, this.state.currentTime)

    if (this.config.autoPlay) {
      return
    }

    if (gesture) {
      // Process tap/flick gesture
      const result = this.state.inputHandler.processGesture(
        gesture,
        this.state.noteStates,
        this.state.idxNext,
        this.state.lines,
        this.state.judge
      )

      if (result) {
        const rgba = this.pickHitfxRgba(result.grade, result.noteState.note.tint_hitfx_rgb)
        this.renderer.addHitEffect(result.x, result.y, result.rotation, result.noteState.note.kind, rgba, this.state.currentTime)
        if (!result.noteState.note.fake) {
          this.playHitsoundForNoteKind(result.noteState.note.kind, performance.now())
        }
      }
    }
  }

  /**
   * Handle pointer move (for manual input)
   */
  public onPointerMove(event: PointerEvent): void {
    this.state.inputHandler.onPointerMove(event)

    if (this.config.autoPlay) {
      return
    }

    // Process drag notes continuously
    const results = this.state.inputHandler.processDragNotes(
      this.state.currentTime,
      this.state.noteStates,
      this.state.idxNext,
      this.state.lines,
      this.state.judge
    )

    for (const result of results) {
      const rgba = this.pickHitfxRgba(result.grade, result.noteState.note.tint_hitfx_rgb)
      this.renderer.addHitEffect(result.x, result.y, result.rotation, result.noteState.note.kind, rgba, this.state.currentTime)
      if (!result.noteState.note.fake) {
        this.playHitsoundForNoteKind(result.noteState.note.kind, performance.now())
      }
    }
  }

  private sanitizeGrade(noteKind: number, grade: JudgeGrade | 'MISS'): JudgeGrade | 'MISS' | null {
    const g = String(grade).toUpperCase()
    const k = Number(noteKind)
    if (k === 2 || k === 4) {
      return g === 'PERFECT' ? 'PERFECT' : null
    }
    if (k === 3) {
      if (g === 'PERFECT') return 'PERFECT'
      if (g === 'GOOD' || g === 'BAD') return 'GOOD'
      if (g === 'MISS') return 'MISS'
      return null
    }
    if (k === 1) {
      if (g === 'PERFECT' || g === 'GOOD' || g === 'BAD') return g as JudgeGrade
      if (g === 'MISS') return 'MISS'
      return null
    }
    return null
  }

  private autoplayStep(t: number): void {
    const prev = this.prevAutoplayTime
    const st0 = Math.max(0, this.state.idxNext - 20)
    const st1 = Math.min(this.state.noteStates.length, this.state.idxNext + 300)

    for (let i = st0; i < st1; i++) {
      const s = this.state.noteStates[i]
      const n = s.note
      if (s.judged || n.fake) continue

      if (n.kind !== 3) {
        const grade = this.sanitizeGrade(n.kind, 'PERFECT')
        const tHit = n.t_hit
        if (grade && prev < tHit && tHit <= t) {
          if (grade === 'MISS') {
            s.miss = true
            s.judged = true
            s.miss_t = tHit
            this.state.judge.markMiss(s)
            continue
          }

          this.state.judge.tryHit(s, tHit)

          if (!n.fake) {
            const ln = this.state.lines[n.line_id]
            const lineState = evalLineState(ln, tHit)
            const [x, y] = noteWorldPos(lineState.x, lineState.y, lineState.rot, lineState.scroll, n, n.scroll_hit, false)
            const rgba = this.pickHitfxRgba('PERFECT', n.tint_hitfx_rgb)
            this.renderer.addHitEffect(x, y, lineState.rot, n.kind, rgba, tHit)
            this.playHitsoundForNoteKind(n.kind, tHit * 1000)
          }
        }
      } else {
        const grade = this.sanitizeGrade(3, 'PERFECT')
        const tHit = n.t_hit
        if (!s.holding && grade && prev < tHit && tHit <= t) {
          if (grade === 'MISS') {
            s.miss = true
            s.judged = true
            s.miss_t = tHit
            s.hold_failed = true
            s.hold_finalized = true
            this.state.judge.markMiss(s)
            continue
          }

          s.hit = true
          s.holding = true
          s.hold_grade = grade
          this.state.judge.bumpCombo()
          s.next_hold_fx_ms = Math.floor(tHit * 1000) + 200

          if (!n.fake) {
            const ln = this.state.lines[n.line_id]
            const lineState = evalLineState(ln, tHit)
            const [x, y] = noteWorldPos(lineState.x, lineState.y, lineState.rot, lineState.scroll, n, lineState.scroll, false)
            const rgba = this.pickHitfxRgba(grade, n.tint_hitfx_rgb)
            this.renderer.addHitEffect(x, y, lineState.rot, n.kind, rgba, tHit)
            this.playHitsoundForNoteKind(n.kind, tHit * 1000)
          }
        }

        if (s.holding) {
          if (t >= n.t_end) {
            s.holding = false
          }
        }
      }
    }

    this.prevAutoplayTime = t
  }

  private holdMaintenance(t: number): void {
    const holdTailTol = 0.8
    const st0 = Math.max(0, this.state.idxNext - 50)
    const st1 = Math.min(this.state.noteStates.length, this.state.idxNext + 500)

    for (let i = st0; i < st1; i++) {
      const s = this.state.noteStates[i]
      const n = s.note
      if (s.judged || n.fake) continue
      if (n.kind !== 3 || !s.holding) continue

      const pid = s.hold_pointer_id
      const downNow = this.state.inputHandler.isDown(pid)
      if (!downNow && t < n.t_end - 1e-6) {
        const dur = Math.max(1e-6, n.t_end - n.t_hit)
        const progR = Math.max(0, Math.min(1, (t - n.t_hit) / dur))
        s.released_early = true
        if (progR < holdTailTol) {
          s.miss = true
          s.judged = true
          s.miss_t = t
          s.hold_failed = true
          s.hold_finalized = true
          s.holding = false
          this.state.judge.markMiss(s)
        } else {
          s.holding = false
        }
      }

      if (t >= n.t_end) {
        s.holding = false
      }
    }
  }

  private holdFinalize(t: number): void {
    const holdTailTol = 0.8
    const missWindow = 0.160
    const st0 = Math.max(0, this.state.idxNext - 200)
    const st1 = Math.min(this.state.noteStates.length, this.state.idxNext + 800)

    for (let i = st0; i < st1; i++) {
      const s = this.state.noteStates[i]
      const n = s.note
      if (n.fake || n.kind !== 3 || s.hold_finalized) continue

      if (!s.hit && !s.hold_failed && t > n.t_hit + missWindow) {
        s.hold_failed = true
        this.state.judge.breakCombo()
      }

      if (s.released_early && !s.hold_finalized) {
        const dur = Math.max(1e-6, n.t_end - n.t_hit)
        const prog = Math.max(0, Math.min(1, (t - n.t_hit) / dur))
        if (prog < holdTailTol) {
          s.hold_failed = true
          this.state.judge.breakCombo()
        } else {
          const g = (s.hold_grade ?? 'PERFECT') as JudgeGrade
          this.state.judge.applyHoldResult(g)
          s.hold_finalized = true
        }
      }

      if (t >= n.t_end && !s.hold_finalized) {
        if (s.hit && !s.hold_failed) {
          const g = (s.hold_grade ?? 'PERFECT') as JudgeGrade
          this.state.judge.applyHoldResult(g)
        } else {
          this.state.judge.markMiss(s)
        }
        s.hold_finalized = true
        s.judged = true
      }
    }
  }

  private holdTickFx(t: number): void {
    const respack = this.config.respack
    if (!respack) return

    const holdFxIntervalMs = 200
    const nowTick = Math.floor(t * 1000)
    const st0 = Math.max(0, this.state.idxNext - 200)
    const st1 = Math.min(this.state.noteStates.length, this.state.idxNext + 800)

    for (let i = st0; i < st1; i++) {
      const s = this.state.noteStates[i]
      const n = s.note
      if (n.fake || n.kind !== 3 || !s.holding || s.judged) continue
      if (t >= n.t_end) continue

      if (s.next_hold_fx_ms <= 0) {
        s.next_hold_fx_ms = nowTick + holdFxIntervalMs
        continue
      }

      while (nowTick >= s.next_hold_fx_ms && t < n.t_end) {
        const ln = this.state.lines[n.line_id]
        const lineState = evalLineState(ln, t)
        const [x, y] = noteWorldPos(lineState.x, lineState.y, lineState.rot, lineState.scroll, n, lineState.scroll, false)
        const rgba = this.pickHitfxRgba('PERFECT', n.tint_hitfx_rgb) ?? respack.judgeColors.PERFECT
        if (!n.fake) {
          this.renderer.addHitEffect(x, y, lineState.rot, n.kind, rgba, t)
        }
        s.next_hold_fx_ms += holdFxIntervalMs
      }
    }
  }

  private pickHitfxRgba(grade: JudgeGrade, tintHitfxRgb: [number, number, number] | null): [number, number, number, number] | undefined {
    if (tintHitfxRgb) {
      return [tintHitfxRgb[0], tintHitfxRgb[1], tintHitfxRgb[2], 255]
    }

    const respack = this.config.respack
    if (!respack) return undefined

    return (respack.judgeColors as any)[grade] ?? respack.judgeColors.PERFECT
  }

  private playHitsoundForNoteKind(noteKind: number, nowMs: number): void {
    const respack = this.config.respack
    if (!respack) return

    let key: keyof Respack['sounds']
    switch (noteKind) {
      case 1:
        key = 'click'
        break
      case 2:
        key = 'drag'
        break
      case 4:
        key = 'flick'
        break
      case 3:
      default:
        key = 'click'
        break
    }

    const minIntervalMs = 0
    const last = this.hitsoundLastMs.get(key) ?? -1e12
    if (minIntervalMs > 0 && nowMs - last < minIntervalMs) return

    const snd = respack.sounds[key]
    if (snd) {
      this.audio.playSound(snd, 1.0)
      this.hitsoundLastMs.set(key, nowMs)
    }
  }

  /**
   * Pause the game
   */
  public pause(): void {
    this.state.pause()
    this.audio.pauseMusic()
  }

  /**
   * Resume the game
   */
  public resume(): void {
    this.state.resume()
    this.audio.unpauseMusic()

    // Adjust start time to account for pause
    this.startTimeMs = performance.now() - (this.state.currentTime * 1000)
  }

  /**
   * Stop the game
   */
  public stop(): void {
    this.state.isPlaying = false

    if (this.rafId !== null) {
      cancelAnimationFrame(this.rafId)
      this.rafId = null
    }

    this.audio.stopMusic()
  }

  /**
   * Reset and restart the game
   */
  public restart(): void {
    this.stop()
    this.state.reset()
    this.start()
  }

  /**
   * Destroy the game loop and clean up resources
   */
  public destroy(): void {
    this.stop()
    this.renderer.destroy()
    this.audio.destroy()
  }

  /**
   * Get current game state (for external access)
   */
  public getState(): GameState {
    return this.state
  }
}
