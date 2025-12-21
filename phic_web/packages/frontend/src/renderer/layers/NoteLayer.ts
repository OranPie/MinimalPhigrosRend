/**
 * Note layer - renders notes with sprite pooling
 * Ported from frame_renderer.py lines 232-512
 */

import { Container, Graphics, Rectangle, Sprite, Texture, TilingSprite } from 'pixi.js'
import type { RenderLayer, FrameState, NoteRenderData } from '../types.js'
import { evalLineState, noteWorldPos } from '../../runtime/kinematics.js'
import type { Respack } from '../../loaders/RespackLoader.js'

/**
 * Sprite pool for efficient note rendering
 * Currently unused - using Graphics directly for simplicity
 * TODO: Implement texture-based sprite rendering
 */
class SpritePool {
  private pool: Sprite[] = []
  private active: Set<Sprite> = new Set()

  /**
   * Get a sprite from the pool (or create new one)
   */
  public acquire(texture?: Texture): Sprite {
    let sprite = this.pool.pop()
    if (!sprite) {
      sprite = new Sprite(texture)
    } else if (texture) {
      sprite.texture = texture
    }

    sprite.visible = true
    this.active.add(sprite)
    return sprite
  }

  /**
   * Return sprite to pool
   */
  public release(sprite: Sprite): void {
    sprite.visible = false
    this.active.delete(sprite)
    this.pool.push(sprite)
  }

  /**
   * Release all active sprites
   */
  public releaseAll(): void {
    for (const sprite of this.active) {
      sprite.visible = false
      this.pool.push(sprite)
    }
    this.active.clear()
  }

  /**
   * Destroy pool and all sprites
   */
  public destroy(): void {
    for (const sprite of this.pool) {
      sprite.destroy()
    }
    for (const sprite of this.active) {
      sprite.destroy()
    }
    this.pool = []
    this.active.clear()
  }
}

class TilingSpritePool {
  private pool: TilingSprite[] = []
  private active: Set<TilingSprite> = new Set()

  public acquire(texture?: Texture): TilingSprite {
    let spr = this.pool.pop()
    if (!spr) {
      spr = new TilingSprite({ texture: texture ?? Texture.EMPTY, width: 1, height: 1 })
    } else if (texture) {
      spr.texture = texture
    }

    spr.visible = true
    this.active.add(spr)
    return spr
  }

  public releaseAll(): void {
    for (const spr of this.active) {
      spr.visible = false
      this.pool.push(spr)
    }
    this.active.clear()
  }

  public destroy(): void {
    for (const spr of this.pool) {
      spr.destroy()
    }
    for (const spr of this.active) {
      spr.destroy()
    }
    this.pool = []
    this.active.clear()
  }
}

/**
 * Note layer with optimized sprite pooling
 */
export class NoteLayer implements RenderLayer {
  public container: Container

  private spritePool = new SpritePool()
  private tilingSpritePool = new TilingSpritePool()
  private graphicsPool: Graphics[] = []
  private activeGraphics: Graphics[] = []

  private respack: Respack | null = null
  private holdSliceCache: Map<string, { head: Texture; mid: Texture; tail: Texture; dims: { imgW: number; imgH: number; headH: number; tailH: number; midH: number } }> = new Map()

  private noteSizeMult: number = 1.0

  private readonly BASE_NOTE_WIDTH = 120
  private readonly BASE_NOTE_HEIGHT = 30
  private readonly MISS_FADE_SEC = 0.35

  constructor() {
    this.container = new Container()
    this.container.label = 'NoteLayer'
  }

  public async init(): Promise<void> {
    // Note layer is ready immediately
  }

  public setRespack(respack: Respack | null): void {
    this.respack = respack
    this.holdSliceCache.clear()
  }

  public setNoteSizeMult(mult: number): void {
    const m = Number(mult)
    this.noteSizeMult = isFinite(m) && m > 0 ? m : 1.0
  }

