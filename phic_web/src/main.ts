import "./styles.css";

import { buildRunPlan } from "./planner/runPlan";
import { loadRespackFromZip, type LoadedRespack } from "./assets/respack";
import { CanvasRenderer } from "./render/canvasRenderer";
import { runPlan } from "./runtime/runner";
import type { EngineCreateConfig, SimulateMode } from "./wasm/engine";

const root = document.querySelector<HTMLDivElement>("#app");
if (!root) throw new Error("#app missing");

root.innerHTML = `
  <div class="panel">
    <h1>Phic Web Runtime</h1>
    <div class="status-row">
      <span class="badge" id="status">Idle</span>
      <span class="badge badge-soft" id="file-state">No files</span>
    </div>
    <div class="progress-wrap">
      <div class="progress-bar"><div id="progress-fill"></div></div>
      <div class="progress-text" id="progress-text">0%</div>
    </div>
    <div class="controls">
      <label>Drop files here or choose:
        <input id="files" type="file" multiple />
      </label>
      <label>Global Respack (.zip)
        <input id="respack" type="file" accept=".zip" />
      </label>
      <div class="button-row">
        <button id="respack-unload">Unload Respack</button>
        <button id="respack-test-sfx">Test SFX</button>
        <button id="export-settings">Export Settings</button>
      </div>
      <pre id="respack-info">Respack: (none)</pre>
      <label>Import Settings JSON
        <input id="settings-file" type="file" accept=".json,.jsonc" />
      </label>
      <label>Advance JSON
        <input id="advance" type="file" accept=".json,.jsonc" />
      </label>
      <label>Optional Audio File
        <input id="audio" type="file" accept="audio/*" />
      </label>
      <div class="inline-grid">
        <label><input id="feature-simulate" type="checkbox" checked /> Enable simulateplay</label>
        <label><input id="feature-autosim" type="checkbox" checked /> Use auto-simulate step</label>
        <label><input id="feature-autoplay" type="checkbox" /> Enable autoplay</label>
        <label><input id="feature-audio" type="checkbox" checked /> Enable audio sync</label>
      </div>
      <div id="simulate-advanced">
        <label>Simulate Mode
          <select id="simulate-mode">
            <option value="conservative">conservative</option>
            <option value="aggressive">aggressive</option>
            <option value="extreme">extreme</option>
          </select>
        </label>
        <label>Simulate Max Pointers
          <input id="simulate-ptrs" type="number" min="1" max="8" value="2" />
        </label>
      </div>
      <label>Playlist seed
        <input id="seed" type="number" value="42" />
      </label>
      <label>Playlist limit (0 = unlimited)
        <input id="limit" type="number" value="5" />
      </label>
      <div class="inline-grid inline-grid-2">
        <label>Approach
          <input id="approach" type="number" step="0.1" min="0.1" value="3.0" />
        </label>
        <label>Chart Speed
          <input id="chart-speed" type="number" step="0.05" min="0.05" value="1.0" />
        </label>
        <label>Note Flow Speed Multiplier
          <input id="note-flow" type="number" step="0.05" min="0.05" value="1.0" />
        </label>
        <label>Expand Factor
          <input id="expand" type="number" step="0.1" min="1.0" value="1.0" />
        </label>
        <label>Overrender Factor
          <input id="overrender" type="number" step="0.1" min="1.0" value="2.0" />
        </label>
        <label>HitFX Scale Multiplier
          <input id="hitfx-scale" type="number" step="0.05" min="0.05" value="1.0" />
        </label>
        <label>Font Size Multiplier
          <input id="font-scale" type="number" step="0.05" min="0.05" value="1.0" />
        </label>
        <label>Note Scale X
          <input id="note-scale-x" type="number" step="0.05" min="0.1" value="1.0" />
        </label>
        <label>Note Scale Y
          <input id="note-scale-y" type="number" step="0.05" min="0.1" value="1.0" />
        </label>
        <label>Trail Alpha
          <input id="trail-alpha" type="number" step="0.05" min="0" max="1" value="0.0" />
        </label>
        <label>BG Dim
          <input id="bg-dim" type="number" step="1" min="0" max="255" value="120" />
        </label>
        <label>Lane Count
          <input id="lane-count" type="number" step="1" min="1" max="16" value="8" />
        </label>
        <label><input id="note-outline" type="checkbox" /> Note Outline</label>
        <label><input id="multicolor-lines" type="checkbox" /> Multicolor Lines</label>
        <label><input id="no-title-overlay" type="checkbox" /> Hide Title Overlay</label>
      </div>
      <label>Name contains filter
        <input id="name-filter" type="text" placeholder="optional" />
      </label>
      <label>Level filter csv (IN,AT,...)
        <input id="level-filter" type="text" placeholder="optional" />
      </label>
      <label><input id="shuffle" type="checkbox" checked /> Shuffle playlist</label>
      <div class="button-row">
        <button id="run">Run Plan</button>
        <button id="pause" disabled>Pause</button>
        <button id="stop" disabled>Stop</button>
      </div>
      <div class="button-row">
        <button id="clear-log">Clear Log</button>
        <button id="reset-global">Reset Global</button>
        <button id="clear-files">Clear Files</button>
      </div>
    </div>
    <pre id="summary">Plan: -\nCurrent: -\nAggregate: -</pre>
    <pre id="log"></pre>
  </div>
  <div class="canvas-wrap" id="dropzone">
    <div class="drop-hint" id="drop-hint">Drag and drop chart files (.json/.rpe/.pec)</div>
    <canvas id="canvas"></canvas>
    <pre id="stats"></pre>
  </div>
`;

