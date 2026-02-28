export interface RunItem {
  label: string;
  file: File;
  format: string;
  startTime?: number;
  endTime?: number;
}

export interface PlannerOptions {
  advanceFile?: File | null;
  playlistSeed: number;
  playlistShuffle: boolean;
  playlistLimit: number;
  filterNameContains: string;
  filterLevelsCsv: string;
}

function inferFormat(name: string): string {
  const n = name.toLowerCase();
  if (n.endsWith(".pec")) return "pec";
  if (n.endsWith(".rpe")) return "rpe";
  return "official";
}

function levelMatch(name: string, csv: string): boolean {
  if (!csv.trim()) return true;
  const upper = name.toUpperCase();
  return csv
    .split(/[;,]/g)
    .map((s) => s.trim().toUpperCase())
    .filter(Boolean)
    .some((token) => upper.includes(token));
}

function normalizeChartFiles(files: File[]): File[] {
  return files.filter((f) => /\.(json|rpe|pec)$/i.test(f.name) && !/^(info|meta)\.json$/i.test(f.name));
}

function seededShuffle<T>(arr: T[], seed: number): T[] {
  const out = arr.slice();
  let s = seed || 42;
  const rand = () => {
    s = (1664525 * s + 1013904223) >>> 0;
    return s / 0xffffffff;
  };
  for (let i = out.length - 1; i > 0; i -= 1) {
    const j = Math.floor(rand() * (i + 1));
    [out[i], out[j]] = [out[j], out[i]];
  }
  return out;
}

function stripJsonc(text: string): string {
  let s = text;
  s = s.replace(/\/\*[\s\S]*?\*\//g, "");
  s = s.replace(/(^|[^:])\/\/.*$/gm, "$1");
  s = s.replace(/,\s*([}\]])/g, "$1");
  return s;
}

export async function buildRunPlan(files: File[], options: PlannerOptions): Promise<RunItem[]> {
  const allCharts = normalizeChartFiles(files);
  const byName = new Map(allCharts.map((f) => [f.name, f]));

  const plan: RunItem[] = [];

  if (options.advanceFile) {
    try {
      const raw = await options.advanceFile.text();
      const parsed = JSON.parse(stripJsonc(raw)) as { tracks?: Array<Record<string, unknown>>; sequence?: Array<Record<string, unknown>> };
      const tracks = parsed.tracks ?? parsed.sequence ?? [];
      tracks.forEach((t, idx) => {
        const input = typeof t.input === "string" ? t.input : "";
        const file = byName.get(input) ?? byName.get(input.split("/").pop() || "");
        if (!file) return;
        plan.push({
          label: `advance[${idx}]`,
          file,
          format: typeof t.format === "string" ? t.format : inferFormat(file.name),
          startTime: typeof t.start_at === "number" ? t.start_at : undefined,
          endTime: typeof t.end_at === "number" ? t.end_at : undefined
        });
      });
    } catch {
      // Keep plan from playlist fallback.
    }
  }

  let playlist = allCharts.filter((f) => {
    if (options.filterNameContains && !f.name.includes(options.filterNameContains)) return false;
    if (!levelMatch(f.name, options.filterLevelsCsv)) return false;
    return true;
  });

  if (options.playlistShuffle) {
    playlist = seededShuffle(playlist, options.playlistSeed);
  } else {
    playlist = playlist.slice().sort((a, b) => a.name.localeCompare(b.name));
  }

  if (options.playlistLimit > 0 && playlist.length > options.playlistLimit) {
    playlist = playlist.slice(0, options.playlistLimit);
  }

  playlist.forEach((file, idx) => {
    plan.push({
      label: `playlist[${idx}]`,
      file,
      format: inferFormat(file.name)
    });
  });

  return plan;
}