  public update(state: FrameState): void {
    // Release all sprites and graphics from previous frame
    this.spritePool.releaseAll()
    this.tilingSpritePool.releaseAll()
    for (const graphics of this.activeGraphics) {
      graphics.clear()
      graphics.visible = false
      this.graphicsPool.push(graphics)
    }
    this.activeGraphics = []

    // Gather visible notes
    const noteData: NoteRenderData[] = []
    const searchStart = Math.max(0, state.idxNext - 400)
    const searchEnd = Math.min(state.states.length, state.idxNext + 1200)

    for (let i = searchStart; i < searchEnd; i++) {
      const noteState = state.states[i]
      const note = noteState.note

      // Skip judged non-hold notes
      if (note.kind !== 3 && noteState.judged) {
        // Show miss fade
        if (noteState.miss && noteState.miss_t !== undefined) {
          if (state.time > noteState.miss_t + this.MISS_FADE_SEC) {
            continue
          }
        } else {
          continue
        }
      }

      // Skip finalized holds
      if (note.kind === 3 && noteState.hold_finalized) {
        continue
      }

      // Fake notes should render but are never judged/hit

      // Visibility culling by time
      let tEnter = note.t_enter
      if (!isFinite(tEnter)) tEnter = -1e9
      if (state.time < tEnter) continue

      const tEnd = note.kind === 3 ? note.t_end : note.t_hit
      const extraAfter = note.kind === 3 ? 0.35 : 3.5
      if (state.time > tEnd + extraAfter) continue

      // Calculate world position
      const line = state.lines[note.line_id]
      const lineState = evalLineState(line, state.time)

      const holdKeepHead = Boolean(this.respack?.hold.keepHead)

      if (note.kind === 3) {
        // Hold note - render head and tail
        const scNow = lineState.scroll

        // Match pygame: hold head uses scroll_hit until it reaches the line, then sticks to the line
        // (i.e. head_target_scroll = scroll_hit if sc_now <= scroll_hit else sc_now)
        // Special: when respack.keepHead is enabled and the hold has been hit, force head on the line.
        const keepHead = Boolean(this.respack?.hold.keepHead)
        const hitForDraw = Boolean(noteState.hit) && !note.fake
        const headTargetScroll = (keepHead && hitForDraw)
          ? scNow
          : ((noteState.hit || noteState.holding || state.time >= note.t_hit)
              ? (scNow <= note.scroll_hit ? note.scroll_hit : scNow)
              : note.scroll_hit)

        const [headX, headY] = noteWorldPos(
          lineState.x,
          lineState.y,
          lineState.rot,
          lineState.scroll,
          note,
          headTargetScroll,
          false
          , { holdKeepHead }
        )

        const [tailX, tailY] = noteWorldPos(
          lineState.x,
          lineState.y,
          lineState.rot,
          lineState.scroll,
          note,
          note.scroll_end,
          true
          , { holdKeepHead }
        )

        // Calculate hold progress
        let progress: number | undefined
        if (noteState.hit || noteState.holding || state.time >= note.t_hit) {
          const den = note.scroll_end - note.scroll_hit
          const num = lineState.scroll - note.scroll_hit
          if (Math.abs(den) > 1e-6) {
            progress = Math.max(0, Math.min(1, num / den))
          } else {
            const durT = note.t_end - note.t_hit
            if (durT > 1e-6) {
              progress = Math.max(0, Math.min(1, (state.time - note.t_hit) / durT))
            }
          }
        }

        const noteAlpha01 = Math.max(0, Math.min(1, note.alpha01 ?? 1.0))
        let alpha = noteAlpha01
        if (lineState.alphaRaw < 0) {
          alpha *= Math.max(0, Math.min(1, 1.0 + lineState.alphaRaw))
        }

        // Hold bbox screen culling (match pygame)
        const margin = 120
        const minx = Math.min(headX, tailX)
        const maxx = Math.max(headX, tailX)
        const miny = Math.min(headY, tailY)
        const maxy = Math.max(headY, tailY)
        if (maxx < -margin || minx > state.width + margin || maxy < -margin || miny > state.height + margin) {
          continue
        }

        noteData.push({
          x: headX,
          y: headY,
          tailX,
          tailY,
          rotation: lineState.rot,
          alpha,
          note,
          state: noteState,
          progress,
        })
      } else {
        // Regular note (tap, drag, flick)
        const [x, y] = noteWorldPos(
          lineState.x,
          lineState.y,
          lineState.rot,
          lineState.scroll,
          note,
          note.scroll_hit,
          false
        )

        // Screen culling
        const margin = 120
        if (x < -margin || x > state.width + margin ||
            y < -margin || y > state.height + margin) {
          continue
        }

        const noteAlpha01 = Math.max(0, Math.min(1, note.alpha01 ?? 1.0))
        let alpha = noteAlpha01
        if (lineState.alphaRaw < 0) {
          alpha *= Math.max(0, Math.min(1, 1.0 + lineState.alphaRaw))
        }
        // Apply miss fade
        if (noteState.miss && noteState.miss_t !== undefined) {
          const dt = state.time - noteState.miss_t
          if (dt >= 0) {
            const fade = Math.max(0, Math.min(1, dt / this.MISS_FADE_SEC))
            alpha *= (1 - fade) * 0.65
          }
        }

        noteData.push({
          x,
          y,
          rotation: lineState.rot,
          alpha,
          note,
          state: noteState,
        })
      }
    }

    // Render all visible notes
    for (const data of noteData) {
      if (data.note.kind === 3) {
        this.renderHoldNote(data)
      } else {
        this.renderRegularNote(data)
      }
    }
  }