const filesInput = document.querySelector<HTMLInputElement>("#files")!;
const respackInput = document.querySelector<HTMLInputElement>("#respack")!;
const advanceInput = document.querySelector<HTMLInputElement>("#advance")!;
const audioInput = document.querySelector<HTMLInputElement>("#audio")!;
const runBtn = document.querySelector<HTMLButtonElement>("#run")!;
const pauseBtn = document.querySelector<HTMLButtonElement>("#pause")!;
const stopBtn = document.querySelector<HTMLButtonElement>("#stop")!;
const logEl = document.querySelector<HTMLElement>("#log")!;
const summaryEl = document.querySelector<HTMLElement>("#summary")!;
const statsEl = document.querySelector<HTMLElement>("#stats")!;
const canvas = document.querySelector<HTMLCanvasElement>("#canvas")!;
const dropzone = document.querySelector<HTMLElement>("#dropzone")!;
const statusEl = document.querySelector<HTMLElement>("#status")!;
const fileStateEl = document.querySelector<HTMLElement>("#file-state")!;
const dropHintEl = document.querySelector<HTMLElement>("#drop-hint")!;
const simulateAdvancedEl = document.querySelector<HTMLElement>("#simulate-advanced")!;
const progressFillEl = document.querySelector<HTMLElement>("#progress-fill")!;
const progressTextEl = document.querySelector<HTMLElement>("#progress-text")!;
const clearLogBtn = document.querySelector<HTMLButtonElement>("#clear-log")!;
const resetGlobalBtn = document.querySelector<HTMLButtonElement>("#reset-global")!;
const clearFilesBtn = document.querySelector<HTMLButtonElement>("#clear-files")!;
const respackUnloadBtn = document.querySelector<HTMLButtonElement>("#respack-unload")!;
const respackTestSfxBtn = document.querySelector<HTMLButtonElement>("#respack-test-sfx")!;
const exportSettingsBtn = document.querySelector<HTMLButtonElement>("#export-settings")!;
const settingsFileInput = document.querySelector<HTMLInputElement>("#settings-file")!;
const respackInfoEl = document.querySelector<HTMLElement>("#respack-info")!;

