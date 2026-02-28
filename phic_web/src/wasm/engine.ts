export type SimulateMode = "conservative" | "aggressive" | "extreme";

export interface EngineCreateConfig {
  width?: number;
  height?: number;
  approachSec?: number;
  noteSpeed?: number;
  autoplay?: boolean;
  noCull?: boolean;
  noCullScreen?: boolean;
  noCullEnterTime?: boolean;
  noteOutline?: boolean;
  noteScaleX?: number;
  noteScaleY?: number;
  noteFlowSpeedMultiplier?: number;
  expand?: number;
  overrender?: number;
  trailAlpha?: number;
  trailBlur?: number;
  trailDim?: number;
  simulateplay?: boolean;
  simulateMode?: SimulateMode;
  simulateMaxPointers?: number;
  modMirror?: boolean;
  modReverse?: boolean;
  modRandomize?: boolean;
  modHoldConvert?: boolean;
  modTransposeSec?: number;
  modStretchFactor?: number;
  modStretchAnchorSec?: number;
  modQuantize?: boolean;
  modQuantizeStepSec?: number;
  modWave?: boolean;
  modWaveAmplitudeLane?: number;
  modWavePeriodSec?: number;
  modStutter?: boolean;
  modStutterRepeat?: number;
  modStutterIntervalSec?: number;
  modThinOutEvery?: number;
  modSeed?: number;
  laneCount?: number;
  bgmVolume?: number;
  hitfxScaleMul?: number;
  fontSizeMultiplier?: number;
  holdTailTol?: number;
  judgeWidth?: number;
  judgeHeight?: number;
  flickThreshold?: number;
  bgBlur?: number;
  bgDim?: number;
  hitsoundMinIntervalMs?: number;
  holdFxIntervalMs?: number;
  multicolorLines?: boolean;
  noTitleOverlay?: boolean;
  advanceSeqOverlay?: boolean;
  rpeEasingShift?: number;
  seed?: number;
}

export interface InputEvent {
  lane: number;
  eventTime: number;
}

export interface FrameCommand {
  noteId: number;
  lane: number;
  kind: number;
  x: number;
  y: number;
  alpha: number;
  tHitSec: number;
  holdEndSec: number;
}

export interface EngineStats {
  combo: number;
  maxCombo: number;
  judgedCnt: number;
  hitTotal: number;
  accSum: number;
  accuracy: number;
  timeSec: number;
}

export interface StepResult {
  commands: FrameCommand[];
  judgeEvents: JudgeEvent[];
  stats: EngineStats;
}

export interface JudgeEvent {
  noteId: number;
  lane: number;
  kind: number;
  source: number;
  noteKind: number;
  eventTime: number;
}

interface CExports {
  _malloc(size: number): number;
  _free(ptr: number): void;
  _phic_abi_version(): number;
  _phic_engine_create(cfgPtr: number): number;
  _phic_engine_destroy(enginePtr: number): void;
  _phic_engine_load_chart(enginePtr: number, bytesPtr: number, byteCount: number, fmtPtr: number): number;
  _phic_engine_step(
    enginePtr: number,
    dtSec: number,
    eventsPtr: number,
    eventCount: number,
    outCommandsPtr: number,
    commandCapacity: number,
    outCommandCountPtr: number,
    outStatsPtr: number
  ): number;
  _phic_engine_step_ex(
    enginePtr: number,
    dtSec: number,
    eventsPtr: number,
    eventCount: number,
    outCommandsPtr: number,
    commandCapacity: number,
    outCommandCountPtr: number,
    outStatsPtr: number,
    outJudgeEventsPtr: number,
    judgeEventCapacity: number,
    outJudgeEventCountPtr: number
  ): number;
  _phic_engine_step_auto(
    enginePtr: number,
    dtSec: number,
    outCommandsPtr: number,
    commandCapacity: number,
    outCommandCountPtr: number,
    outStatsPtr: number
  ): number;
  _phic_engine_step_auto_ex(
    enginePtr: number,
    dtSec: number,
    outCommandsPtr: number,
    commandCapacity: number,
    outCommandCountPtr: number,
    outStatsPtr: number,
    outJudgeEventsPtr: number,
    judgeEventCapacity: number,
    outJudgeEventCountPtr: number
  ): number;
  _phic_engine_step_v2?(
    enginePtr: number,
    dtSec: number,
    eventsPtr: number,
    eventCount: number,
    outCommandsPtr: number,
    commandCapacity: number,
    outCommandCountPtr: number,
    outStatsPtr: number,
    outJudgeEventsPtr: number,
    judgeEventCapacity: number,
    outJudgeEventCountPtr: number
  ): number;
  _phic_engine_step_auto_v2?(
    enginePtr: number,
    dtSec: number,
    outCommandsPtr: number,
    commandCapacity: number,
    outCommandCountPtr: number,
    outStatsPtr: number,
    outJudgeEventsPtr: number,
    judgeEventCapacity: number,
    outJudgeEventCountPtr: number
  ): number;
  _phic_engine_seek(enginePtr: number, timeSec: number): number;
  _phic_engine_reset(enginePtr: number): number;
  _phic_engine_last_error(enginePtr: number): number;
  UTF8ToString(ptr: number): string;
  stringToUTF8(value: string, ptr: number, maxBytes: number): void;
  lengthBytesUTF8(value: string): number;
  HEAPU8: Uint8Array;
  HEAP32: Int32Array;
  HEAPF32: Float32Array;
  HEAPF64: Float64Array;
}

