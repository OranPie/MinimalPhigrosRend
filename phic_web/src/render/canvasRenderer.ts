import type { FrameCommand } from "../wasm/engine";
import type { JudgeEvent } from "../wasm/engine";
import type { LoadedRespack } from "../assets/respack";

export interface CanvasVisualConfig {
  approachSec: number;
  noteFlowSpeedMultiplier: number;
  laneCount: number;
  noteScaleX: number;
  noteScaleY: number;
  expand: number;
  overrender: number;
  trailAlpha: number;
  bgDim: number;
  hitfxScaleMul: number;
  fontSizeMultiplier: number;
  noteOutline: boolean;
  multicolorLines: boolean;
  noTitleOverlay: boolean;
}

const DEFAULT_VIS: CanvasVisualConfig = {
  approachSec: 3,
  noteFlowSpeedMultiplier: 1,
  laneCount: 8,
  noteScaleX: 1,
  noteScaleY: 1,
  expand: 1,
  overrender: 2,
  trailAlpha: 0,
  bgDim: 120,
  hitfxScaleMul: 1,
  fontSizeMultiplier: 1,
  noteOutline: false,
  multicolorLines: false,
  noTitleOverlay: false
};

export class CanvasRenderer {
  private readonly ctx: CanvasRenderingContext2D;
  private readonly canvas: HTMLCanvasElement;
  private vis: CanvasVisualConfig = { ...DEFAULT_VIS };
  private hudText = "";
  private respack: LoadedRespack | null = null;
  private judgeFx: Array<{ lane: number; ttl: number; duration: number; kind: number; color: string; scale: number; angle: number }> = [];

  constructor(canvas: HTMLCanvasElement) {
    const ctx = canvas.getContext("2d");
    if (!ctx) throw new Error("Canvas2D context unavailable");
    this.ctx = ctx;
    this.canvas = canvas;
  }

  resize(width: number, height: number): void {
    this.canvas.width = width;
    this.canvas.height = height;
  }

  setVisualConfig(next: Partial<CanvasVisualConfig>): void {
    this.vis = {
      ...this.vis,
      ...next,
      approachSec: Math.max(0.05, next.approachSec ?? this.vis.approachSec),
      noteFlowSpeedMultiplier: Math.max(0.05, next.noteFlowSpeedMultiplier ?? this.vis.noteFlowSpeedMultiplier),
      laneCount: Math.max(1, Math.floor(next.laneCount ?? this.vis.laneCount)),
      noteScaleX: Math.max(0.05, next.noteScaleX ?? this.vis.noteScaleX),
      noteScaleY: Math.max(0.05, next.noteScaleY ?? this.vis.noteScaleY),
      expand: Math.max(1, next.expand ?? this.vis.expand),
      overrender: Math.max(1, next.overrender ?? this.vis.overrender),
      trailAlpha: Math.max(0, Math.min(1, next.trailAlpha ?? this.vis.trailAlpha)),
      bgDim: Math.max(0, Math.min(255, next.bgDim ?? this.vis.bgDim)),
      hitfxScaleMul: Math.max(0.05, next.hitfxScaleMul ?? this.vis.hitfxScaleMul),
      fontSizeMultiplier: Math.max(0.1, next.fontSizeMultiplier ?? this.vis.fontSizeMultiplier)
    };
  }

  setHudText(text: string): void {
    this.hudText = text;
  }

  setRespack(pack: LoadedRespack | null): void {
    this.respack = pack;
  }

  pushJudgeEvents(events: JudgeEvent[]): void {
    if (this.respack?.hideParticles) {
      return;
    }
    const duration = Math.max(0.05, this.respack?.hitfxDuration ?? 0.22);
    for (const ev of events) {
      const color = this.pickJudgeColor(ev.kind);
      const scale = ev.kind === 1 ? 1 : ev.kind === 2 ? 0.85 : ev.kind === 3 ? 0.75 : 0.7;
      const angle = this.respack?.hitfxRotate ? Math.random() * Math.PI * 2 : 0;
      this.judgeFx.push({ lane: ev.lane, ttl: duration, duration, kind: ev.kind, color, scale, angle });
    }
    if (this.judgeFx.length > 200) {
      this.judgeFx.splice(0, this.judgeFx.length - 200);
    }
  }

