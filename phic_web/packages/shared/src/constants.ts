// Note type constants (from Python: phic_renderer/core/constants.py)
export const NOTE_COLORS = {
  TAP: { r: 255, g: 220, b: 120 },    // Kind 1: Orange
  DRAG: { r: 140, g: 240, b: 255 },   // Kind 2: Cyan
  HOLD: { r: 120, g: 200, b: 255 },   // Kind 3: Light blue
  FLICK: { r: 255, g: 140, b: 220 },  // Kind 4: Pink
} as const

export const NOTE_KINDS = {
  TAP: 1,
  DRAG: 2,
  HOLD: 3,
  FLICK: 4,
} as const

export type NoteKind = typeof NOTE_KINDS[keyof typeof NOTE_KINDS]

// Judge timing windows (from Python: phic_renderer/runtime/judge.py)
export const JUDGE_WINDOWS = {
  PERFECT: 0.045,  // 45ms
  GOOD: 0.090,     // 90ms
  BAD: 0.150,      // 150ms
} as const

// Judge weights (from Python: phic_renderer/runtime/judge.py)
export const JUDGE_WEIGHTS = {
  PERFECT: 1.0,
  GOOD: 0.6,
  BAD: 0.0,
  MISS: 0.0,
} as const

export type JudgeGrade = 'PERFECT' | 'GOOD' | 'BAD' | 'MISS'