const PHIC_OK = 0;
const PHIC_ERR_BUFFER_TOO_SMALL = 3;
const CONFIG_BYTES = 320;
const INPUT_EVENT_BYTES = 16;
const FRAME_COMMAND_BYTES = 24;
const FRAME_COMMAND_V2_BYTES = 40;
const STATS_BYTES = 48;
const JUDGE_EVENT_BYTES = 24;
const JUDGE_EVENT_V2_BYTES = 32;

function modeToInt(mode: SimulateMode): number {
  if (mode === "aggressive") return 1;
  if (mode === "extreme") return 2;
  return 0;
}

function writeCString(mod: CExports, value: string): number {
  const bytes = mod.lengthBytesUTF8(value) + 1;
  const ptr = mod._malloc(bytes);
  mod.stringToUTF8(value, ptr, bytes);
  return ptr;
}

export class WasmPhicEngine {
  private readonly mod: CExports;
  private readonly enginePtr: number;
  private readonly abiVersion: number;
  private readonly useV2: boolean;
  private readonly commandStride: number;
  private readonly judgeEventStride: number;
  private disposed = false;

  private commandCapacity = 8192;
  private commandsPtr: number;
  private commandCountPtr: number;
  private statsPtr: number;
  private judgeEventCapacity = 1024;
  private judgeEventsPtr: number;
  private judgeEventCountPtr: number;

  static async loadModule(jsModuleUrl = "/wasm/phic_web.js"): Promise<CExports> {
    const modFactory = await import(/* @vite-ignore */ jsModuleUrl);
    const instantiated = await modFactory.default();
    return instantiated as CExports;
  }

  static async create(cfg: EngineCreateConfig = {}, jsModuleUrl?: string): Promise<WasmPhicEngine> {
    const mod = await WasmPhicEngine.loadModule(jsModuleUrl);
    return new WasmPhicEngine(mod, cfg);
  }

