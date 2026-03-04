#pragma once
#include "phigros/chart/compiled_chart.hpp"
#include <iosfwd>
#include <string>

namespace phigros::chart {

// Binary chart format (.phbc) — little-endian, version 1.
//
// Layout:
//   Header (52 bytes):
//     [0-3]   char magic[4] = "PHBC"
//     [4-5]   uint16_t version = 1
//     [6-7]   uint16_t flags = 0
//     [8-15]  double offset
//     [16-23] double chart_end_t
//     [24-27] int32_t playable_count
//     [28-31] int32_t note_count
//     [32-35] int32_t line_count
//     [36-39] float sample_rate
//     [40-47] double t_start
//     [48-51] int32_t sample_count
//
//   Per-line (line_count):
//     int32_t lid
//     uint8_t color_r, color_g, color_b, _pad
//     int32_t dyn_color   (1 if dynamic color arrays follow, else 0)
//     if dyn_color: sample_count × float color_r, color_g, color_b
//     sample_count × float pos_x
//     sample_count × float pos_y
//     sample_count × float rot
//     sample_count × float alpha
//     sample_count × float scroll
//
//   Per-note (note_count):
//     int32_t nid, line_id, kind
//     uint8_t above, fake, mh, _pad
//     double  t_hit, t_end, t_enter, scroll_hit, scroll_end
//     double  x_local_px, y_offset_px
//     float   speed_mul, size_px, alpha01
//     uint8_t tint_r, tint_g, tint_b, _pad

// Write a CompiledChartData to os in PHBC format.
void write_phbc(const CompiledChartData& c, std::ostream& os);

// Read a CompiledChartData from is. Throws std::runtime_error on format mismatch.
CompiledChartData read_phbc(std::istream& is);

} // namespace phigros::chart