const simulateModeEl = document.querySelector<HTMLSelectElement>("#simulate-mode")!;
const simulatePtrsEl = document.querySelector<HTMLInputElement>("#simulate-ptrs")!;
const featureSimulateEl = document.querySelector<HTMLInputElement>("#feature-simulate")!;
const featureAutoSimEl = document.querySelector<HTMLInputElement>("#feature-autosim")!;
const featureAutoplayEl = document.querySelector<HTMLInputElement>("#feature-autoplay")!;
const featureAudioEl = document.querySelector<HTMLInputElement>("#feature-audio")!;
const seedEl = document.querySelector<HTMLInputElement>("#seed")!;
const limitEl = document.querySelector<HTMLInputElement>("#limit")!;
const approachEl = document.querySelector<HTMLInputElement>("#approach")!;
const chartSpeedEl = document.querySelector<HTMLInputElement>("#chart-speed")!;
const noteFlowEl = document.querySelector<HTMLInputElement>("#note-flow")!;
const expandEl = document.querySelector<HTMLInputElement>("#expand")!;
const overrenderEl = document.querySelector<HTMLInputElement>("#overrender")!;
const hitfxScaleEl = document.querySelector<HTMLInputElement>("#hitfx-scale")!;
const fontScaleEl = document.querySelector<HTMLInputElement>("#font-scale")!;
const noteScaleXEl = document.querySelector<HTMLInputElement>("#note-scale-x")!;
const noteScaleYEl = document.querySelector<HTMLInputElement>("#note-scale-y")!;
const trailAlphaEl = document.querySelector<HTMLInputElement>("#trail-alpha")!;
const bgDimEl = document.querySelector<HTMLInputElement>("#bg-dim")!;
const laneCountEl = document.querySelector<HTMLInputElement>("#lane-count")!;
const noteOutlineEl = document.querySelector<HTMLInputElement>("#note-outline")!;
const multicolorLinesEl = document.querySelector<HTMLInputElement>("#multicolor-lines")!;
const noTitleOverlayEl = document.querySelector<HTMLInputElement>("#no-title-overlay")!;
const nameFilterEl = document.querySelector<HTMLInputElement>("#name-filter")!;
const levelFilterEl = document.querySelector<HTMLInputElement>("#level-filter")!;
const shuffleEl = document.querySelector<HTMLInputElement>("#shuffle")!;

const renderer = new CanvasRenderer(canvas);
renderer.resize(1280, 720);

let droppedFiles: File[] = [];
let globalRespack: File | null = null;
let loadedRespack: LoadedRespack | null = null;
let respackSfx: Partial<Record<"click.ogg" | "drag.ogg" | "flick.ogg", HTMLAudioElement>> = {};
const noteKindById = new Map<number, number>();
const stopSignal = { stopped: false };
const pauseSignal = { paused: false };
let running = false;
const GLOBAL_KEY = "phic.web.global.v1";

let aggregate = {
  runsDone: 0,
  judged: 0,
  hit: 0,
  weightedAcc: 0
};

const setStatus = (text: string, extraClass = ""): void => {
  statusEl.textContent = text;
  statusEl.className = `badge ${extraClass}`.trim();
};

const setProgress = (percent: number, text?: string): void => {
  const p = Math.max(0, Math.min(100, percent));
  progressFillEl.style.width = `${p.toFixed(1)}%`;
  progressTextEl.textContent = text ?? `${p.toFixed(1)}%`;
};

const parseNum = (el: HTMLInputElement, fallback: number, min?: number): number => {
  const raw = Number(el.value);
  let n = Number.isFinite(raw) ? raw : fallback;
  if (typeof min === "number") n = Math.max(min, n);
  return n;
};

