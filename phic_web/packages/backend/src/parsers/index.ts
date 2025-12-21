/**
 * Universal chart parser
 * Auto-detects format and routes to appropriate parser
 */

import { loadOfficial } from './official.js'
import { loadRPE, isRPEFormat } from './rpe.js'
import { loadPEC, isPECFormat } from './pec.js'
import type { ParsedChart } from '@phic-web/shared'

export type ChartFormat = 'official' | 'rpe' | 'pec' | 'unknown'

/**
 * Detect chart format from data
 */
export function detectFormat(data: any): ChartFormat {
  if (!data) return 'unknown'

  // Check for RPE
  if (isRPEFormat(data)) return 'rpe'

  // Check for PEC
  if (isPECFormat(data)) return 'pec'

  // Check for official format
  if (data.formatVersion && data.judgeLineList) return 'official'

  return 'unknown'
}

/**
 * Parse chart data with automatic format detection
 */
export function parseChart(
  data: any,
  W: number = 1920,
  H: number = 1080
): ParsedChart {
  const format = detectFormat(data)

  switch (format) {
    case 'official':
      return loadOfficial(data, W, H)

    case 'rpe':
      return loadRPE(data, W, H)

    case 'pec':
      return loadPEC(data, W, H)

    default:
      throw new Error(`Unknown or unsupported chart format`)
  }
}

// Re-export individual parsers
export { loadOfficial } from './official.js'
export { loadRPE } from './rpe.js'
export { loadPEC, loadPECText } from './pec.js'

export { precomputeTEnter } from './visibility.js'
