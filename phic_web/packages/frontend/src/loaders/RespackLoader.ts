/**
 * Resource pack loader for web
 * Loads resource packs from zip files containing note images, sound effects, and configuration
 * Ported from phic_renderer/io/respack_impl.py
 */

import JSZip from 'jszip'
import { sound } from '@pixi/sound'
import { Texture } from 'pixi.js'

/**
 * Respack configuration data
 */
export interface RespackInfo {
  // Hit effect configuration
  hitFx?: number[] // [rows, cols] for sprite sheet layout
  hitFxDuration?: number
  hitFxScale?: number
  hitFxRotate?: boolean
  hitFxTinted?: boolean

  // Hold note configuration
  holdAtlas?: number[] // [tail_h, head_h]
  holdAtlasMH?: number[] // Multi-hold atlas
  holdRepeat?: boolean
  holdCompact?: boolean
  holdKeepHead?: boolean
  holdTailNoScale?: boolean

  // Visual configuration
  hideParticles?: boolean

  // Judge colors (hex RGBA)
  colorPerfect?: number | string
  colorGood?: number | string
  colorBad?: number | string
  colorMiss?: number | string
}

/**
 * RGBA color tuple
 */
export type RGBA = [number, number, number, number]

/**
 * Loaded resource pack
 */
export interface Respack {
  info: RespackInfo

  // Note textures
  textures: {
    click: Texture
    drag: Texture
    flick: Texture
    hold: Texture
    click_mh: Texture
    drag_mh: Texture
    flick_mh: Texture
    hold_mh: Texture
    hit_fx: Texture
  }

  // Sound effects
  sounds: {
    click?: any
    drag?: any
    flick?: any
  }

  // Hit effect configuration
  hitfx: {
    frames: [number, number] // [rows, cols]
    duration: number
    scale: number
    rotate: boolean
    tinted: boolean
  }

  // Hold note configuration
  hold: {
    tailH: number
    headH: number
    tailHMH: number
    headHMH: number
    repeat: boolean
    compact: boolean
    keepHead: boolean
    tailNoScale: boolean
  }

  // Visual configuration
  hideParticles: boolean

  // Judge colors
  judgeColors: {
    PERFECT: RGBA
    GOOD: RGBA
    BAD: RGBA
    MISS: RGBA
  }
}

/**
 * Parse hex RGBA color value
 */
function parseHexRGBA(value: number | string | undefined, defaultColor: RGBA): RGBA {
  if (value === undefined || value === null) {
    return defaultColor
  }

  try {
    let n: number
    if (typeof value === 'number') {
      n = value
    } else {
      const s = value.trim()
      n = parseInt(s, 0) // Support 0x prefix
    }

    if (n <= 0xffffff) {
      // RGB format
      const r = (n >> 16) & 0xff
      const g = (n >> 8) & 0xff
      const b = n & 0xff
      const a = 255
      return [r, g, b, a]
    } else {
      // AARRGGBB format (common in respacks)
      const a = (n >> 24) & 0xff
      const r = (n >> 16) & 0xff
      const g = (n >> 8) & 0xff
      const b = n & 0xff
      return [r, g, b, a]
    }
  } catch {
    return defaultColor
  }
}

/**
 * Parse info.yml (minimal YAML parser)
 */
