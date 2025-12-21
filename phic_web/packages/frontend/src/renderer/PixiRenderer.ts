/**
 * Main PixiJS renderer orchestrating all layers
 * Ported from phic_renderer/renderer/pygame_backend.py
 */

import { Application } from 'pixi.js'
import type { RuntimeLine, NoteState } from '@phic-web/shared'
import type { RendererConfig, FrameState, RenderLayer } from './types.js'
import type { Respack } from '../loaders/RespackLoader.js'
import { BackgroundLayer } from './layers/BackgroundLayer.js'
import { LineLayer } from './layers/LineLayer.js'
import { NoteLayer } from './layers/NoteLayer.js'
import { HitEffectLayer } from './layers/HitEffectLayer.js'
import { UILayer } from './layers/UILayer.js'

/**
 * PixiJS-based renderer with layered architecture
 */
export class PixiRenderer {
  private app: Application
  private layers: RenderLayer[] = []
  private initialized = false

  // Individual layers
  private backgroundLayer!: BackgroundLayer
  private lineLayer!: LineLayer
  private noteLayer!: NoteLayer
  private hitEffectLayer!: HitEffectLayer
  private uiLayer!: UILayer

  constructor(
    private canvas: HTMLCanvasElement,
    private config: RendererConfig
  ) {
    // Create PixiJS application
    this.app = new Application()
  }

  /**
   * Initialize renderer and all layers
   */
  public async init(): Promise<void> {
    if (this.initialized) return

    // Initialize PixiJS application
    await this.app.init({
      canvas: this.canvas,
      width: this.config.width,
      height: this.config.height,
      backgroundColor: this.config.backgroundColor ?? 0x0a0a0e,
      antialias: this.config.antialias ?? true,
      resolution: this.config.resolution ?? (window.devicePixelRatio || 1),
      autoDensity: true,
      backgroundAlpha: this.config.backgroundAlpha ?? 1,
    })

    // Create layers in bottom-to-top order
    this.backgroundLayer = new BackgroundLayer()
    this.lineLayer = new LineLayer()
    this.noteLayer = new NoteLayer()
    this.hitEffectLayer = new HitEffectLayer()
    this.uiLayer = new UILayer(this.config.width, this.config.height)

    this.layers = [
      this.backgroundLayer,
      this.lineLayer,
      this.noteLayer,
      this.hitEffectLayer,
      this.uiLayer,
    ]

    // Add layer containers to stage
    for (const layer of this.layers) {
      this.app.stage.addChild(layer.container)
    }

    // Initialize all layers
    await Promise.all(this.layers.map(layer => layer.init()))

    this.initialized = true
  }

  /**
   * Render a single frame
   */
  public render(
    time: number,
    lines: RuntimeLine[],
    states: NoteState[],
    idxNext: number
  ): void {
    if (!this.initialized) {
      console.warn('PixiRenderer: render() called before init()')
      return
    }

    const frameState: FrameState = {
      time,
      lines,
      states,
      idxNext,
      width: this.config.width,
      height: this.config.height,
    }

    // Update all layers
    for (const layer of this.layers) {
      layer.update(frameState)
    }

    // PixiJS automatically renders the stage
  }

  /**
   * Set background image
   */
  public async setBackground(imageUrl: string, blurRadius = 10, dimAlpha = 0.5): Promise<void> {
    await this.backgroundLayer.setBackground(imageUrl, blurRadius, dimAlpha, this.config.width, this.config.height)
  }

  /**
   * Set resource pack for custom textures and effects
   */
  public setRespack(respack: Respack | null): void {
    this.noteLayer.setRespack(respack)
    this.hitEffectLayer.setRespack(respack)
  }

  public setNoteSizeMult(mult: number): void {
    this.noteLayer.setNoteSizeMult(mult)
  }

  /**
   * Add hit effect at position
   */
  public addHitEffect(
    x: number,
    y: number,
    rotation: number,
    noteKind: number,
    rgba?: [number, number, number, number],
    timeSec?: number
  ): void {
    this.hitEffectLayer.addEffect(x, y, rotation, noteKind, rgba, timeSec)
  }

  /**
   * Update UI stats
   */
  public updateUI(score: number, combo: number, accuracy: number): void {
    this.uiLayer.updateStats(score, combo, accuracy)
  }

  /**
   * Resize renderer
   */
  public resize(width: number, height: number): void {
    this.config.width = width
    this.config.height = height
    this.app.renderer.resize(width, height)

    for (const layer of this.layers) {
      layer.resize(width, height)
    }
  }

  /**
   * Destroy renderer and clean up resources
   */
  public destroy(): void {
    for (const layer of this.layers) {
      layer.destroy()
    }
    this.app.destroy(true, { children: true, texture: true })
    this.initialized = false
  }

  /**
   * Get PixiJS application instance (for advanced usage)
   */
  public getApp(): Application {
    return this.app
  }
}
