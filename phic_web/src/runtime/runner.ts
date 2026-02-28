import type { RunItem } from "../planner/runPlan";
import { AudioClock } from "./audioClock";
import type { StepResult } from "../wasm/engine";
import { WasmPhicEngine, type EngineCreateConfig } from "../wasm/engine";

export interface RunnerCallbacks {
  onFrame(result: StepResult, run: RunItem): void;
  onRunStart(run: RunItem): void;
  onRunEnd(run: RunItem, result: StepResult): void;
  onRunProgress?(runIndex: number, runCount: number, elapsedSec: number, maxSec: number): void;
  onLog(message: string): void;
}

export interface RunnerOptions {
  stepHz: number;
  autoSimulate: boolean;
  stopSignal?: { stopped: boolean };
  pauseSignal?: { paused: boolean };
  audioFile?: File | null;
}

function pickMaxSec(item: RunItem): number {
  if (typeof item.endTime === "number") {
    return Math.max(0, item.endTime - (item.startTime || 0));
  }
  return 30;
}

export async function runPlan(
  plan: RunItem[],
  cfg: EngineCreateConfig,
  options: RunnerOptions,
  callbacks: RunnerCallbacks
): Promise<void> {
  const dtNominal = 1 / Math.max(1, options.stepHz);
  const engine = await WasmPhicEngine.create(cfg);

  const clock = new AudioClock();
  const audio = options.audioFile ? new Audio(URL.createObjectURL(options.audioFile)) : null;
  clock.attach(audio);

  try {
    for (let runIndex = 0; runIndex < plan.length; runIndex += 1) {
      const item = plan[runIndex];
      if (options.stopSignal?.stopped) break;

      callbacks.onRunStart(item);
      const bytes = new Uint8Array(await item.file.arrayBuffer());
      engine.loadChart(bytes, item.format);

      if (typeof item.startTime === "number") {
        engine.seek(item.startTime);
      }

      if (audio) {
        audio.currentTime = 0;
        await clock.play();
      }

      let elapsed = 0;
      let lastClock = clock.now();
      let last: StepResult = engine.step(0, [], options.autoSimulate);
      const maxSec = pickMaxSec(item);

      while (elapsed < maxSec && !options.stopSignal?.stopped) {
        if (options.pauseSignal?.paused) {
          if (audio) clock.pause();
          await new Promise<void>((resolve) => setTimeout(resolve, 30));
          lastClock = clock.now();
          continue;
        }
        if (audio && audio.paused) {
          await clock.play();
          lastClock = clock.now();
        }

        const nowClock = clock.now();
        let dt = nowClock - lastClock;
        if (!Number.isFinite(dt) || dt <= 0) dt = dtNominal;
        dt = Math.max(dtNominal * 0.25, Math.min(dt, dtNominal * 4));

        last = engine.step(dt, [], options.autoSimulate);
        callbacks.onFrame(last, item);

        elapsed += dt;
        callbacks.onRunProgress?.(runIndex + 1, plan.length, elapsed, maxSec);
        lastClock = nowClock;
        await new Promise<void>((resolve) => requestAnimationFrame(() => resolve()));
      }

      callbacks.onRunEnd(item, last);
      if (audio) clock.pause();
    }
  } finally {
    if (audio) {
      URL.revokeObjectURL(audio.src);
      audio.pause();
    }
    engine.dispose();
  }
}
