import JSZip from "jszip";

export interface RgbaColor {
  r: number;
  g: number;
  b: number;
  a: number;
}

export interface LoadedRespack {
  info: Record<string, unknown>;
  images: Partial<Record<RespackImageName, HTMLImageElement>>;
  sounds: Partial<Record<RespackSoundName, string>>;
  hitfxFrames: [number, number];
  hitfxDuration: number;
  hitfxScale: number;
  hitfxRotate: boolean;
  hitfxTinted: boolean;
  holdAtlas: [number, number];
  holdAtlasMh: [number, number];
  holdRepeat: boolean;
  holdCompact: boolean;
  holdKeepHead: boolean;
  holdTailNoScale: boolean;
  hideParticles: boolean;
  judgeColors: Record<"PERFECT" | "GOOD" | "BAD" | "MISS", RgbaColor>;
}

type RespackImageName =
  | "click.png"
  | "drag.png"
  | "flick.png"
  | "hold.png"
  | "click_mh.png"
  | "drag_mh.png"
  | "flick_mh.png"
  | "hold_mh.png"
  | "hit_fx.png"
  | "hit_fx.good.png";
type RespackSoundName = "click.ogg" | "drag.ogg" | "flick.ogg";

const REQUIRED_IMAGES: RespackImageName[] = [
  "click.png",
  "drag.png",
  "flick.png",
  "hold.png",
  "click_mh.png",
  "drag_mh.png",
  "flick_mh.png",
  "hold_mh.png",
  "hit_fx.png"
];
const OPTIONAL_IMAGES: RespackImageName[] = ["hit_fx.good.png"];
const OPTIONAL_SOUNDS: RespackSoundName[] = ["click.ogg", "drag.ogg", "flick.ogg"];

function tryNumber(v: unknown, fallback: number): number {
  const n = Number(v);
  return Number.isFinite(n) ? n : fallback;
}

function toBool(v: unknown, fallback = false): boolean {
  if (typeof v === "boolean") return v;
  if (typeof v === "string") {
    const s = v.trim().toLowerCase();
    if (s === "true" || s === "1" || s === "yes") return true;
    if (s === "false" || s === "0" || s === "no") return false;
  }
  return fallback;
}

function parseHexColor(v: unknown, fallback: RgbaColor): RgbaColor {
  if (typeof v === "undefined" || v === null) return fallback;
  let n = Number.NaN;
  if (typeof v === "number") {
    n = v;
  } else {
    const s = String(v).trim();
    n = Number(s.startsWith("0x") ? s : Number.parseInt(s, 0));
  }
  if (!Number.isFinite(n)) return fallback;
  const value = Math.max(0, Math.floor(n));
  if (value <= 0xffffff) {
    return { r: (value >> 16) & 255, g: (value >> 8) & 255, b: value & 255, a: 255 };
  }
  return { r: (value >> 16) & 255, g: (value >> 8) & 255, b: value & 255, a: (value >> 24) & 255 };
}

function stripComment(line: string): string {
  let sq = false;
  let dq = false;
  let out = "";
  for (const ch of line) {
    if (ch === "'" && !dq) sq = !sq;
    if (ch === '"' && !sq) dq = !dq;
    if (ch === "#" && !sq && !dq) break;
    out += ch;
  }
  return out.trim();
}

function parseScalar(raw: string): unknown {
  const s = raw.trim().replace(/^['"]|['"]$/g, "");
  if (s.length === 0) return "";
  if (s === "true") return true;
  if (s === "false") return false;
  const maybeNum = Number(s);
  if (Number.isFinite(maybeNum)) return maybeNum;
  return s;
}

function parseInfoYmlLoose(text: string): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const rawLine of text.split(/\r?\n/g)) {
    const line = stripComment(rawLine);
    if (!line || !line.includes(":")) continue;
    const idx = line.indexOf(":");
    const key = line.slice(0, idx).trim();
    const valuePart = line.slice(idx + 1).trim();
    if (!key) continue;
    if (valuePart.startsWith("[") && valuePart.endsWith("]")) {
      const inner = valuePart.slice(1, -1).trim();
      out[key] = inner ? inner.split(",").map((x) => parseScalar(x)) : [];
      continue;
    }
    out[key] = parseScalar(valuePart);
  }
  return out;
}