  private constructor(mod: CExports, cfg: EngineCreateConfig) {
    this.mod = mod;

    const cfgPtr = this.mod._malloc(CONFIG_BYTES);
    this.mod.HEAPU8.fill(0, cfgPtr, cfgPtr + CONFIG_BYTES);

    const width = cfg.width ?? 1280;
    const height = cfg.height ?? 720;
    const approach = cfg.approachSec ?? 3.0;
    const speed = cfg.noteSpeed ?? 1.0;
    const autoplay = cfg.autoplay ? 1 : 0;
    const noCull = cfg.noCull ? 1 : 0;
    const noCullScreen = cfg.noCullScreen ? 1 : 0;
    const noCullEnterTime = cfg.noCullEnterTime ? 1 : 0;
    const noteOutline = cfg.noteOutline ? 1 : 0;
    const noteScaleX = cfg.noteScaleX ?? 1.0;
    const noteScaleY = cfg.noteScaleY ?? 1.0;
    const noteFlow = cfg.noteFlowSpeedMultiplier ?? 1.0;
    const expand = cfg.expand ?? 1.0;
    const overrender = cfg.overrender ?? 2.0;
    const trailAlpha = cfg.trailAlpha ?? 0.0;
    const trailBlur = cfg.trailBlur ?? 0;
    const trailDim = cfg.trailDim ?? 0;
    const simulate = cfg.simulateplay ? 1 : 0;
    const simulateMode = modeToInt(cfg.simulateMode ?? "conservative");
    const simulateMaxPointers = cfg.simulateMaxPointers ?? 2;
    const modMirror = cfg.modMirror ? 1 : 0;
    const modReverse = cfg.modReverse ? 1 : 0;
    const modRandomize = cfg.modRandomize ? 1 : 0;
    const modHoldConvert = cfg.modHoldConvert ? 1 : 0;
    const modTransposeSec = cfg.modTransposeSec ?? 0.0;
    const modStretchFactor = cfg.modStretchFactor ?? 1.0;
    const modStretchAnchorSec = cfg.modStretchAnchorSec ?? 0.0;
    const modQuantize = cfg.modQuantize ? 1 : 0;
    const modQuantizeStepSec = cfg.modQuantizeStepSec ?? 0.05;
    const modWave = cfg.modWave ? 1 : 0;
    const modWaveAmplitudeLane = cfg.modWaveAmplitudeLane ?? 1.0;
    const modWavePeriodSec = cfg.modWavePeriodSec ?? 1.0;
    const modStutter = cfg.modStutter ? 1 : 0;
    const modStutterRepeat = cfg.modStutterRepeat ?? 2;
    const modStutterIntervalSec = cfg.modStutterIntervalSec ?? 0.02;
    const modThinOutEvery = cfg.modThinOutEvery ?? 1;
    const modSeed = cfg.modSeed ?? cfg.seed ?? 0;
    const laneCount = cfg.laneCount ?? 8;
    const bgmVolume = cfg.bgmVolume ?? 0.8;
    const hitfxScaleMul = cfg.hitfxScaleMul ?? 1.0;
    const fontSizeMultiplier = cfg.fontSizeMultiplier ?? 1.0;
    const holdTailTol = cfg.holdTailTol ?? 0.8;
    const judgeWidth = cfg.judgeWidth ?? 0.12;
    const judgeHeight = cfg.judgeHeight ?? 0.06;
    const flickThreshold = cfg.flickThreshold ?? 0.02;
    const bgBlur = cfg.bgBlur ?? 10;
    const bgDim = cfg.bgDim ?? 120;
    const hitsoundMinIntervalMs = cfg.hitsoundMinIntervalMs ?? 30;
    const holdFxIntervalMs = cfg.holdFxIntervalMs ?? 200;
    const multicolorLines = cfg.multicolorLines ? 1 : 0;
    const noTitleOverlay = cfg.noTitleOverlay ? 1 : 0;
    const advanceSeqOverlay = cfg.advanceSeqOverlay ? 1 : 0;
    const rpeEasingShift = cfg.rpeEasingShift ?? 0;

    this.mod.HEAP32[(cfgPtr + 0) >> 2] = width;
    this.mod.HEAP32[(cfgPtr + 4) >> 2] = height;
    this.mod.HEAPF64[(cfgPtr + 8) >> 3] = approach;
    this.mod.HEAPF64[(cfgPtr + 16) >> 3] = speed;
    this.mod.HEAP32[(cfgPtr + 24) >> 2] = autoplay;
    this.mod.HEAP32[(cfgPtr + 28) >> 2] = noCull;
    this.mod.HEAP32[(cfgPtr + 32) >> 2] = noCullScreen;
    this.mod.HEAP32[(cfgPtr + 36) >> 2] = noCullEnterTime;
    this.mod.HEAP32[(cfgPtr + 40) >> 2] = noteOutline;
    this.mod.HEAPF64[(cfgPtr + 48) >> 3] = noteScaleX;
    this.mod.HEAPF64[(cfgPtr + 56) >> 3] = noteScaleY;
    this.mod.HEAPF64[(cfgPtr + 64) >> 3] = noteFlow;
    this.mod.HEAPF64[(cfgPtr + 72) >> 3] = expand;
    this.mod.HEAPF64[(cfgPtr + 80) >> 3] = overrender;
    this.mod.HEAPF64[(cfgPtr + 88) >> 3] = trailAlpha;
    this.mod.HEAP32[(cfgPtr + 96) >> 2] = trailBlur;
    this.mod.HEAP32[(cfgPtr + 100) >> 2] = trailDim;
    this.mod.HEAP32[(cfgPtr + 104) >> 2] = simulate;
    this.mod.HEAP32[(cfgPtr + 108) >> 2] = simulateMode;
    this.mod.HEAP32[(cfgPtr + 112) >> 2] = simulateMaxPointers;
    this.mod.HEAP32[(cfgPtr + 116) >> 2] = modMirror;
    this.mod.HEAP32[(cfgPtr + 120) >> 2] = modReverse;
    this.mod.HEAP32[(cfgPtr + 124) >> 2] = modRandomize;
    this.mod.HEAP32[(cfgPtr + 128) >> 2] = modHoldConvert;
    this.mod.HEAPF64[(cfgPtr + 136) >> 3] = modTransposeSec;
    this.mod.HEAPF64[(cfgPtr + 144) >> 3] = modStretchFactor;
    this.mod.HEAPF64[(cfgPtr + 152) >> 3] = modStretchAnchorSec;
    this.mod.HEAP32[(cfgPtr + 160) >> 2] = modQuantize;
    this.mod.HEAPF64[(cfgPtr + 168) >> 3] = modQuantizeStepSec;
    this.mod.HEAP32[(cfgPtr + 176) >> 2] = modWave;
    this.mod.HEAPF64[(cfgPtr + 184) >> 3] = modWaveAmplitudeLane;
    this.mod.HEAPF64[(cfgPtr + 192) >> 3] = modWavePeriodSec;
    this.mod.HEAP32[(cfgPtr + 200) >> 2] = modStutter;
    this.mod.HEAP32[(cfgPtr + 204) >> 2] = modStutterRepeat;
    this.mod.HEAPF64[(cfgPtr + 208) >> 3] = modStutterIntervalSec;
    this.mod.HEAP32[(cfgPtr + 216) >> 2] = modThinOutEvery;
    this.mod.HEAP32[(cfgPtr + 220) >> 2] = modSeed;
    this.mod.HEAP32[(cfgPtr + 224) >> 2] = laneCount;
    this.mod.HEAPF64[(cfgPtr + 232) >> 3] = bgmVolume;
    this.mod.HEAPF64[(cfgPtr + 240) >> 3] = hitfxScaleMul;
    this.mod.HEAPF64[(cfgPtr + 248) >> 3] = fontSizeMultiplier;
    this.mod.HEAPF64[(cfgPtr + 256) >> 3] = holdTailTol;
    this.mod.HEAPF64[(cfgPtr + 264) >> 3] = judgeWidth;
    this.mod.HEAPF64[(cfgPtr + 272) >> 3] = judgeHeight;
    this.mod.HEAPF64[(cfgPtr + 280) >> 3] = flickThreshold;
    this.mod.HEAP32[(cfgPtr + 288) >> 2] = bgBlur;
    this.mod.HEAP32[(cfgPtr + 292) >> 2] = bgDim;
    this.mod.HEAP32[(cfgPtr + 296) >> 2] = hitsoundMinIntervalMs;
    this.mod.HEAP32[(cfgPtr + 300) >> 2] = holdFxIntervalMs;
    this.mod.HEAP32[(cfgPtr + 304) >> 2] = multicolorLines;
    this.mod.HEAP32[(cfgPtr + 308) >> 2] = noTitleOverlay;
    this.mod.HEAP32[(cfgPtr + 312) >> 2] = advanceSeqOverlay;
    this.mod.HEAP32[(cfgPtr + 316) >> 2] = rpeEasingShift;

    this.enginePtr = this.mod._phic_engine_create(cfgPtr);
    this.mod._free(cfgPtr);
    if (!this.enginePtr) {
      throw new Error("phic_engine_create failed");
    }

    this.abiVersion = this.mod._phic_abi_version();
    this.useV2 = this.abiVersion >= 5 && typeof this.mod._phic_engine_step_v2 === "function" && typeof this.mod._phic_engine_step_auto_v2 === "function";
    this.commandStride = this.useV2 ? FRAME_COMMAND_V2_BYTES : FRAME_COMMAND_BYTES;
    this.judgeEventStride = this.useV2 ? JUDGE_EVENT_V2_BYTES : JUDGE_EVENT_BYTES;

    this.commandsPtr = this.mod._malloc(this.commandCapacity * this.commandStride);
    this.commandCountPtr = this.mod._malloc(8);
    this.statsPtr = this.mod._malloc(STATS_BYTES);
    this.judgeEventsPtr = this.mod._malloc(this.judgeEventCapacity * this.judgeEventStride);
    this.judgeEventCountPtr = this.mod._malloc(8);
  }

