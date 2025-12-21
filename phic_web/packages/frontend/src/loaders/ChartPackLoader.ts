/**
 * Chart pack loader for web
 * Loads chart packs (ZIP files containing chart + music + background + metadata)
 * Ported from phic_renderer/io/chart_pack_impl.py
 */

import JSZip from 'jszip'
import { loadChart, type LoadedChart } from './ChartLoader.js'

/**
 * Chart pack metadata from info.yml
 */
export interface ChartPackInfo {
  chart?: string // Chart filename (default: chart.json)
  music?: string // Music filename (default: song.mp3)
  illustration?: string // Background filename (default: background.png)
  [key: string]: any // Other custom fields
}

/**
 * Loaded chart pack with all assets
 */
export interface ChartPack {
  info: ChartPackInfo
  chart: LoadedChart
  musicBlob: Blob | null
  backgroundBlob: Blob | null
}

/**
 * Parse info.yml (minimal YAML parser)
 * Shared implementation with RespackLoader
 */
function parseInfoYml(text: string): ChartPackInfo {
  const info: any = {}

  // Helper to strip inline comments
  function stripInlineComment(s: string): string {
    let inSq = false
    let inDq = false
    const buf: string[] = []

    for (const ch of s) {
      if (ch === "'" && !inDq) {
        inSq = !inSq
      } else if (ch === '"' && !inSq) {
        inDq = !inDq
      }

      if (!inSq && !inDq && ch === '#') {
        break
      }
      buf.push(ch)
    }

    return buf.join('').trimEnd()
  }

  for (const raw of text.split('\n')) {
    const line = stripInlineComment(raw).trim()

    if (!line || line.startsWith('#')) continue
    if (!line.includes(':')) continue

    const colonIdx = line.indexOf(':')
    const k = line.substring(0, colonIdx).trim()
    let v = line.substring(colonIdx + 1).trim().replace(/^["']|["']$/g, '')

    // Parse array values [a, b, c]
    if (v.startsWith('[') && v.endsWith(']')) {
      const inside = v.substring(1, v.length - 1).trim()
      if (inside) {
        const parts = inside.split(',').map(p => p.trim())
        const arr: any[] = []
        for (const p of parts) {
          const numVal = Number(p)
          if (!isNaN(numVal)) {
            arr.push(numVal)
          } else {
            arr.push(p.replace(/^["']|["']$/g, ''))
          }
        }
        info[k] = arr
      } else {
        info[k] = []
      }
    } else {
      // Parse scalar values
      if (v.toLowerCase() === 'true' || v.toLowerCase() === 'false') {
        info[k] = v.toLowerCase() === 'true'
      } else {
        const numVal = Number(v)
        if (!isNaN(numVal)) {
          info[k] = numVal
        } else {
          info[k] = v
        }
      }
    }
  }

  return info
}

/**
 * Load chart pack from ZIP file
 */
export async function loadChartPack(
  file: File | Blob,
  width: number = 1920,
  height: number = 1080
): Promise<ChartPack> {
  const zip = await JSZip.loadAsync(file)

  // Load info.yml
  const infoFile = zip.file('info.yml')
  if (!infoFile) {
    throw new Error('Missing info.yml in chart pack')
  }
  const infoText = await infoFile.async('text')
  const info = parseInfoYml(infoText)

  // Get filenames from info.yml (with defaults)
  const chartFilename = info.chart ?? 'chart.json'
  const musicFilename = info.music ?? 'song.mp3'
  const bgFilename = info.illustration ?? 'background.png'

  // Load chart file
  const chartFile = zip.file(chartFilename)
  if (!chartFile) {
    throw new Error(`Chart file not found: ${chartFilename}`)
  }
  const chartBlob = await chartFile.async('blob')
  const chartFileObj = new File([chartBlob], chartFilename)
  const chart = await loadChart(chartFileObj, width, height)

  // Load music file (optional)
  let musicBlob: Blob | null = null
  const musicFile = zip.file(musicFilename)
  if (musicFile) {
    musicBlob = await musicFile.async('blob')
  }

  // Load background image (optional)
  let backgroundBlob: Blob | null = null
  const bgFile = zip.file(bgFilename)
  if (bgFile) {
    backgroundBlob = await bgFile.async('blob')
  }

  return {
    info,
    chart,
    musicBlob,
    backgroundBlob,
  }
}
