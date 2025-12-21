/**
 * Renderer types and interfaces
 */

import type { RuntimeLine, RuntimeNote, NoteState } from '@phic-web/shared'
import type { Container } from 'pixi.js'

/**
 * Renderer configuration
 */
export interface RendererConfig {
  /** Canvas width */
  width: number
  /** Canvas height */
  height: number
  /** Background color */
  backgroundColor?: number
  /** Antialiasing */
  antialias?: boolean
  /** Resolution multiplier */
  resolution?: number
  /** Transparent background */
  backgroundAlpha?: number
}

/**
 * Renderer state for a single frame
 */
export interface FrameState {
  /** Current time in seconds */
  time: number
  /** All judgment lines */
  lines: RuntimeLine[]
  /** All note states */
  states: NoteState[]
  /** Next note index to process */
  idxNext: number
  /** Screen dimensions */
  width: number
  height: number
}

/**
 * Background layer config
 */
export interface BackgroundConfig {
  /** Background image URL */
  imageUrl?: string
  /** Blur radius in pixels */
  blurRadius?: number
  /** Dim overlay alpha (0-1) */
  dimAlpha?: number
}

/**
 * Judgment line render data
 */
export interface LineRenderData {
  x: number
  y: number
  rotation: number
  alpha: number
  scroll: number
  line: RuntimeLine
}

/**
 * Note render data
 */
export interface NoteRenderData {
  x: number
  y: number
  rotation: number
  alpha: number
  note: RuntimeNote
  state: NoteState
  /** For hold notes: tail position */
  tailX?: number
  tailY?: number
  /** For hold notes: progress (0-1) */
  progress?: number
}

/**
 * Hit effect data
 */
export interface HitEffectData {
  x: number
  y: number
  rotation: number
  time: number
  noteKind: number
  rgba?: [number, number, number, number]
}

/**
 * Layer interface - all layers implement this
 */
export interface RenderLayer {
  /** Layer container */
  container: Container
  /** Initialize layer resources */
  init(): Promise<void>
  /** Update layer for current frame */
  update(state: FrameState): void
  /** Clean up layer resources */
  destroy(): void
  /** Resize layer to new dimensions */
  resize(width: number, height: number): void
}