  private pickNoteTexture(noteKind: number, mh: boolean): Texture | null {
    if (!this.respack) return null

    switch (noteKind) {
      case 1:
        return mh ? this.respack.textures.click_mh : this.respack.textures.click
      case 2:
        return mh ? this.respack.textures.drag_mh : this.respack.textures.drag
      case 3:
        return mh ? this.respack.textures.hold_mh : this.respack.textures.hold
      case 4:
        return mh ? this.respack.textures.flick_mh : this.respack.textures.flick
      default:
        return mh ? this.respack.textures.click_mh : this.respack.textures.click
    }
  }

  private setSpriteTintAndAlpha(sprite: Sprite | TilingSprite, data: NoteRenderData): void {
    const a = Math.max(0, Math.min(1, data.alpha))
    sprite.alpha = a

    const rgb = data.note.tint_rgb
    if (rgb && (rgb[0] !== 255 || rgb[1] !== 255 || rgb[2] !== 255)) {
      sprite.tint = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2]
    } else {
      sprite.tint = 0xffffff
    }
  }

  /**
   * Render a regular note (tap, drag, flick)
   */
  private renderRegularNote(data: NoteRenderData): void {
    const { x, y, alpha, note } = data
    const rotation = data.rotation // Unused for now, but keep for future texture rendering

    if (this.respack) {
      const tex = this.pickNoteTexture(note.kind, Boolean(note.mh))
      if (tex) {
        const spr = this.spritePool.acquire(tex)
        if (!spr.parent) {
          this.container.addChild(spr)
        }

        spr.anchor.set(0.5, 0.5)
        spr.position.set(x, y)
        spr.rotation = rotation

        const targetW = this.BASE_NOTE_WIDTH * this.noteSizeMult * (note.size_px ?? 1.0)
        const sc = targetW / Math.max(1, tex.width)
        spr.scale.set(sc)

        this.setSpriteTintAndAlpha(spr, data)
        return
      }
    }

    // Get or create graphics
    let graphics = this.graphicsPool.pop()
    if (!graphics) {
      graphics = new Graphics()
      this.container.addChild(graphics)
    }

    graphics.visible = true
    this.activeGraphics.push(graphics)

    // Note dimensions
    const width = this.BASE_NOTE_WIDTH * this.noteSizeMult * (note.size_px ?? 1.0)
    const height = this.BASE_NOTE_HEIGHT * this.noteSizeMult * (note.size_px ?? 1.0)

    // Note colors (use tint_rgb when available)
    const rgb = note.tint_rgb
    let color: number
    if (rgb && rgb.length === 3) {
      color = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2]
    } else {
      switch (note.kind) {
        case 1: color = 0x00bfff; break // Tap - light blue
        case 2: color = 0xffff00; break // Drag - yellow
        case 4: color = 0xff69b4; break // Flick - pink
        default: color = 0xffffff; break
      }
    }

    // Draw filled rectangle
    graphics.clear()
    graphics.rect(-width / 2, -height / 2, width, height)
    graphics.fill({ color, alpha })

    // Draw outline
    graphics.rect(-width / 2, -height / 2, width, height)
    graphics.stroke({ width: 2, color: 0x000000, alpha: alpha * 0.86 })

    // Apply transform
    graphics.position.set(x, y)
    graphics.rotation = rotation
  }

  /**
   * Render a hold note (3-slice: head, body, tail)
   */
  private renderHoldNote(data: NoteRenderData): void {
    const { x, y, tailX, tailY, alpha, note, progress } = data
    // rotation reserved for future texture-based rendering

    if (tailX === undefined || tailY === undefined) return

    if (this.respack) {
      const tex = this.pickNoteTexture(3, Boolean(note.mh))
      if (tex) {
        this.renderHoldNoteRespack(data, tex)
        return
      }
    }

    // Get or create graphics
    let graphics = this.graphicsPool.pop()
    if (!graphics) {
      graphics = new Graphics()
      this.container.addChild(graphics)
    }

    graphics.visible = true
    this.activeGraphics.push(graphics)

    const width = this.BASE_NOTE_WIDTH * (note.size_px ?? 1.0)
    const height = this.BASE_NOTE_HEIGHT * (note.size_px ?? 1.0)
    const bodyWidth = 20

    // Calculate distance between head and tail
    const dx = tailX - x
    const dy = tailY - y
    const length = Math.sqrt(dx * dx + dy * dy)

    // Hold color
    const color = note.mh ? 0xff6b6b : 0x4ecdc4 // Red for multi-hold, cyan for normal

    graphics.clear()

    // Draw body (connecting line)
    if (length > height) {
      const bodyAlpha = data.state.hold_failed ? alpha * 0.35 : alpha
      graphics.moveTo(x, y)
      graphics.lineTo(tailX, tailY)
      graphics.stroke({ width: bodyWidth, color, alpha: bodyAlpha })

      // Draw progress indicator if holding
      if (progress !== undefined && progress > 0) {
        const progX = x + dx * progress
        const progY = y + dy * progress
        graphics.moveTo(x, y)
        graphics.lineTo(progX, progY)
        graphics.stroke({ width: bodyWidth + 4, color: 0xffffff, alpha: bodyAlpha * 0.5 })
      }
    }

    // Draw head
    graphics.rect(x - width / 2, y - height / 2, width, height)
    graphics.fill({ color, alpha })
    graphics.rect(x - width / 2, y - height / 2, width, height)
    graphics.stroke({ width: 2, color: 0x000000, alpha: alpha * 0.86 })

    // Draw tail
    graphics.rect(tailX - width / 2, tailY - height / 2, width, height)
    graphics.fill({ color, alpha: alpha * 0.7 })
    graphics.rect(tailX - width / 2, tailY - height / 2, width, height)
    graphics.stroke({ width: 2, color: 0x000000, alpha: alpha * 0.6 })
  }

  private renderHoldNoteRespack(data: NoteRenderData, holdTexture: Texture): void {
    if (!this.respack) return

    const { x, y, tailX, tailY, note, progress } = data
    if (tailX === undefined || tailY === undefined) return

    const dx = tailX - x
    const dy = tailY - y
    const length = Math.sqrt(dx * dx + dy * dy)
    if (length < 1e-3) return

    const angle = Math.atan2(dy, dx)
    const rot = angle - Math.PI / 2
    const uX = dx / length
    const uY = dy / length

    const mh = Boolean(note.mh)
    const cacheKey = `${mh ? 'mh' : 'n'}:${holdTexture.uid}`
    let slices = this.holdSliceCache.get(cacheKey)
    if (!slices) {
      const imgW = Math.max(1, holdTexture.width)
      const imgH = Math.max(1, holdTexture.height)
      const tailH = mh ? this.respack.hold.tailHMH : this.respack.hold.tailH
      const headH = mh ? this.respack.hold.headHMH : this.respack.hold.headH
      const midH = Math.max(1, imgH - headH - tailH)

      const head = new Texture({
        source: holdTexture.source,
        frame: new Rectangle(0, 0, imgW, headH),
      })
      const mid = new Texture({
        source: holdTexture.source,
        frame: new Rectangle(0, headH, imgW, midH),
      })
      const tail = new Texture({
        source: holdTexture.source,
        frame: new Rectangle(0, headH + midH, imgW, tailH),
      })

      slices = { head, mid, tail, dims: { imgW, imgH, headH, tailH, midH } }
      this.holdSliceCache.set(cacheKey, slices)
    }

    const { imgW, headH, tailH, midH } = slices.dims
    const headSrc = slices.head
    const midSrc = slices.mid
    const tailSrc = slices.tail

    const targetW = this.BASE_NOTE_WIDTH * this.noteSizeMult * (note.size_px ?? 1.0)
    const scaleX = targetW / imgW

    const keepHead = this.respack.hold.keepHead
    const hideHeadNow = progress !== undefined && !keepHead

    const headLen = hideHeadNow ? 0 : headH * scaleX
    const tailLen = tailH * scaleX

    // Match pygame: when geometric length is short, clamp head/tail draw lengths to avoid abrupt disappearance
    const headDrawLen = hideHeadNow ? 0 : Math.min(length, headLen)
    const tailDrawLen = Math.min(length, tailLen)
    const y0Mid = hideHeadNow ? 0 : headDrawLen
    const y1Mid = Math.max(0, length - tailDrawLen)
    const midLen = Math.max(0, y1Mid - y0Mid)

    // Head
    if (!hideHeadNow) {
      const headSpr = this.spritePool.acquire(headSrc)
      if (!headSpr.parent) this.container.addChild(headSpr)
      headSpr.anchor.set(0.5, 0)
      headSpr.position.set(x, y)
      headSpr.rotation = rot
      headSpr.scale.set(scaleX, headH > 0 ? (headDrawLen / headH) : scaleX)
      this.setSpriteTintAndAlpha(headSpr, data)
    }

    // Mid
    if (midLen > 0.5) {
      if (this.respack.hold.repeat) {
        const midSpr = this.tilingSpritePool.acquire(midSrc)
        if (!midSpr.parent) this.container.addChild(midSpr)
        midSpr.anchor.set(0.5, 0)
        midSpr.position.set(x + uX * y0Mid, y + uY * y0Mid)
        midSpr.rotation = rot
        midSpr.width = targetW
        midSpr.height = midLen
        midSpr.tileScale.set(scaleX, scaleX)
        this.setSpriteTintAndAlpha(midSpr, data)
      } else {
        const midSpr = this.spritePool.acquire(midSrc)
        if (!midSpr.parent) this.container.addChild(midSpr)
        midSpr.anchor.set(0.5, 0)
        midSpr.position.set(x + uX * y0Mid, y + uY * y0Mid)
        midSpr.rotation = rot
        midSpr.scale.set(scaleX, midLen / Math.max(1, midH))
        this.setSpriteTintAndAlpha(midSpr, data)
      }
    }

    // Tail
    const tailSpr = this.spritePool.acquire(tailSrc)
    if (!tailSpr.parent) this.container.addChild(tailSpr)
    tailSpr.anchor.set(0.5, 1)
    tailSpr.position.set(tailX, tailY)
    tailSpr.rotation = rot
    tailSpr.scale.set(scaleX, tailH > 0 ? (tailDrawLen / tailH) : scaleX)
    this.setSpriteTintAndAlpha(tailSpr, data)
  }

  public resize(_width: number, _height: number): void {
    // Note layer is resolution-independent
  }

  public destroy(): void {
    this.spritePool.destroy()
    this.tilingSpritePool.destroy()
    for (const graphics of this.graphicsPool) {
      graphics.destroy()
    }
    for (const graphics of this.activeGraphics) {
      graphics.destroy()
    }
    this.container.destroy()
  }
}