async function readImageEntry(entry: JSZip.JSZipObject): Promise<HTMLImageElement> {
  const blob = await entry.async("blob");
  const url = URL.createObjectURL(blob);
  try {
    const img = await new Promise<HTMLImageElement>((resolve, reject) => {
      const node = new Image();
      node.onload = () => resolve(node);
      node.onerror = (err) => reject(err);
      node.src = url;
    });
    return img;
  } finally {
    URL.revokeObjectURL(url);
  }
}

function pickEntry(zip: JSZip, baseName: string): JSZip.JSZipObject | null {
  let exact = zip.file(baseName);
  if (exact) return exact;
  const lowered = baseName.toLowerCase();
  exact = zip.file(new RegExp(`(^|/)${baseName.replace(".", "\\.")}$`, "i"))?.[0] ?? null;
  if (exact) return exact;
  const all = zip.file(/.*/g);
  return all.find((f) => f.name.toLowerCase().endsWith(`/${lowered}`) || f.name.toLowerCase() === lowered) ?? null;
}

export async function loadRespackFromZip(file: File): Promise<LoadedRespack> {
  const zip = await JSZip.loadAsync(file);
  const infoEntry = pickEntry(zip, "info.yml");
  const info = infoEntry ? parseInfoYmlLoose(await infoEntry.async("text")) : {};

  const images: LoadedRespack["images"] = {};
  for (const name of REQUIRED_IMAGES) {
    const entry = pickEntry(zip, name);
    if (!entry) throw new Error(`respack missing required image: ${name}`);
    images[name] = await readImageEntry(entry);
  }
  for (const name of OPTIONAL_IMAGES) {
    const entry = pickEntry(zip, name);
    if (entry) images[name] = await readImageEntry(entry);
  }

  const sounds: LoadedRespack["sounds"] = {};
  for (const name of OPTIONAL_SOUNDS) {
    const entry = pickEntry(zip, name);
    if (!entry) continue;
    const blob = await entry.async("blob");
    sounds[name] = URL.createObjectURL(blob);
  }

  const hitFxRaw = Array.isArray(info.hitFx) ? info.hitFx : [5, 6];
  const holdAtlasRaw = Array.isArray(info.holdAtlas) ? info.holdAtlas : [50, 50];
  const holdAtlasMhRaw = Array.isArray(info.holdAtlasMH) ? info.holdAtlasMH : holdAtlasRaw;

  return {
    info,
    images,
    sounds,
    hitfxFrames: [Math.max(1, Math.floor(tryNumber(hitFxRaw[0], 5))), Math.max(1, Math.floor(tryNumber(hitFxRaw[1], 6)))],
    hitfxDuration: Math.max(0.01, tryNumber(info.hitFxDuration, 0.5)),
    hitfxScale: Math.max(0.05, tryNumber(info.hitFxScale, 1.0)),
    hitfxRotate: toBool(info.hitFxRotate, false),
    hitfxTinted: toBool(info.hitFxTinted, true),
    holdAtlas: [Math.max(1, Math.floor(tryNumber(holdAtlasRaw[0], 50))), Math.max(1, Math.floor(tryNumber(holdAtlasRaw[1], 50)))],
    holdAtlasMh: [Math.max(1, Math.floor(tryNumber(holdAtlasMhRaw[0], 50))), Math.max(1, Math.floor(tryNumber(holdAtlasMhRaw[1], 50)))],
    holdRepeat: toBool(info.holdRepeat, false),
    holdCompact: toBool(info.holdCompact, false),
    holdKeepHead: toBool(info.holdKeepHead, false),
    holdTailNoScale: toBool(info.holdTailNoScale, false),
    hideParticles: toBool(info.hideParticles, false),
    judgeColors: {
      PERFECT: parseHexColor(info.colorPerfect, { r: 255, g: 255, b: 255, a: 255 }),
      GOOD: parseHexColor(info.colorGood, { r: 180, g: 220, b: 255, a: 255 }),
      BAD: parseHexColor(info.colorBad, { r: 255, g: 180, b: 180, a: 255 }),
      MISS: parseHexColor(info.colorMiss, { r: 200, g: 200, b: 200, a: 255 })
    }
  };
}

