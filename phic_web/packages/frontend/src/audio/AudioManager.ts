/**
 * Audio manager using Web Audio API and @pixi/sound
 * Ported from phic_renderer/audio/base.py
 */

import { sound } from '@pixi/sound'

/**
 * Audio manager for BGM and hitsounds
 */
export class AudioManager {
  private audioContext: AudioContext | null = null
  private bgmSound: any | null = null
  private bgmStartTime: number = 0
  private bgmPauseTime: number = 0
  private isPaused: boolean = false

  constructor() {
    // Initialize @pixi/sound
    sound.init()
  }

  /**
   * Initialize audio context (must be called after user interaction)
   */
  public async init(): Promise<void> {
    if (!this.audioContext) {
      this.audioContext = new AudioContext()
    }

    // Resume if suspended (browser autoplay policy)
    if (this.audioContext.state === 'suspended') {
      await this.audioContext.resume()
    }
  }

  /**
   * Play music file (BGM)
   * @param path - URL to music file
   * @param volume - Volume (0-1)
   * @param startPosSec - Start position in seconds
   */
  public async playMusicFile(
    path: string,
    volume: number = 0.8,
    startPosSec: number = 0
  ): Promise<void> {
    await this.init()

    // Stop current music if playing
    this.stopMusic()

    try {
      // Load and play with @pixi/sound
      this.bgmSound = await sound.add('bgm', path)

      this.bgmSound.play({
        volume,
        start: startPosSec,
        complete: () => {
          this.bgmSound = null
        },
      })

      this.bgmStartTime = this.audioContext!.currentTime - startPosSec
      this.isPaused = false
    } catch (error) {
      console.error('Failed to play music:', error)
    }
  }

  /**
   * Stop music
   */
  public stopMusic(): void {
    if (this.bgmSound) {
      this.bgmSound.stop()
      this.bgmSound = null
    }
    this.bgmStartTime = 0
    this.bgmPauseTime = 0
    this.isPaused = false
  }

  /**
   * Pause music
   */
  public pauseMusic(): void {
    if (this.bgmSound && !this.isPaused) {
      this.bgmSound.pause()
      this.bgmPauseTime = this.getCurrentTime()
      this.isPaused = true
    }
  }

  /**
   * Unpause music
   */
  public unpauseMusic(): void {
    if (this.bgmSound && this.isPaused) {
      this.bgmSound.resume()
      // Adjust start time to account for pause duration
      const pauseDuration = this.audioContext!.currentTime - this.bgmPauseTime
      this.bgmStartTime += pauseDuration
      this.isPaused = false
    }
  }

  /**
   * Get current music position in seconds
   */
  public getMusicPosition(): number | null {
    if (!this.bgmSound || !this.audioContext) return null

    if (this.isPaused) {
      return this.bgmPauseTime
    }

    return this.audioContext.currentTime - this.bgmStartTime
  }

  /**
   * Get current audio context time (high precision)
   */
  public getCurrentTime(): number {
    return this.audioContext?.currentTime ?? 0
  }

  /**
   * Load a sound effect
   */
  public async loadSound(path: string, alias?: string): Promise<any> {
    try {
      const soundAlias = alias ?? path
      return await sound.add(soundAlias, path)
    } catch (error) {
      console.error('Failed to load sound:', path, error)
      return null
    }
  }

  /**
   * Play a sound effect
   */
  public playSound(soundOrPath: any | string, volume: number = 1.0): any {
    try {
      if (typeof soundOrPath === 'string') {
        // Path provided, play directly
        const s = sound.find(soundOrPath)
        if (s) {
          return s.play({ volume })
        }
      } else if (soundOrPath) {
        // Sound object provided
        return soundOrPath.play({ volume })
      }
    } catch (error) {
      console.error('Failed to play sound:', error)
    }
    return null
  }

  /**
   * Stop all sounds
   */
  public stopAllSounds(): void {
    sound.stopAll()
  }

  /**
   * Set global volume
   */
  public setGlobalVolume(volume: number): void {
    sound.volumeAll = volume
  }

  /**
   * Destroy audio manager
   */
  public destroy(): void {
    this.stopMusic()
    this.stopAllSounds()
    sound.removeAll()

    if (this.audioContext) {
      this.audioContext.close()
      this.audioContext = null
    }
  }
}
