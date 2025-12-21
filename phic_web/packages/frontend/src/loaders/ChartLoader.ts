/**
 * Chart file loader for web
 * Loads and parses chart files (Official, RPE, PEC formats)
 * Ported from phic_renderer/io/chart_loader_impl.py
 */

import { ParsedChart } from '@phic-web/shared'
import { loadOfficial, loadRPE, loadPECText } from '@phic-web/backend/parsers'

/**
 * Chart format type
 */
export type ChartFormat = 'official' | 'rpe' | 'pec'

/**
 * Loaded chart with metadata
 */
export interface LoadedChart {
  format: ChartFormat
  chart: ParsedChart
  fileName: string
}

/**
 * Detect chart format from JSON data
 */
function detectFormat(data: any): ChartFormat {
  // RPE format has META and BPMList
  if (data.META && data.BPMList && data.judgeLineList) {
    return 'rpe'
  }

  // Official format has formatVersion
  if (data.formatVersion && data.judgeLineList) {
    return 'official'
  }

  // RPE can also have eventLayers
  if (data.judgeLineList && data.judgeLineList.some((jl: any) => jl.eventLayers)) {
    return 'rpe'
  }

  // Default to official
  return 'official'
}

/**
 * Load chart from file
 * @param file - Chart file (JSON or PEC text)
 * @param width - Canvas width (default: 1920)
 * @param height - Canvas height (default: 1080)
 */
export async function loadChart(
  file: File,
  width: number = 1920,
  height: number = 1080
): Promise<LoadedChart> {
  const fileName = file.name.toLowerCase()

  // Check if it's a PEC file by extension
  if (fileName.endsWith('.pec') || fileName.endsWith('.pe')) {
    const text = await file.text()
    const chart = loadPECText(text, width, height)
    return {
      format: 'pec',
      chart,
      fileName: file.name,
    }
  }

  // Try to parse as JSON first
  const text = await file.text()
  try {
    const data = JSON.parse(text)
    const format = detectFormat(data)

    let chart: ParsedChart
    if (format === 'official') {
      chart = loadOfficial(data, width, height)
    } else {
      chart = loadRPE(data, width, height)
    }

    return {
      format,
      chart,
      fileName: file.name,
    }
  } catch (error) {
    // If JSON parsing fails, try as PEC text
    try {
      const chart = loadPECText(text, width, height)
      return {
        format: 'pec',
        chart,
        fileName: file.name,
      }
    } catch (pecError) {
      throw new Error(`Failed to parse chart file: ${error}`)
    }
  }
}

/**
 * Load chart from URL (for testing/demo)
 */
export async function loadChartFromURL(
  url: string,
  width: number = 1920,
  height: number = 1080
): Promise<LoadedChart> {
  const response = await fetch(url)
  const blob = await response.blob()
  const file = new File([blob], url.split('/').pop() ?? 'chart.json')
  return loadChart(file, width, height)
}