  draw(commands: FrameCommand[], timeSec = 0): void {
    const w = this.canvas.width;
    const h = this.canvas.height;
    const expand = this.vis.expand;
    const laneCount = this.vis.laneCount;
    const laneColor = (idx: number): string => {
      if (!this.vis.multicolorLines) return "#30445f";
      const hue = Math.round((idx / Math.max(1, laneCount)) * 300);
      return `hsl(${hue} 70% 55%)`;
    };
    const bg = Math.max(0, 28 - this.vis.bgDim * 0.09);

    if (this.vis.trailAlpha <= 0.001) {
      this.ctx.fillStyle = `rgb(${bg * 0.4}, ${bg * 0.7}, ${bg})`;
      this.ctx.fillRect(0, 0, w, h);
    } else {
      this.ctx.fillStyle = `rgba(${bg * 0.4}, ${bg * 0.7}, ${bg}, ${1 - this.vis.trailAlpha})`;
      this.ctx.fillRect(0, 0, w, h);
    }

    this.ctx.lineWidth = 1;
    for (let lane = 0; lane < laneCount; lane += 1) {
      this.ctx.strokeStyle = laneColor(lane);
      const x = ((lane + 1) / (laneCount + 1)) * w;
      this.ctx.beginPath();
      this.ctx.moveTo(x, 0);
      this.ctx.lineTo(x, h);
      this.ctx.stroke();
    }

    const margin = (this.vis.overrender - 1) * 0.5;
    const pickNoteImage = (kind: number, mh: boolean): HTMLImageElement | undefined => {
      if (!this.respack) return undefined;
      if (kind === 2) return this.respack.images[mh ? "drag_mh.png" : "drag.png"];
      if (kind === 3) return this.respack.images[mh ? "hold_mh.png" : "hold.png"];
      if (kind === 4) return this.respack.images[mh ? "flick_mh.png" : "flick.png"];
      return this.respack.images[mh ? "click_mh.png" : "click.png"];
    };
    const hitfxBase = this.respack?.hitfxScale ?? 1.0;
    const mhBuckets = new Map<string, number>();
    for (const cmd of commands) {
      const key = `${cmd.lane}:${cmd.kind}:${Math.round(cmd.y * 1000)}`;
      mhBuckets.set(key, (mhBuckets.get(key) ?? 0) + 1);
    }

    for (const cmd of commands) {
      const xw = Math.max(-margin, Math.min(1 + margin, cmd.x));
      const yw = Math.max(-margin, Math.min(1 + margin, cmd.y));
      const x = (0.5 + (xw - 0.5) / expand) * w;
      const y = (0.5 + (yw - 0.5) / expand) * h;
      const alpha = Math.max(0, Math.min(1, cmd.alpha));
      const rx = 8 * this.vis.noteScaleX * this.vis.hitfxScaleMul * hitfxBase / expand;
      const ry = 8 * this.vis.noteScaleY * this.vis.hitfxScaleMul * hitfxBase / expand;
      const key = `${cmd.lane}:${cmd.kind}:${Math.round(cmd.y * 1000)}`;
      const mh = (mhBuckets.get(key) ?? 0) > 1;

      this.ctx.globalAlpha = alpha;
      const tex = pickNoteImage(cmd.kind, mh);
        if (tex) {
          if (cmd.kind === 3 && this.respack) {
          const [tailH, headH] = mh ? this.respack.holdAtlasMh : this.respack.holdAtlas;
          const safeTail = Math.max(1, Math.floor(tailH));
          const safeHead = Math.max(1, Math.floor(headH));
          const midH = Math.max(0, tex.height - safeTail - safeHead);
          const dtEnd = Math.max(0, (cmd.holdEndSec - timeSec) * this.vis.noteFlowSpeedMultiplier);
          const holdDur = Math.max(0, cmd.holdEndSec - cmd.tHitSec);
          const holdProgress = holdDur > 1e-6 ? Math.max(0, Math.min(1, (timeSec - cmd.tHitSec) / holdDur)) : 0;
          const keepHead = !!this.respack.holdKeepHead;
          const hideHeadNow = holdProgress > 1e-6 && !keepHead;
          const yEndRaw = 1.0 - (dtEnd / Math.max(0.05, this.vis.approachSec));
          const yEndCmd = Math.max(-margin, Math.min(1 + margin, 0.5 + (yEndRaw - 0.5) / expand));
          const yEnd = (0.5 + (yEndCmd - 0.5) / expand) * h;
          const yTop = Math.min(y, yEnd);
          const yBottom = Math.max(y, yEnd);

          if (tex.height > safeTail + safeHead && cmd.holdEndSec > cmd.tHitSec + 1e-6) {
            const drawW = rx * 2;
            const headHDraw = ry * 2;
            const tailHDraw = this.respack.holdTailNoScale ? Math.min(headHDraw, (safeTail / Math.max(1, safeHead)) * headHDraw) : headHDraw;
            const tailDstY = yTop - tailHDraw * 0.5;
            const headDstY = yBottom - headHDraw * 0.5;

            this.ctx.drawImage(tex, 0, 0, tex.width, safeTail, x - rx, tailDstY, drawW, tailHDraw);

            const bodyTop = tailDstY + tailHDraw;
            const bodyBottom = hideHeadNow ? (yBottom + headHDraw * 0.5) : headDstY;
            const bodyH = Math.max(0, bodyBottom - bodyTop);
            if (bodyH > 0.5 && midH > 0) {
              if (this.respack.holdRepeat) {
                let yCursor = bodyTop;
                while (yCursor < bodyBottom - 0.5) {
                  const remain = bodyBottom - yCursor;
                  const hChunk = Math.min(remain, headHDraw);
                  this.ctx.drawImage(tex, 0, safeTail, tex.width, midH, x - rx, yCursor, drawW, hChunk);
                  yCursor += hChunk;
                }
              } else {
                if (hideHeadNow && this.respack.holdCompact) {
                  const keep = Math.max(0.02, Math.min(1, 1 - holdProgress));
                  const srcH = Math.max(1, Math.floor(midH * keep));
                  const srcY = safeTail + Math.max(0, midH - srcH);
                  this.ctx.drawImage(tex, 0, srcY, tex.width, srcH, x - rx, bodyTop, drawW, bodyH);
                } else {
                  this.ctx.drawImage(tex, 0, safeTail, tex.width, midH, x - rx, bodyTop, drawW, bodyH);
                }
              }
            }

            if (!hideHeadNow) {
              this.ctx.drawImage(tex, 0, tex.height - safeHead, tex.width, safeHead, x - rx, headDstY, drawW, headHDraw);
            }
          } else {
            this.ctx.drawImage(tex, x - rx, y - ry, rx * 2, ry * 2);
          }
        } else {
          this.ctx.drawImage(tex, x - rx, y - ry, rx * 2, ry * 2);
        }
        if (this.vis.noteOutline) {
          this.ctx.strokeStyle = "#e2e8f0";
          this.ctx.lineWidth = 1;
          this.ctx.strokeRect(x - rx, y - ry, rx * 2, ry * 2);
        }
      } else {
        this.ctx.fillStyle = cmd.kind === 2 ? "#6cc0ff" : cmd.kind === 3 ? "#7cf29c" : cmd.kind === 4 ? "#ff8fab" : "#ffe066";
        this.ctx.beginPath();
        this.ctx.ellipse(x, y, rx, ry, 0, 0, Math.PI * 2);
        this.ctx.fill();
        if (this.vis.noteOutline) {
          this.ctx.strokeStyle = "#e2e8f0";
          this.ctx.lineWidth = 1;
          this.ctx.stroke();
        }
      }
    }

    const nextFx: typeof this.judgeFx = [];
    const judgeY = h * 0.9;
    for (const fx of this.judgeFx) {
      const laneX = ((Math.max(0, Math.min(laneCount - 1, fx.lane)) + 1) / (laneCount + 1)) * w;
      const progress = 1 - Math.max(0, Math.min(1, fx.ttl / Math.max(0.05, fx.duration)));
      const t = Math.max(0, Math.min(1, fx.ttl / Math.max(0.05, fx.duration)));
      const r = (16 * fx.scale * this.vis.hitfxScaleMul * (this.respack?.hitfxScale ?? 1.0)) / expand;
      const fxSheet = this.respack?.images[fx.kind === 2 ? "hit_fx.good.png" : "hit_fx.png"] ?? this.respack?.images["hit_fx.png"];
      if (fxSheet && this.respack && !this.respack.hideParticles) {
        const cols = Math.max(1, Math.floor(this.respack.hitfxFrames[0]));
        const rows = Math.max(1, Math.floor(this.respack.hitfxFrames[1]));
        const total = cols * rows;
        const frame = Math.min(total - 1, Math.max(0, Math.floor(progress * total)));
        const sw = Math.max(1, Math.floor(fxSheet.width / cols));
        const sh = Math.max(1, Math.floor(fxSheet.height / rows));
        const sx = (frame % cols) * sw;
        const sy = Math.floor(frame / cols) * sh;
        this.ctx.save();
        this.ctx.translate(laneX, judgeY);
        if (this.respack.hitfxRotate) {
          this.ctx.rotate(fx.angle + progress * Math.PI * 0.8);
        }
        this.ctx.globalAlpha = t;
        this.ctx.drawImage(fxSheet, sx, sy, sw, sh, -r, -r, r * 2, r * 2);
        if (this.respack.hitfxTinted) {
          const prev = this.ctx.globalCompositeOperation;
          this.ctx.globalCompositeOperation = "source-atop";
          this.ctx.fillStyle = fx.color;
          this.ctx.fillRect(-r, -r, r * 2, r * 2);
          this.ctx.globalCompositeOperation = prev;
        }
        this.ctx.restore();
      } else {
        this.ctx.globalAlpha = t;
        this.ctx.fillStyle = fx.color;
        this.ctx.beginPath();
        this.ctx.arc(laneX, judgeY, r * (1.3 - t * 0.3), 0, Math.PI * 2);
        this.ctx.fill();
      }
      const ttl = fx.ttl - 1 / 60;
      if (ttl > 0) {
        nextFx.push({ ...fx, ttl });
      }
    }
    this.judgeFx = nextFx;

    if (!this.vis.noTitleOverlay && this.hudText) {
      this.ctx.globalAlpha = 0.95;
      this.ctx.fillStyle = "#e2e8f0";
      this.ctx.font = `${Math.round(12 * this.vis.fontSizeMultiplier)}px system-ui, sans-serif`;
      this.ctx.fillText(this.hudText, 12, 20);
    }

    this.ctx.globalAlpha = 1;
  }

  private pickJudgeColor(kind: number): string {
    if (this.respack) {
      const c =
        kind === 1 ? this.respack.judgeColors.PERFECT :
        kind === 2 ? this.respack.judgeColors.GOOD :
        kind === 3 ? this.respack.judgeColors.BAD :
        this.respack.judgeColors.MISS;
      return `rgba(${c.r}, ${c.g}, ${c.b}, ${Math.max(0, Math.min(1, c.a / 255))})`;
    }
    if (kind === 1) return "rgba(255,255,255,1)";
    if (kind === 2) return "rgba(180,220,255,1)";
    if (kind === 3) return "rgba(255,180,180,1)";
    return "rgba(200,200,200,1)";
  }
}
