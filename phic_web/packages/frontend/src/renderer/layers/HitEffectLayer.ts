/**
 * Hit effect layer - particle effects on note hits
 * Ported from hitfx.py and frame_renderer.py lines 514-527
 */

import { Container, Graphics, Rectangle, Sprite, Texture } from 'pixi.js'
import type { RenderLayer, FrameState, HitEffectData } from '../types.js'
import type { Respack } from '../../loaders/RespackLoader.js'

interface ActiveEffect extends HitEffectData {
  startTime: number
}

interface ParticleBurst {
  x: number
  y: number
  startMs: number
  durationMs: number
  rgba: [number, number, number, number]
  pa: Array<[number, number]>
}

/**
 * Hit effect layer with particle animations
 */
export class HitEffectLayer implements RenderLayer {
  public container: Container

  private effects: ActiveEffect[] = []
  private effectDuration = 0.3 // seconds
  private graphics: Graphics
  private particleGraphics: Graphics

  private respack: Respack | null = null
  private spriteContainer: Container
  private spritePool: Sprite[] = []
  private activeSprites: Sprite[] = []
  private hitfxFrameCache: Map<number, Texture> = new Map()

  private particleBursts: ParticleBurst[] = []

  constructor() {
    this.container = new Container()
    this.container.label = 'HitEffectLayer'

    this.graphics = new Graphics()
    this.container.addChild(this.graphics)

    this.particleGraphics = new Graphics()
    this.container.addChild(this.particleGraphics)

    this.spriteContainer = new Container()
    this.spriteContainer.label = 'HitEffectSprites'
    this.container.addChild(this.spriteContainer)
  }

  private renderParticleBurst(burst: ParticleBurst, nowMs: number): void {
    const tick = (nowMs - burst.startMs) / Math.max(1, burst.durationMs)
    const t = Math.max(0, Math.min(1, tick))
    const alpha = Math.floor(255 * (1 - t))
    const sizeF = 30 * (((0.2078 * t - 1.6524) * t + 1.6399) * t + 0.4988)
    const size = Math.max(2, Math.floor(sizeF))
    const [r, g, b] = burst.rgba
    const color = (r << 16) | (g << 8) | b
    const a01 = alpha / 255

    for (const [spd, ang] of burst.pa) {
      const dist = (spd * (9 * t / (8 * t + 1))) / 2
      const px = burst.x + dist * Math.cos(ang)
      const py = burst.y + dist * Math.sin(ang)
      this.particleGraphics
        .rect(px - size / 2, py - size / 2, size, size)
        .fill({ color, alpha: a01 })
    }
  }

  public async init(): Promise<void> {
    // Hit effect layer is ready immediately
  }

  /**
   * Add a new hit effect
   */
  public addEffect(
    x: number,
    y: number,
    rotation: number,
    noteKind: number,
    rgba?: [number, number, number, number],
    timeSec?: number
  ): void {
    const t = timeSec ?? performance.now() / 1000
    this.effects.push({
      x,
      y,
      rotation,
      time: t,
      noteKind,
      rgba,
      startTime: t,
    })

    if (this.respack && !this.respack.hideParticles) {
      const nowMs = Math.floor(t * 1000)
      const durMs = Math.max(1, Math.floor(this.respack.hitfx.duration * 1000))
      const rgba0 = rgba ?? [255, 255, 255, 255]
      const count = 4
      const pa: Array<[number, number]> = []
      for (let i = 0; i < count; i++) {
        const spd = 185 + Math.random() * (265 - 185)
        const ang = Math.random() * Math.PI * 2
        pa.push([spd, ang])
      }
      this.particleBursts.push({
        x,
        y,
        startMs: nowMs,
        durationMs: durMs,
        rgba: rgba0,
        pa,
      })
    }
  }

  public setRespack(respack: Respack | null): void {
    this.respack = respack
    this.hitfxFrameCache.clear()
    this.particleBursts = []
    if (this.respack) {
      this.effectDuration = Math.max(1e-6, this.respack.hitfx.duration)
    } else {
      this.effectDuration = 0.3
    }
  }

