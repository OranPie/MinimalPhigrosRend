/**
 * UI layer - score, combo, accuracy display
 * Ported from ui_rendering.py
 */

import { Container, Text, TextStyle } from 'pixi.js'
import type { RenderLayer, FrameState } from '../types.js'

/**
 * UI layer for score, combo, and accuracy
 */
export class UILayer implements RenderLayer {
  public container: Container

  private scoreText: Text
  private comboText: Text
  private accuracyText: Text

  constructor(
    private width: number,
    private height: number
  ) {
    this.container = new Container()
    this.container.label = 'UILayer'

    // Score text (top center)
    this.scoreText = new Text({
      text: '0',
      style: new TextStyle({
        fontFamily: 'Arial',
        fontSize: 48,
        fontWeight: 'bold',
        fill: 0xffffff,
        stroke: { color: 0x000000, width: 4 },
        dropShadow: {
          color: 0x000000,
          blur: 4,
          angle: Math.PI / 4,
          distance: 2,
        },
      }),
    })
    this.scoreText.anchor.set(0.5, 0)
    this.container.addChild(this.scoreText)

    // Combo text (center)
    this.comboText = new Text({
      text: '',
      style: new TextStyle({
        fontFamily: 'Arial',
        fontSize: 72,
        fontWeight: 'bold',
        fill: 0xffff00,
        stroke: { color: 0x000000, width: 5 },
        dropShadow: {
          color: 0x000000,
          blur: 6,
          angle: Math.PI / 4,
          distance: 3,
        },
      }),
    })
    this.comboText.anchor.set(0.5, 0.5)
    this.container.addChild(this.comboText)

    // Accuracy text (top right)
    this.accuracyText = new Text({
      text: '100.00%',
      style: new TextStyle({
        fontFamily: 'Arial',
        fontSize: 32,
        fontWeight: 'bold',
        fill: 0x00ff00,
        stroke: { color: 0x000000, width: 3 },
        dropShadow: {
          color: 0x000000,
          blur: 3,
          angle: Math.PI / 4,
          distance: 2,
        },
      }),
    })
    this.accuracyText.anchor.set(1, 0)
    this.container.addChild(this.accuracyText)

    this.updatePositions()
  }

  public async init(): Promise<void> {
    // UI layer is ready immediately
  }

  /**
   * Update UI stats
   */
  public updateStats(score: number, combo: number, accuracy: number): void {
    // Update text content
    this.scoreText.text = String(Math.floor(score)).padStart(7, '0')

    if (combo > 0) {
      this.comboText.text = String(combo)
      this.comboText.visible = true
    } else {
      this.comboText.visible = false
    }

    this.accuracyText.text = `${accuracy.toFixed(2)}%`

    // Update accuracy color based on value
    if (accuracy >= 99.0) {
      this.accuracyText.style.fill = 0x00ff00 // Green
    } else if (accuracy >= 95.0) {
      this.accuracyText.style.fill = 0xffff00 // Yellow
    } else if (accuracy >= 90.0) {
      this.accuracyText.style.fill = 0xffa500 // Orange
    } else {
      this.accuracyText.style.fill = 0xff0000 // Red
    }
  }

  /**
   * Update text positions based on screen size
   */
  private updatePositions(): void {
    this.scoreText.x = this.width / 2
    this.scoreText.y = 20

    this.comboText.x = this.width / 2
    this.comboText.y = this.height / 2

    this.accuracyText.x = this.width - 20
    this.accuracyText.y = 20
  }

  public update(_state: FrameState): void {
    // UI is updated via updateStats(), not per-frame
  }

  public resize(width: number, height: number): void {
    this.width = width
    this.height = height
    this.updatePositions()
  }

  public destroy(): void {
    this.scoreText.destroy()
    this.comboText.destroy()
    this.accuracyText.destroy()
    this.container.destroy()
  }
}