function parseInfoYml(text: string): RespackInfo {
  const info: any = {}

  // Helper to strip inline comments
  function stripInlineComment(s: string): string {
    let inSq = false
    let inDq = false
    const buf: string[] = []

    for (const ch of s) {
      if (ch === "'" && !inDq) {
        inSq = !inSq
      } else if (ch === '"' && !inSq) {
        inDq = !inDq
      }

      if (!inSq && !inDq && ch === '#') {
        break
      }
      buf.push(ch)
    }

    return buf.join('').trimEnd()
  }

  for (const raw of text.split('\n')) {
    const line = stripInlineComment(raw).trim()

    if (!line || line.startsWith('#')) continue
    if (!line.includes(':')) continue

    const colonIdx = line.indexOf(':')
    const k = line.substring(0, colonIdx).trim()
    let v = line.substring(colonIdx + 1).trim().replace(/^["']|["']$/g, '')

    // Parse array values [a, b, c]
    if (v.startsWith('[') && v.endsWith(']')) {
      const inside = v.substring(1, v.length - 1).trim()
      if (inside) {
        const parts = inside.split(',').map(p => p.trim())
        const arr: any[] = []
        for (const p of parts) {
          const numVal = Number(p)
          if (!isNaN(numVal)) {
            arr.push(numVal)
          } else {
            arr.push(p.replace(/^["']|["']$/g, ''))
          }
        }
        info[k] = arr
      } else {
        info[k] = []
      }
    } else {
      // Parse scalar values
      if (v.toLowerCase() === 'true' || v.toLowerCase() === 'false') {
        info[k] = v.toLowerCase() === 'true'
      } else {
        const numVal = Number(v)
        if (!isNaN(numVal)) {
          info[k] = numVal
        } else {
          info[k] = v
        }
      }
    }
  }

  return info
}

/**
 * Load a texture from a blob
 */
async function loadTexture(blob: Blob): Promise<Texture> {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(blob)
    const img = new Image()

    img.onload = () => {
      try {
        const texture = Texture.from(img)
        URL.revokeObjectURL(url)
        if (!texture || texture.valid === false) {
          reject(new Error('Failed to create valid texture'))
        } else {
          resolve(texture)
        }
      } catch (error) {
        URL.revokeObjectURL(url)
        reject(error)
      }
    }

    img.onerror = () => {
      URL.revokeObjectURL(url)
      reject(new Error('Failed to load image'))
    }

    img.src = url
  })
}

/**
 * Load a sound from a blob
 */
async function loadSound(blob: Blob, alias: string): Promise<any> {
  try {
    const url = URL.createObjectURL(blob)
    const snd = await sound.add(alias, url)
    // Note: Don't revoke URL yet, sound needs it
    return snd
  } catch (error) {
    console.error(`Failed to load sound ${alias}:`, error)
    return null
  }
}

/**
 * Load resource pack from zip file
 */
export async function loadRespack(file: File | Blob): Promise<Respack> {
  const zip = await JSZip.loadAsync(file)

  // Load info.yml
  const infoFile = zip.file('info.yml')
  if (!infoFile) {
    throw new Error('Missing info.yml in respack')
  }
  const infoText = await infoFile.async('text')
  const info = parseInfoYml(infoText)

  // Required image files
  const requiredImages = [
    'click.png',
    'drag.png',
    'flick.png',
    'hold.png',
    'click_mh.png',
    'drag_mh.png',
    'flick_mh.png',
    'hold_mh.png',
    'hit_fx.png',
  ]

  // Load all textures
  const texturePromises: Record<string, Promise<Texture>> = {}
  for (const fileName of requiredImages) {
    const imgFile = zip.file(fileName)
    if (!imgFile) {
      throw new Error(`Missing required image: ${fileName}`)
    }
    texturePromises[fileName] = imgFile.async('blob').then(loadTexture)
  }

  const textureResults = await Promise.all(
    Object.entries(texturePromises).map(async ([name, promise]) => ({
      name,
      texture: await promise,
    }))
  )

  const textures: any = {}
  for (const { name, texture } of textureResults) {
    const key = name.replace('.png', '').replace('-', '_')
    textures[key] = texture
  }

  // Load sound effects (optional)
  const sounds: any = {}
  const soundFiles = [
    ['click.ogg', 'click'],
    ['drag.ogg', 'drag'],
    ['flick.ogg', 'flick'],
  ]

  for (const [fileName, key] of soundFiles) {
    const soundFile = zip.file(fileName)
    if (soundFile) {
      try {
        const blob = await soundFile.async('blob')
        sounds[key] = await loadSound(blob, `respack_${key}`)
      } catch (error) {
        console.warn(`Failed to load sound ${fileName}:`, error)
      }
    }
  }

  // Parse hit effect configuration
  const hitfxFrames = info.hitFx ?? [5, 6]
  const hitfx = {
    frames: [hitfxFrames[0], hitfxFrames[1]] as [number, number],
    duration: info.hitFxDuration ?? 0.5,
    scale: info.hitFxScale ?? 1.0,
    rotate: info.hitFxRotate ?? false,
    tinted: info.hitFxTinted ?? true,
  }

  // Parse hold note configuration
  const holdAtlas = info.holdAtlas ?? [50, 50]
  const holdAtlasMH = info.holdAtlasMH ?? holdAtlas
  const hold = {
    tailH: holdAtlas[0],
    headH: holdAtlas[1],
    tailHMH: holdAtlasMH[0],
    headHMH: holdAtlasMH[1],
    repeat: info.holdRepeat ?? false,
    compact: info.holdCompact ?? false,
    keepHead: info.holdKeepHead ?? false,
    tailNoScale: info.holdTailNoScale ?? false,
  }

  // Parse judge colors
  const judgeColors = {
    PERFECT: parseHexRGBA(info.colorPerfect, [255, 255, 255, 255]),
    GOOD: parseHexRGBA(info.colorGood, [180, 220, 255, 255]),
    BAD: parseHexRGBA(info.colorBad, [255, 180, 180, 255]),
    MISS: parseHexRGBA(info.colorMiss, [200, 200, 200, 255]),
  }

  return {
    info,
    textures,
    sounds,
    hitfx,
    hold,
    hideParticles: info.hideParticles ?? false,
    judgeColors,
  }
}
