/**
 * Judgment line layer - renders judgment lines with animations
 * Ported from frame_renderer.py lines 108-224
 */

import { Container, Graphics, Text, Sprite, Texture } from 'pixi.js'
import type { RenderLayer, FrameState, LineRenderData } from '../types.js'
import { evalLineState } from '../../runtime/kinematics.js'

/**
 * Line layer rendering judgment lines
 */
export class LineLayer implements RenderLayer {
  public container: Container

  private lineGraphics: Map<number, Graphics> = new Map()
  private lineTexts: Map<number, Text> = new Map()
  private lineTextures: Map<number, Sprite> = new Map()
  private textureCache: Map<string, Texture> = new Map()

  constructor() {
    this.container = new Container()
    this.container.label = 'LineLayer'
  }

  public async init(): Promise<void> {
    // Line layer is ready immediately
  }

  public update(state: FrameState): void {
    // Evaluate all line states
    const lineData: LineRenderData[] = []

    for (const line of state.lines) {
      const lineState = evalLineState(line, state.time)

      // Skip invisible lines
      if (lineState.alpha01 <= 0.001) continue

      lineData.push({
        x: lineState.x,
        y: lineState.y,
        rotation: lineState.rot,
        alpha: lineState.alpha01,
        scroll: lineState.scroll,
        line,
      })
    }

    // Clear all existing graphics
    for (const graphics of this.lineGraphics.values()) {
      graphics.clear()
      graphics.visible = false
    }
    for (const text of this.lineTexts.values()) {
      text.visible = false
    }
    for (const sprite of this.lineTextures.values()) {
      sprite.visible = false
    }

    // Render each visible line
    for (const data of lineData) {
      this.renderLine(data, state.time, state.width)
    }
  }

  /**
   * Render a single judgment line
   */
  private renderLine(data: LineRenderData, time: number, viewportW: number): void {
    const { x, y, rotation, alpha, line } = data
    const lid = line.lid

    // Get or create graphics for this line
    let graphics = this.lineGraphics.get(lid)
    if (!graphics) {
      graphics = new Graphics()
      this.lineGraphics.set(lid, graphics)
      this.container.addChild(graphics)
    }

    graphics.visible = true

    // Line dimensions (match pygame_backend.py: line_len = 6.75 * W)
    const lineLength = 6.75 * viewportW
    const lineWidth = 4

    // Get line scale if available
    let scaleX = 1.0
    if (line.scale_x) {
      scaleX = typeof line.scale_x === 'function'
        ? line.scale_x(time)
        : line.scale_x.eval(time)
    }
    // scaleY available but not currently used for line rendering

    // Draw line
    const halfLen = (lineLength * scaleX) / 2
    const cos = Math.cos(rotation)
    const sin = Math.sin(rotation)

    const x1 = x - cos * halfLen
    const y1 = y - sin * halfLen
    const x2 = x + cos * halfLen
    const y2 = y + sin * halfLen

    // Line color with alpha
    const color = line.color_rgb
    const lineColor = (color[0] << 16) | (color[1] << 8) | color[2]

    graphics.clear()
    graphics
      .moveTo(x1, y1)
      .lineTo(x2, y2)
      .stroke({ width: lineWidth, color: lineColor, alpha })

    // Draw center dot
    graphics
      .circle(x, y, 6)
      .fill({ color: lineColor, alpha: alpha * 0.86 })

    // Render text overlay if present
    if (line.text) {
      let textContent = ''
      try {
        textContent = typeof line.text === 'function'
          ? String(line.text(time))
          : String(line.text.eval(time))
      } catch {
        textContent = ''
      }

      if (textContent) {
        let textObj = this.lineTexts.get(lid)
        if (!textObj) {
          textObj = new Text({
            text: textContent,
            style: {
              fontFamily: 'Arial',
              fontSize: 16,
              fill: 0xffffff,
            },
          })
          this.lineTexts.set(lid, textObj)
          this.container.addChild(textObj)
        }

        textObj.text = textContent
        textObj.alpha = alpha
        textObj.x = x
        textObj.y = y
        textObj.visible = true
      }
    }

    // TODO: Render texture overlay if present (line.texture_path)
    // This would require loading textures dynamically
  }

  public resize(_width: number, _height: number): void {
    // Line layer is resolution-independent
  }

  public destroy(): void {
    for (const graphics of this.lineGraphics.values()) {
      graphics.destroy()
    }
    for (const text of this.lineTexts.values()) {
      text.destroy()
    }
    for (const sprite of this.lineTextures.values()) {
      sprite.destroy()
    }
    for (const texture of this.textureCache.values()) {
      texture.destroy()
    }
    this.container.destroy()
  }
}
