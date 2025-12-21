/**
 * Core runtime types ported from phic_renderer/types.py
 * These types represent the runtime state of charts, lines, and notes
 */

import type { PiecewiseEased, IntegralTrack, PiecewiseColor, PiecewiseText } from '../math/tracks.js'

/**
 * Runtime note data
 * Ported from phic_renderer/types.py:7-32
 */
export interface RuntimeNote {
  nid: number
  line_id: number
  kind: number              // 1=tap, 2=drag, 3=hold, 4=flick
  above: boolean
  fake: boolean
  t_hit: number             // Hit time in seconds
  t_end: number             // End time in seconds (for holds)
  x_local_px: number        // Position along tangent (pixels)
  y_offset_px: number       // Offset perpendicular to line
  speed_mul: number         // Hold tail multiplier (official) or general multiplier (RPE)
  size_px: number           // Width scale in pixels
  alpha01: number           // Alpha transparency (0..1)

  // Tint colors
  tint_rgb: [number, number, number]
  tint_hitfx_rgb: [number, number, number] | null

  // Cached scroll positions at key times
  scroll_hit: number        // Scroll position at hit time
  scroll_end: number        // Scroll position at end time

  // RPE-specific fields
  hitsound_path: string | null  // Custom hitsound path
  t_enter: number           // First time note enters screen (precomputed)
  mh: boolean               // Multi-hit flag for simultaneous notes
}

/**
 * Runtime judgment line data
 * Ported from phic_renderer/types.py:34-54
 */
export interface RuntimeLine {
  lid: number

  // Animation tracks (can be PiecewiseEased or callable)
  pos_x: PiecewiseEased | ((t: number) => number)
  pos_y: PiecewiseEased | ((t: number) => number)
  rot: PiecewiseEased | ((t: number) => number)      // Rotation in radians
  alpha: PiecewiseEased | ((t: number) => number)    // Alpha 0..1
  scroll_px: IntegralTrack | ((t: number) => number) // Scroll integral

  // Colors
  color_rgb: [number, number, number]
  color?: PiecewiseColor | ((t: number) => [number, number, number]) | null

  // Scaling
  scale_x?: PiecewiseEased | ((t: number) => number) | null
  scale_y?: PiecewiseEased | ((t: number) => number) | null

  // Text overlay
  text?: PiecewiseText | ((t: number) => string) | null

  // Texture
  texture_path?: string | null
  anchor: [number, number]  // Texture anchor point (0.5, 0.5 = center)

  // GIF support
  is_gif: boolean
  gif_progress?: PiecewiseEased | ((t: number) => number) | null

  // Hierarchy
  father: number            // Parent line ID (-1 = no parent)
  rotate_with_father: boolean

  // Metadata
  name: string
  event_counts: Record<string, number>
}

/**
 * Note state during gameplay
 * Tracks judgment and hold state for each note
 * Ported from phic_renderer/types.py:57-68
 */
export interface NoteState {
  note: RuntimeNote

  // Judgment state
  judged: boolean
  hit: boolean
  miss: boolean
  miss_t?: number                // Time when note was missed (for fade effect)

  // Hold state
  holding: boolean
  released_early: boolean
  hold_grade: string | null      // 'PERFECT' | 'GOOD' | 'BAD'
  hold_finalized: boolean
  hold_failed: boolean

  // Hold visual feedback
  next_hold_fx_ms: number        // Next hold effect time in milliseconds

  // Additional state for web (optional)
  hold_pointer_id?: number       // Which pointer is holding this note
}

/**
 * Parsed chart data containing all runtime information
 */
export interface ParsedChart {
  lines: RuntimeLine[]
  notes: RuntimeNote[]
  duration: number              // Total chart duration in seconds
  bpm: number                   // Base BPM
  offset: number                // Audio offset in seconds
  metadata?: {
    name?: string
    artist?: string
    charter?: string
    difficulty?: number
    format?: string             // 'official' | 'rpe' | 'pec'
  }
}

/**
 * Chart metadata without full parsed data
 */
export interface ChartMetadata {
  id: string
  name: string
  artist?: string
  charter?: string
  difficulty?: number
  duration?: number
  bpm?: number
  format: 'official' | 'rpe' | 'pec' | 'unknown'
  has_background: boolean
  has_music: boolean
  created_at?: Date
  updated_at?: Date
}
