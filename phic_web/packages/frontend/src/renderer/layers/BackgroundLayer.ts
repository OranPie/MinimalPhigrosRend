/**
 * Background layer - blurred background image with dim overlay
 * Ported from frame_renderer.py lines 71-88
 */

import { Container, Sprite, Graphics, BlurFilter, Texture } from 'pixi.js'
import type { RenderLayer, FrameState } from '../types.js'

/**
 * Background layer with blur and dim effects
 */
export class BackgroundLayer implements RenderLayer {
  public container: Container

  private bgSprite: Sprite | null = null
  private dimOverlay: Graphics
  private blurFilter: BlurFilter | null = null
  private dimAlpha: number = 0.5

  constructor() {
    this.container = new Container()
    this.container.label = 'BackgroundLayer'

    // Create dim overlay
    this.dimOverlay = new Graphics()
    this.container.addChild(this.dimOverlay)
  }

  public async init(): Promise<void> {
    // Background layer is initialized lazily when setBackground() is called
  }

  /**
   * Set background image with blur and dim
   */
  public async setBackground(
    imageUrl: string,
    blurRadius: number = 10,
    dimAlpha: number = 0.5,
    width?: number,
    height?: number
  ): Promise<void> {
    // Load background texture using Image element (works with blob URLs)
    const texture = await this.loadTexture(imageUrl)

    // Remove old sprite if exists
    if (this.bgSprite) {
      this.bgSprite.destroy()
    }

    // Create background sprite
    this.bgSprite = new Sprite(texture)
    this.container.addChildAt(this.bgSprite, 0) // Always at bottom

    // Apply blur filter
    if (blurRadius > 0) {
      this.blurFilter = new BlurFilter({
        strength: blurRadius,
        quality: 4,
      })
      this.bgSprite.filters = [this.blurFilter]
    }

    // Update dim overlay
    this.updateDimOverlay(dimAlpha)

    // Resize to fit screen if dimensions provided
    if (width !== undefined && height !== undefined) {
      this.resize(width, height)
    }
  }

  /**
   * Load texture from URL (supports blob URLs)
   */
  private async loadTexture(url: string): Promise<Texture> {
    return new Promise((resolve, reject) => {
      const img = new Image()
      img.crossOrigin = 'anonymous'

      img.onload = () => {
        try {
          const texture = Texture.from(img)
          resolve(texture)
        } catch (error) {
          reject(error)
        }
      }

      img.onerror = () => {
        reject(new Error(`Failed to load image: ${url}`))
      }

      img.src = url
    })
  }

  /**
   * Update dim overlay alpha
   */
  private updateDimOverlay(alpha: number): void {
    if (!this.bgSprite || !this.bgSprite.texture) return

    this.dimAlpha = alpha

    const width = this.bgSprite.texture.width
    const height = this.bgSprite.texture.height

    this.dimOverlay.clear()
    this.dimOverlay
      .rect(0, 0, width, height)
      .fill({ color: 0x000000, alpha })
  }

  /**
   * Resize background to fit new dimensions
   */
  public resize(width: number, height: number): void {
    if (!this.bgSprite || !this.bgSprite.texture) return

    // Scale background to cover entire screen
    const texture = this.bgSprite.texture
    const scaleX = width / texture.width
    const scaleY = height / texture.height
    const scale = Math.max(scaleX, scaleY)

    this.bgSprite.scale.set(scale)

    // Center background
    this.bgSprite.x = (width - texture.width * scale) / 2
    this.bgSprite.y = (height - texture.height * scale) / 2

    // Resize dim overlay
    this.dimOverlay.clear()
    this.dimOverlay
      .rect(0, 0, width, height)
      .fill({ color: 0x000000, alpha: this.dimAlpha })
  }

  public update(_state: FrameState): void {
    // Background is static, no per-frame updates needed
  }

  public destroy(): void {
    if (this.bgSprite) {
      this.bgSprite.destroy({ texture: true })
    }
    this.dimOverlay.destroy()
    this.container.destroy()
  }
}
