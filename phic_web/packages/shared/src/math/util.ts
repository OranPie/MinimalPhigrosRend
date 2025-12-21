/**
 * Math utility functions ported from phic_renderer/math/util.py
 */

/**
 * Clamp a value between min and max
 */
export function clamp(x: number, a: number, b: number): number {
  return x < a ? a : x > b ? b : x
}

/**
 * Linear interpolation between two values
 */
export function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t
}

/**
 * Get current time in seconds using high-resolution timer
 */
export function nowSec(): number {
  return performance.now() / 1000
}

/**
 * Convert HSV to RGB
 * @param h Hue (0..1)
 * @param s Saturation (0..1)
 * @param v Value (0..1)
 * @returns RGB tuple with values 0-255
 */
export function hsvToRgb(h: number, s: number, v: number): [number, number, number] {
  const i = Math.floor(h * 6.0)
  const f = h * 6.0 - i
  const p = v * (1.0 - s)
  const q = v * (1.0 - f * s)
  const t = v * (1.0 - (1.0 - f) * s)

  let r: number, g: number, b: number

  switch (i % 6) {
    case 0:
      [r, g, b] = [v, t, p]
      break
    case 1:
      [r, g, b] = [q, v, p]
      break
    case 2:
      [r, g, b] = [p, v, t]
      break
    case 3:
      [r, g, b] = [p, q, v]
      break
    case 4:
      [r, g, b] = [t, p, v]
      break
    default:
      [r, g, b] = [v, p, q]
  }

  return [Math.floor(r * 255), Math.floor(g * 255), Math.floor(b * 255)]
}

/**
 * Rotate a 2D vector by angle
 * @param x X coordinate
 * @param y Y coordinate
 * @param ang Angle in radians
 * @returns Rotated coordinates [x, y]
 */
export function rotateVec(x: number, y: number, ang: number): [number, number] {
  const c = Math.cos(ang)
  const s = Math.sin(ang)
  return [c * x - s * y, s * x + c * y]
}

/**
 * Get the four corners of a rotated rectangle
 * @param cx Center X
 * @param cy Center Y
 * @param w Width
 * @param h Height
 * @param ang Rotation angle in radians
 * @returns Array of 4 corner points [[x1, y1], [x2, y2], [x3, y3], [x4, y4]]
 */
export function rectCorners(
  cx: number,
  cy: number,
  w: number,
  h: number,
  ang: number
): Array<[number, number]> {
  const hx = w * 0.5
  const hy = h * 0.5
  const pts: Array<[number, number]> = [
    [-hx, -hy],
    [hx, -hy],
    [hx, hy],
    [-hx, hy],
  ]

  const c = Math.cos(ang)
  const s = Math.sin(ang)

  return pts.map(([px, py]) => {
    const rx = c * px - s * py
    const ry = s * px + c * py
    return [cx + rx, cy + ry]
  })
}

/**
 * Apply canvas expansion transform to a point
 * Used for zoom/scale effects
 * @param x X coordinate
 * @param y Y coordinate
 * @param W Canvas width
 * @param H Canvas height
 * @param expand Expansion factor (>1 for zoom in, <1 for zoom out)
 * @returns Transformed coordinates [x, y]
 */
export function applyExpandXY(
  x: number,
  y: number,
  W: number,
  H: number,
  expand: number
): [number, number] {
  if (expand <= 1.000001) {
    return [x, y]
  }

  const cx = W * 0.5
  const cy = H * 0.5
  const s = 1.0 / expand

  return [cx + (x - cx) * s, cy + (y - cy) * s]
}

/**
 * Apply canvas expansion transform to an array of points
 */
export function applyExpandPts(
  pts: Array<[number, number]>,
  W: number,
  H: number,
  expand: number
): Array<[number, number]> {
  if (expand <= 1.000001) {
    return pts
  }

  return pts.map(([px, py]) => applyExpandXY(px, py, W, H, expand))
}