  public update(state: FrameState): void {
    const now = state.time

    if (this.respack) {
      this.graphics.visible = false
      this.spriteContainer.visible = true
      this.particleGraphics.visible = !this.respack.hideParticles
      this.releaseAllSprites()

      this.particleGraphics.clear()

      // Prune old effects
      this.effects = this.effects.filter(fx => {
        const age = now - fx.startTime
        return age < this.effectDuration
      })

      if (!this.respack.hideParticles) {
        const nowMs = Math.floor(now * 1000)
        this.particleBursts = this.particleBursts.filter(p => nowMs < p.startMs + p.durationMs)
        for (const burst of this.particleBursts) {
          this.renderParticleBurst(burst, nowMs)
        }
      }

      for (const fx of this.effects) {
        this.renderRespackHitfx(fx, now)
      }
      return
    }

    this.spriteContainer.visible = false
    this.graphics.visible = true
    this.particleGraphics.visible = false
    this.graphics.clear()

    // Prune old effects
    this.effects = this.effects.filter(fx => {
      const age = now - fx.startTime
      return age < this.effectDuration
    })

    // Render each active effect
    for (const fx of this.effects) {
      this.renderEffect(fx, now)
    }
  }

  private releaseAllSprites(): void {
    for (const s of this.activeSprites) {
      s.visible = false
      this.spritePool.push(s)
    }
    this.activeSprites = []
  }

  private acquireSprite(texture: Texture): Sprite {
    const s = this.spritePool.pop() ?? new Sprite()
    s.texture = texture
    s.visible = true
    if (!s.parent) {
      this.spriteContainer.addChild(s)
    }
    this.activeSprites.push(s)
    return s
  }

  private renderRespackHitfx(fx: ActiveEffect, currentTime: number): void {
    if (!this.respack) return

    const age = currentTime - fx.startTime
    const dur = Math.max(1e-6, this.respack.hitfx.duration)
    if (age < 0 || age > dur) return

    const sheet = this.respack.textures.hit_fx
    const fw = this.respack.hitfx.frames[0]
    const fh = this.respack.hitfx.frames[1]
    const cellW = Math.floor(sheet.width / fw)
    const cellH = Math.floor(sheet.height / fh)
    if (cellW <= 0 || cellH <= 0) return

    const p = Math.max(0, Math.min(0.999999, age / dur))
    const idx = Math.floor(p * (fw * fh))

    let frameTexture = this.hitfxFrameCache.get(idx)
    if (!frameTexture) {
      const ix = idx % fw
      const iy = Math.floor(idx / fw)
      const frame = new Rectangle(ix * cellW, iy * cellH, cellW, cellH)
      frameTexture = new Texture({ source: sheet.source, frame })
      this.hitfxFrameCache.set(idx, frameTexture)
    }

    const spr = this.acquireSprite(frameTexture)
    spr.anchor.set(0.5, 0.5)
    spr.position.set(fx.x, fx.y)

    const sc = this.respack.hitfx.scale
    spr.scale.set(sc)

    spr.rotation = this.respack.hitfx.rotate ? fx.rotation : 0

    const rgba = fx.rgba ?? [255, 255, 255, 255]
    const [r, g, b, a] = rgba
    if (this.respack.hitfx.tinted || r !== 255 || g !== 255 || b !== 255) {
      spr.tint = (r << 16) | (g << 8) | b
    } else {
      spr.tint = 0xffffff
    }
    spr.alpha = a / 255
  }

  /**
   * Render a single hit effect
   */
  private renderEffect(fx: ActiveEffect, currentTime: number): void {
    const age = currentTime - fx.startTime
    const progress = age / this.effectDuration

    // Fade out over time
    const alpha = 1 - progress

    // Effect colors by note kind
    let color: number
    switch (fx.noteKind) {
      case 1: color = 0x00bfff; break // Tap - light blue
      case 2: color = 0xffff00; break // Drag - yellow
      case 3: color = 0x4ecdc4; break // Hold - cyan
      case 4: color = 0xff69b4; break // Flick - pink
      default: color = 0xffffff; break
    }

    // Expanding ring effect
    const radius = 30 + progress * 40
    const thickness = 4 * (1 - progress)

    this.graphics
      .circle(fx.x, fx.y, radius)
      .stroke({ width: thickness, color, alpha })

    // Particle burst (4 particles)
    const particleCount = 4
    for (let i = 0; i < particleCount; i++) {
      const angle = fx.rotation + (Math.PI * 2 * i) / particleCount
      const distance = progress * 60
      const px = fx.x + Math.cos(angle) * distance
      const py = fx.y + Math.sin(angle) * distance
      const size = 8 * (1 - progress)

      this.graphics
        .rect(px - size / 2, py - size / 2, size, size)
        .fill({ color, alpha })
    }
  }

  public resize(_width: number, _height: number): void {
    // Hit effects are position-absolute, no resize needed
  }

  public destroy(): void {
    this.graphics.destroy()
    this.particleGraphics.destroy()
    for (const s of this.spritePool) {
      s.destroy()
    }
    for (const s of this.activeSprites) {
      s.destroy()
    }
    this.container.destroy()
  }
}
