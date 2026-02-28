export class AudioClock {
  private media: HTMLAudioElement | null = null;
  private fallbackStartSec = performance.now() / 1000;

  attach(media: HTMLAudioElement | null): void {
    this.media = media;
    this.fallbackStartSec = performance.now() / 1000;
  }

  async play(): Promise<void> {
    if (!this.media) return;
    try {
      await this.media.play();
    } catch {
      // Browser may block autoplay; fallback clock still works.
      this.fallbackStartSec = performance.now() / 1000;
    }
  }

  pause(): void {
    if (this.media) {
      this.media.pause();
    }
  }

  now(): number {
    if (this.media && Number.isFinite(this.media.currentTime) && !this.media.paused) {
      return this.media.currentTime;
    }
    return performance.now() / 1000 - this.fallbackStartSec;
  }
}