const stripJsonc = (text: string): string => text.replace(/\/\*[\s\S]*?\*\//g, "").replace(/(^|[^:])\/\/.*$/gm, "$1").replace(/,\s*([}\]])/g, "$1");

const remapSettings = (raw: Record<string, unknown>): Record<string, unknown> => {
  const alias: Record<string, string> = {
    chart_speed: "noteSpeed",
    note_flow_speed_multiplier: "noteFlow",
    hitfx_scale_mul: "hitfxScaleMul",
    font_size_multiplier: "fontSizeMultiplier",
    note_scale_x: "noteScaleX",
    note_scale_y: "noteScaleY",
    trail_alpha: "trailAlpha",
    bg_dim: "bgDim",
    lane_count: "laneCount",
    simulateplay_mode: "simulateMode",
    simulateplay_max_pointers: "simulateMaxPointers",
    playlist_seed: "seed",
    playlist_filter_limit: "playlistLimit",
    playlist_no_shuffle: "playlistShuffle",
    no_title_overlay: "noTitleOverlay",
    multicolor_lines: "multicolorLines",
    note_outline: "noteOutline"
  };
  const out = { ...raw };
  for (const [from, to] of Object.entries(alias)) {
    if (typeof out[to] === "undefined" && typeof out[from] !== "undefined") out[to] = out[from];
  }
  if (typeof out.playlistShuffle === "boolean" && typeof raw.playlist_no_shuffle === "boolean") {
    out.playlistShuffle = !raw.playlist_no_shuffle;
  }
  return out;
};

const releaseRespack = (pack: LoadedRespack | null): void => {
  if (!pack) return;
  Object.values(pack.sounds).forEach((url) => {
    if (typeof url === "string") URL.revokeObjectURL(url);
  });
  respackSfx = {};
};

const rebuildRespackSfx = (): void => {
  respackSfx = {};
  const pack = loadedRespack;
  if (!pack) return;
  (Object.keys(pack.sounds) as Array<"click.ogg" | "drag.ogg" | "flick.ogg">).forEach((name) => {
    const url = pack.sounds[name];
    if (!url) return;
    const audio = new Audio(url);
    audio.preload = "auto";
    audio.volume = 0.8;
    respackSfx[name] = audio;
  });
};

const playJudgeSfx = (judgeKind: number, noteKind?: number): void => {
  if (judgeKind === 4) return;
  const key = noteKind === 2 ? "drag.ogg" : noteKind === 4 ? "flick.ogg" : "click.ogg";
  const sfx = respackSfx[key];
  if (!sfx) return;
  try {
    sfx.currentTime = 0;
    void sfx.play();
  } catch {
    // ignore autoplay restrictions
  }
};

const saveGlobalConfig = (): void => {
  const cfg = {
    simulateplay: featureSimulateEl.checked,
    autoSimulate: featureAutoSimEl.checked,
    autoplay: featureAutoplayEl.checked,
    audioSync: featureAudioEl.checked,
    simulateMode: simulateModeEl.value,
    simulateMaxPointers: parseNum(simulatePtrsEl, 2, 1),
    approachSec: parseNum(approachEl, 3.0, 0.1),
    noteSpeed: parseNum(chartSpeedEl, 1.0, 0.05),
    noteFlow: parseNum(noteFlowEl, 1.0, 0.05),
    expand: parseNum(expandEl, 1.0, 1.0),
    overrender: parseNum(overrenderEl, 2.0, 1.0),
    hitfxScaleMul: parseNum(hitfxScaleEl, 1.0, 0.05),
    fontSizeMultiplier: parseNum(fontScaleEl, 1.0, 0.05),
    noteScaleX: parseNum(noteScaleXEl, 1.0, 0.1),
    noteScaleY: parseNum(noteScaleYEl, 1.0, 0.1),
    trailAlpha: parseNum(trailAlphaEl, 0.0, 0),
    bgDim: parseNum(bgDimEl, 120, 0),
    laneCount: parseNum(laneCountEl, 8, 1),
    noteOutline: noteOutlineEl.checked,
    multicolorLines: multicolorLinesEl.checked,
    noTitleOverlay: noTitleOverlayEl.checked,
    seed: parseNum(seedEl, 42),
    playlistLimit: parseNum(limitEl, 5, 0),
    playlistShuffle: shuffleEl.checked,
    filterNameContains: nameFilterEl.value,
    filterLevelsCsv: levelFilterEl.value
  };
  localStorage.setItem(GLOBAL_KEY, JSON.stringify(cfg));
};

const loadGlobalConfig = (): void => {
  const raw = localStorage.getItem(GLOBAL_KEY);
  if (!raw) return;
  try {
    const cfg = remapSettings(JSON.parse(raw) as Record<string, unknown>);
    if (typeof cfg.simulateplay === "boolean") featureSimulateEl.checked = cfg.simulateplay;
    if (typeof cfg.autoSimulate === "boolean") featureAutoSimEl.checked = cfg.autoSimulate;
    if (typeof cfg.autoplay === "boolean") featureAutoplayEl.checked = cfg.autoplay;
    if (typeof cfg.audioSync === "boolean") featureAudioEl.checked = cfg.audioSync;
    if (typeof cfg.simulateMode === "string") simulateModeEl.value = cfg.simulateMode;
    if (typeof cfg.simulateMaxPointers === "number") simulatePtrsEl.value = String(cfg.simulateMaxPointers);
    if (typeof cfg.approachSec === "number") approachEl.value = String(cfg.approachSec);
    if (typeof cfg.noteSpeed === "number") chartSpeedEl.value = String(cfg.noteSpeed);
    if (typeof cfg.noteFlow === "number") noteFlowEl.value = String(cfg.noteFlow);
    if (typeof cfg.expand === "number") expandEl.value = String(cfg.expand);
    if (typeof cfg.overrender === "number") overrenderEl.value = String(cfg.overrender);
    if (typeof cfg.hitfxScaleMul === "number") hitfxScaleEl.value = String(cfg.hitfxScaleMul);
    if (typeof cfg.fontSizeMultiplier === "number") fontScaleEl.value = String(cfg.fontSizeMultiplier);
    if (typeof cfg.noteScaleX === "number") noteScaleXEl.value = String(cfg.noteScaleX);
    if (typeof cfg.noteScaleY === "number") noteScaleYEl.value = String(cfg.noteScaleY);
    if (typeof cfg.trailAlpha === "number") trailAlphaEl.value = String(cfg.trailAlpha);
    if (typeof cfg.bgDim === "number") bgDimEl.value = String(cfg.bgDim);
    if (typeof cfg.laneCount === "number") laneCountEl.value = String(cfg.laneCount);
    if (typeof cfg.noteOutline === "boolean") noteOutlineEl.checked = cfg.noteOutline;
    if (typeof cfg.multicolorLines === "boolean") multicolorLinesEl.checked = cfg.multicolorLines;
    if (typeof cfg.noTitleOverlay === "boolean") noTitleOverlayEl.checked = cfg.noTitleOverlay;
    if (typeof cfg.seed === "number") seedEl.value = String(cfg.seed);
    if (typeof cfg.playlistLimit === "number") limitEl.value = String(cfg.playlistLimit);
    if (typeof cfg.playlistShuffle === "boolean") shuffleEl.checked = cfg.playlistShuffle;
    if (typeof cfg.filterNameContains === "string") nameFilterEl.value = cfg.filterNameContains;
    if (typeof cfg.filterLevelsCsv === "string") levelFilterEl.value = cfg.filterLevelsCsv;
  } catch {
    // ignore corrupted cache
  }
};

const syncControls = (): void => {
  runBtn.disabled = running;
  pauseBtn.disabled = !running;
  stopBtn.disabled = !running;
  pauseBtn.textContent = pauseSignal.paused ? "Resume" : "Pause";
};

const syncFeatureAvailability = (): void => {
  const simEnabled = featureSimulateEl.checked;
  simulateModeEl.disabled = !simEnabled;
  simulatePtrsEl.disabled = !simEnabled;
  featureAutoSimEl.disabled = !simEnabled;
  simulateAdvancedEl.classList.toggle("muted", !simEnabled);
};

const log = (msg: string): void => {
  logEl.textContent = `${logEl.textContent}\n${msg}`.trim();
  logEl.scrollTop = logEl.scrollHeight;
};

const getAllFiles = (): File[] => {
  const inputFiles = Array.from(filesInput.files ?? []);
  const map = new Map<string, File>();
  [...inputFiles, ...droppedFiles].forEach((f) => map.set(`${f.name}:${f.size}`, f));
  return Array.from(map.values());
};

const updateFileState = (): void => {
  const files = getAllFiles();
  fileStateEl.textContent = `${files.length} file(s)${globalRespack ? " + respack" : ""}`;
  dropHintEl.textContent = files.length > 0 ? `Loaded: ${files.slice(0, 2).map((f) => f.name).join(", ")}${files.length > 2 ? "..." : ""}` : "Drag and drop chart files (.json/.rpe/.pec)";
};

const updateRespackInfo = (): void => {
  if (!loadedRespack || !globalRespack) {
    respackInfoEl.textContent = "Respack: (none)";
    return;
  }
  respackInfoEl.textContent = [
    `Respack: ${globalRespack.name}`,
    `hitFx=${loadedRespack.hitfxFrames[0]}x${loadedRespack.hitfxFrames[1]} duration=${loadedRespack.hitfxDuration.toFixed(2)} scale=${loadedRespack.hitfxScale.toFixed(2)}`,
    `holdAtlas=${loadedRespack.holdAtlas[0]},${loadedRespack.holdAtlas[1]} repeat=${loadedRespack.holdRepeat} compact=${loadedRespack.holdCompact}`,
    `particlesHidden=${loadedRespack.hideParticles}`
  ].join("\n");
};

clearLogBtn.addEventListener("click", () => {
  logEl.textContent = "";
});

respackUnloadBtn.addEventListener("click", () => {
  releaseRespack(loadedRespack);
  loadedRespack = null;
  globalRespack = null;
  respackInput.value = "";
  renderer.setRespack(null);
  updateFileState();
  updateRespackInfo();
  log("Respack unloaded");
});

respackTestSfxBtn.addEventListener("click", () => {
  playJudgeSfx(1, 1);
  playJudgeSfx(1, 2);
  playJudgeSfx(1, 4);
  log("Respack SFX test triggered");
});

exportSettingsBtn.addEventListener("click", () => {
  const raw = localStorage.getItem(GLOBAL_KEY);
  if (!raw) return;
  const blob = new Blob([`${JSON.stringify(JSON.parse(raw), null, 2)}\n`], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = "phic_web_settings.json";
  a.click();
  URL.revokeObjectURL(url);
});

clearFilesBtn.addEventListener("click", () => {
  droppedFiles = [];
  filesInput.value = "";
  advanceInput.value = "";
  audioInput.value = "";
  respackInput.value = "";
  releaseRespack(loadedRespack);
  loadedRespack = null;
  globalRespack = null;
  renderer.setRespack(null);
  updateFileState();
  updateRespackInfo();
  log("Cleared selected files");
});

resetGlobalBtn.addEventListener("click", () => {
  localStorage.removeItem(GLOBAL_KEY);
  log("Global settings reset. Reloading...");
  location.reload();
});

settingsFileInput.addEventListener("change", async () => {
  const file = settingsFileInput.files?.[0];
  if (!file) return;
  try {
    const text = await file.text();
    const cfg = remapSettings(JSON.parse(stripJsonc(text)) as Record<string, unknown>);
    localStorage.setItem(GLOBAL_KEY, JSON.stringify(cfg));
    loadGlobalConfig();
    syncRendererPreviewConfig();
    syncFeatureAvailability();
    log(`Imported settings: ${file.name}`);
  } catch (e) {
    log(`Failed to import settings: ${String(e)}`);
  } finally {
    settingsFileInput.value = "";
  }
});

const updateSummary = (lineA: string, lineB: string): void => {
  const acc = aggregate.judged > 0 ? aggregate.weightedAcc / aggregate.judged : 0;
  summaryEl.textContent = [`Plan: ${lineA}`, `Current: ${lineB}`, `Aggregate: runs=${aggregate.runsDone} judged=${aggregate.judged} hit=${aggregate.hit} acc=${acc.toFixed(5)}`].join("\n");
};

const syncRendererPreviewConfig = (): void => {
  renderer.setVisualConfig({
    approachSec: parseNum(approachEl, 3.0, 0.1),
    noteFlowSpeedMultiplier: parseNum(noteFlowEl, 1.0, 0.05),
    laneCount: parseNum(laneCountEl, 8, 1),
    noteScaleX: parseNum(noteScaleXEl, 1.0, 0.1),
    noteScaleY: parseNum(noteScaleYEl, 1.0, 0.1),
    expand: parseNum(expandEl, 1.0, 1.0),
    overrender: parseNum(overrenderEl, 2.0, 1.0),
    trailAlpha: Math.max(0, Math.min(1, parseNum(trailAlphaEl, 0.0, 0))),
    bgDim: Math.max(0, Math.min(255, parseNum(bgDimEl, 120, 0))),
    hitfxScaleMul: parseNum(hitfxScaleEl, 1.0, 0.05),
    fontSizeMultiplier: parseNum(fontScaleEl, 1.0, 0.05),
    noteOutline: noteOutlineEl.checked,
    multicolorLines: multicolorLinesEl.checked,
    noTitleOverlay: noTitleOverlayEl.checked
  });
};

filesInput.addEventListener("change", updateFileState);
respackInput.addEventListener("change", async () => {
  releaseRespack(loadedRespack);
  globalRespack = respackInput.files?.[0] ?? null;
  loadedRespack = null;
  renderer.setRespack(null);
  if (globalRespack) {
    log(`Global respack selected: ${globalRespack.name} (${Math.round(globalRespack.size / 1024)} KiB), loading...`);
    try {
      loadedRespack = await loadRespackFromZip(globalRespack);
      renderer.setRespack(loadedRespack);
      rebuildRespackSfx();
      log(
        `respack loaded: hitFx=${loadedRespack.hitfxFrames[0]}x${loadedRespack.hitfxFrames[1]} duration=${loadedRespack.hitfxDuration.toFixed(2)} scale=${loadedRespack.hitfxScale.toFixed(2)}`
      );
    } catch (e) {
      loadedRespack = null;
      renderer.setRespack(null);
      log(`respack load failed: ${String(e)}`);
    }
  }
  updateFileState();
  updateRespackInfo();
});

[
  simulateModeEl,
  simulatePtrsEl,
  featureSimulateEl,
  featureAutoSimEl,
  featureAutoplayEl,
  featureAudioEl,
  seedEl,
  limitEl,
  approachEl,
  chartSpeedEl,
  noteFlowEl,
  expandEl,
  overrenderEl,
  hitfxScaleEl,
  fontScaleEl,
  noteScaleXEl,
  noteScaleYEl,
  trailAlphaEl,
  bgDimEl,
  laneCountEl,
  noteOutlineEl,
  multicolorLinesEl,
  noTitleOverlayEl,
  nameFilterEl,
  levelFilterEl,
  shuffleEl
].forEach((el) => el.addEventListener("change", () => {
  saveGlobalConfig();
  syncRendererPreviewConfig();
}));
featureSimulateEl.addEventListener("change", syncFeatureAvailability);

dropzone.addEventListener("dragover", (ev) => {
  ev.preventDefault();
  dropzone.classList.add("drag-over");
});

dropzone.addEventListener("dragleave", () => {
  dropzone.classList.remove("drag-over");
});

dropzone.addEventListener("drop", (ev) => {
  ev.preventDefault();
  dropzone.classList.remove("drag-over");
  const files = Array.from(ev.dataTransfer?.files ?? []);
  if (files.length > 0) {
    droppedFiles = files;
    log(`Dropped ${files.length} files`);
    updateFileState();
  }
});

stopBtn.addEventListener("click", () => {
  stopSignal.stopped = true;
  pauseSignal.paused = false;
  log("Stop requested");
});

pauseBtn.addEventListener("click", () => {
  if (!running) return;
  pauseSignal.paused = !pauseSignal.paused;
  setStatus(pauseSignal.paused ? "Paused" : "Running", pauseSignal.paused ? "warn" : "ok");
  log(pauseSignal.paused ? "Paused" : "Resumed");
  syncControls();
});

runBtn.addEventListener("click", async () => {
  if (running) return;
  running = true;
  stopSignal.stopped = false;
  pauseSignal.paused = false;
  aggregate = { runsDone: 0, judged: 0, hit: 0, weightedAcc: 0 };
  logEl.textContent = "";
  setStatus("Preparing", "warn");
  setProgress(0, "Preparing");
  syncControls();
  updateSummary("-", "-");

  const files = getAllFiles();
  if (files.length === 0) {
    log("No files selected/dropped");
    setStatus("Idle");
    setProgress(0, "Idle");
    running = false;
    syncControls();
    return;
  }

  const advanceFile = advanceInput.files?.[0] ?? null;
  const audioFile = featureAudioEl.checked ? (audioInput.files?.[0] ?? null) : null;

  const plan = await buildRunPlan(files, {
    advanceFile,
    playlistSeed: Number(seedEl.value || "42"),
    playlistShuffle: shuffleEl.checked,
    playlistLimit: Number(limitEl.value || "0"),
    filterNameContains: nameFilterEl.value,
    filterLevelsCsv: levelFilterEl.value
  });

  if (plan.length === 0) {
    log("Run plan is empty after filtering");
    setStatus("Idle");
    setProgress(0, "Idle");
    running = false;
    syncControls();
    return;
  }

  log(`Run plan items: ${plan.length}`);
  log(`features: simulateplay=${featureSimulateEl.checked} autoStep=${featureAutoSimEl.checked} autoplay=${featureAutoplayEl.checked} audioSync=${featureAudioEl.checked}`);
  if (globalRespack) {
    log(`global respack: ${globalRespack.name}${loadedRespack ? " (active)" : " (not loaded)"}`);
  }
  setStatus("Running", "ok");
  setProgress(0, "Starting");
  updateSummary(`${plan.length} items`, "Starting");

  const engineCfg: EngineCreateConfig = {
    width: 1280,
    height: 720,
    approachSec: parseNum(approachEl, 3.0, 0.1),
    noteSpeed: parseNum(chartSpeedEl, 1.0, 0.05),
    autoplay: featureAutoplayEl.checked,
    noCull: false,
    noCullScreen: false,
    noCullEnterTime: false,
    noteOutline: noteOutlineEl.checked,
    noteScaleX: parseNum(noteScaleXEl, 1.0, 0.1),
    noteScaleY: parseNum(noteScaleYEl, 1.0, 0.1),
    noteFlowSpeedMultiplier: parseNum(noteFlowEl, 1.0, 0.05),
    expand: parseNum(expandEl, 1.0, 1.0),
    overrender: parseNum(overrenderEl, 2.0, 1.0),
    trailAlpha: Math.max(0, Math.min(1, parseNum(trailAlphaEl, 0.0, 0))),
    simulateplay: featureSimulateEl.checked,
    simulateMode: simulateModeEl.value as SimulateMode,
    simulateMaxPointers: parseNum(simulatePtrsEl, 2, 1),
    laneCount: parseNum(laneCountEl, 8, 1),
    seed: parseNum(seedEl, 42),
    hitfxScaleMul: parseNum(hitfxScaleEl, 1.0, 0.05),
    fontSizeMultiplier: parseNum(fontScaleEl, 1.0, 0.05),
    bgDim: Math.max(0, Math.min(255, parseNum(bgDimEl, 120, 0))),
    multicolorLines: multicolorLinesEl.checked,
    noTitleOverlay: noTitleOverlayEl.checked
  };

  renderer.setVisualConfig({
    approachSec: engineCfg.approachSec,
    noteFlowSpeedMultiplier: engineCfg.noteFlowSpeedMultiplier,
    laneCount: engineCfg.laneCount,
    noteScaleX: engineCfg.noteScaleX,
    noteScaleY: engineCfg.noteScaleY,
    expand: engineCfg.expand,
    overrender: engineCfg.overrender,
    trailAlpha: engineCfg.trailAlpha,
    bgDim: engineCfg.bgDim,
    hitfxScaleMul: engineCfg.hitfxScaleMul,
    fontSizeMultiplier: engineCfg.fontSizeMultiplier,
    noteOutline: engineCfg.noteOutline,
    multicolorLines: engineCfg.multicolorLines,
    noTitleOverlay: engineCfg.noTitleOverlay
  });
  renderer.setRespack(loadedRespack);

  try {
    await runPlan(
      plan,
      engineCfg,
      { stepHz: 120, autoSimulate: featureAutoSimEl.checked, stopSignal, pauseSignal, audioFile },
      {
        onRunStart: (run) => {
          noteKindById.clear();
          log(`Start ${run.label}: ${run.file.name}`);
          updateSummary(`${plan.length} items`, `${run.label} ${run.file.name}`);
          renderer.setHudText(`${run.label} · ${run.file.name}`);
        },
        onRunEnd: (run, result) => {
          aggregate.runsDone += 1;
          aggregate.judged += result.stats.judgedCnt;
          aggregate.hit += result.stats.hitTotal;
          aggregate.weightedAcc += result.stats.accuracy * Math.max(1, result.stats.judgedCnt);
          log(`End ${run.label}: judged=${result.stats.judgedCnt} hit=${result.stats.hitTotal} acc=${result.stats.accuracy.toFixed(4)}`);
          updateSummary(`${plan.length} items`, `${run.label} done`);
        },
        onRunProgress: (runIndex, runCount, elapsedSec, maxSec) => {
          const p = maxSec > 0 ? Math.min(100, (elapsedSec / maxSec) * 100) : 0;
          summaryEl.textContent = summaryEl.textContent.replace(/^Plan:.*$/m, `Plan: ${runIndex}/${runCount} (${p.toFixed(1)}%)`);
          const overall = ((runIndex - 1) + p / 100) / Math.max(1, runCount);
          setProgress(overall * 100, `${runIndex}/${runCount} ${p.toFixed(1)}%`);
        },
        onFrame: (result) => {
          for (const cmd of result.commands) {
            noteKindById.set(cmd.noteId, cmd.kind);
          }
          if (result.judgeEvents.length > 0) {
            renderer.pushJudgeEvents(result.judgeEvents);
            for (const ev of result.judgeEvents) {
              playJudgeSfx(ev.kind, ev.noteKind > 0 ? ev.noteKind : noteKindById.get(ev.noteId));
            }
          }
          renderer.draw(result.commands, result.stats.timeSec);
          statsEl.textContent = [
            `time=${result.stats.timeSec.toFixed(3)}`,
            `combo=${result.stats.combo}`,
            `maxCombo=${result.stats.maxCombo}`,
            `judged=${result.stats.judgedCnt}`,
            `hit=${result.stats.hitTotal}`,
            `accuracy=${result.stats.accuracy.toFixed(6)}`
          ].join("\n");
        },
        onLog: log
      }
    );
    setStatus(stopSignal.stopped ? "Stopped" : "Completed", stopSignal.stopped ? "warn" : "ok");
    setProgress(stopSignal.stopped ? 0 : 100, stopSignal.stopped ? "Stopped" : "Completed");
  } catch (e) {
    log(`Run failed: ${String(e)}`);
    setStatus("Failed", "error");
    setProgress(0, "Failed");
  } finally {
    running = false;
    pauseSignal.paused = false;
    syncControls();
  }
});

loadGlobalConfig();
updateFileState();
updateRespackInfo();
syncControls();
syncFeatureAvailability();
syncRendererPreviewConfig();