  getAbiVersion(): number {
    return this.abiVersion;
  }

  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.mod._phic_engine_destroy(this.enginePtr);
    this.mod._free(this.commandsPtr);
    this.mod._free(this.commandCountPtr);
    this.mod._free(this.statsPtr);
    this.mod._free(this.judgeEventsPtr);
    this.mod._free(this.judgeEventCountPtr);
  }

  loadChart(bytes: Uint8Array, formatHint: string): void {
    this.ensureAlive();
    const bytesPtr = this.mod._malloc(bytes.byteLength);
    this.mod.HEAPU8.set(bytes, bytesPtr);
    const fmtPtr = writeCString(this.mod, formatHint);

    const rc = this.mod._phic_engine_load_chart(this.enginePtr, bytesPtr, bytes.byteLength, fmtPtr);

    this.mod._free(fmtPtr);
    this.mod._free(bytesPtr);

    if (rc !== PHIC_OK) {
      throw new Error(this.lastError());
    }
  }

  seek(timeSec: number): void {
    this.ensureAlive();
    const rc = this.mod._phic_engine_seek(this.enginePtr, timeSec);
    if (rc !== PHIC_OK) throw new Error(this.lastError());
  }

  reset(): void {
    this.ensureAlive();
    const rc = this.mod._phic_engine_reset(this.enginePtr);
    if (rc !== PHIC_OK) throw new Error(this.lastError());
  }

  step(dtSec: number, events: InputEvent[] = [], auto = false): StepResult {
    this.ensureAlive();

    const eventCount = events.length;
    const eventsPtr = eventCount > 0 ? this.mod._malloc(eventCount * INPUT_EVENT_BYTES) : 0;
    if (eventsPtr) {
      for (let i = 0; i < eventCount; i += 1) {
        const base = eventsPtr + i * INPUT_EVENT_BYTES;
        this.mod.HEAP32[(base + 0) >> 2] = events[i].lane;
        this.mod.HEAPF64[(base + 8) >> 3] = events[i].eventTime;
      }
    }

    let rc: number;
    if (this.useV2) {
      if (auto) {
        rc = this.mod._phic_engine_step_auto_v2!(
          this.enginePtr,
          dtSec,
          this.commandsPtr,
          this.commandCapacity,
          this.commandCountPtr,
          this.statsPtr,
          this.judgeEventsPtr,
          this.judgeEventCapacity,
          this.judgeEventCountPtr
        );
      } else {
        rc = this.mod._phic_engine_step_v2!(
          this.enginePtr,
          dtSec,
          eventsPtr,
          eventCount,
          this.commandsPtr,
          this.commandCapacity,
          this.commandCountPtr,
          this.statsPtr,
          this.judgeEventsPtr,
          this.judgeEventCapacity,
          this.judgeEventCountPtr
        );
      }
    } else {
      if (auto) {
        rc = this.mod._phic_engine_step_auto_ex(
          this.enginePtr,
          dtSec,
          this.commandsPtr,
          this.commandCapacity,
          this.commandCountPtr,
          this.statsPtr,
          this.judgeEventsPtr,
          this.judgeEventCapacity,
          this.judgeEventCountPtr
        );
      } else {
        rc = this.mod._phic_engine_step_ex(
          this.enginePtr,
          dtSec,
          eventsPtr,
          eventCount,
          this.commandsPtr,
          this.commandCapacity,
          this.commandCountPtr,
          this.statsPtr,
          this.judgeEventsPtr,
          this.judgeEventCapacity,
          this.judgeEventCountPtr
        );
      }
    }

    if (eventsPtr) this.mod._free(eventsPtr);

    if (rc === PHIC_ERR_BUFFER_TOO_SMALL) {
      const needed = this.mod.HEAP32[this.commandCountPtr >> 2];
      const neededJudge = this.mod.HEAP32[this.judgeEventCountPtr >> 2];
      if (needed > this.commandCapacity) {
        this.growCommandBuffer(Math.max(needed + 256, this.commandCapacity * 2));
      }
      if (neededJudge > this.judgeEventCapacity) {
        this.growJudgeEventBuffer(Math.max(neededJudge + 128, this.judgeEventCapacity * 2));
      }
      return this.step(dtSec, events, auto);
    }
    if (rc !== PHIC_OK) {
      throw new Error(this.lastError());
    }

    return {
      commands: this.readCommands(),
      judgeEvents: this.readJudgeEvents(),
      stats: this.readStats()
    };
  }

  private readCommands(): FrameCommand[] {
    const count = this.mod.HEAP32[this.commandCountPtr >> 2];
    const out: FrameCommand[] = [];
    for (let i = 0; i < count; i += 1) {
      const base = this.commandsPtr + i * this.commandStride;
      out.push({
        noteId: this.mod.HEAP32[(base + 0) >> 2],
        lane: this.mod.HEAP32[(base + 4) >> 2],
        kind: this.mod.HEAP32[(base + 8) >> 2],
        x: this.mod.HEAPF32[(base + 12) >> 2],
        y: this.mod.HEAPF32[(base + 16) >> 2],
        alpha: this.mod.HEAPF32[(base + 20) >> 2],
        tHitSec: this.useV2 ? this.mod.HEAPF64[(base + 24) >> 3] : 0,
        holdEndSec: this.useV2 ? this.mod.HEAPF64[(base + 32) >> 3] : 0
      });
    }
    return out;
  }

  private readJudgeEvents(): JudgeEvent[] {
    const count = this.mod.HEAP32[this.judgeEventCountPtr >> 2];
    const out: JudgeEvent[] = [];
    for (let i = 0; i < count; i += 1) {
      const base = this.judgeEventsPtr + i * this.judgeEventStride;
      out.push({
        noteId: this.mod.HEAP32[(base + 0) >> 2],
        lane: this.mod.HEAP32[(base + 4) >> 2],
        kind: this.mod.HEAP32[(base + 8) >> 2],
        source: this.mod.HEAP32[(base + 12) >> 2],
        noteKind: this.useV2 ? this.mod.HEAP32[(base + 16) >> 2] : 1,
        eventTime: this.useV2 ? this.mod.HEAPF64[(base + 24) >> 3] : this.mod.HEAPF64[(base + 16) >> 3]
      });
    }
    return out;
  }

  private readStats(): EngineStats {
    const p = this.statsPtr;
    return {
      combo: this.mod.HEAP32[(p + 0) >> 2],
      maxCombo: this.mod.HEAP32[(p + 4) >> 2],
      judgedCnt: this.mod.HEAP32[(p + 8) >> 2],
      hitTotal: this.mod.HEAP32[(p + 12) >> 2],
      accSum: this.mod.HEAPF64[(p + 16) >> 3],
      accuracy: this.mod.HEAPF64[(p + 24) >> 3],
      timeSec: this.mod.HEAPF64[(p + 32) >> 3]
    };
  }

  private growCommandBuffer(newCapacity: number): void {
    this.mod._free(this.commandsPtr);
    this.commandCapacity = newCapacity;
    this.commandsPtr = this.mod._malloc(this.commandCapacity * this.commandStride);
  }

  private growJudgeEventBuffer(newCapacity: number): void {
    this.mod._free(this.judgeEventsPtr);
    this.judgeEventCapacity = newCapacity;
    this.judgeEventsPtr = this.mod._malloc(this.judgeEventCapacity * this.judgeEventStride);
  }

  private lastError(): string {
    const ptr = this.mod._phic_engine_last_error(this.enginePtr);
    return ptr ? this.mod.UTF8ToString(ptr) : "unknown error";
  }

  private ensureAlive(): void {
    if (this.disposed) {
      throw new Error("engine already disposed");
    }
  }
}
