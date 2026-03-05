#pragma once
#include "phigros/core/types.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/config/render_config.hpp"
#include "phigros/hud/hud.hpp"
#include <vector>
#include <array>
#include <algorithm>
#include <string>

namespace phigros::render {

// Note type colors (matching Python's NOTE_TYPE_COLORS)
inline math::RGB note_type_color(int kind) {
    switch (kind) {
        case 1: return {255, 220, 120}; // Tap
        case 2: return {140, 240, 255}; // Drag
        case 3: return {120, 200, 255}; // Hold
        case 4: return {255, 140, 220}; // Flick
        default: return {255, 255, 255};
    }
}

// Frame snapshot: computed per-frame state for all lines and visible notes
struct LineSnapshot {
    int lid;
    double x, y, rot, cos_rot, sin_rot, alpha01, scroll;
    math::RGB color;
};

struct NoteSnapshot {
    int nid;
    int kind;
    double wx, wy;           // world position (head)
    double wx_tail, wy_tail; // world position (tail, for holds)
    double alpha;
    double line_rot;         // line rotation for note alignment
    double size_px = 1.0;    // per-note size multiplier (from Note::size_px)
    math::RGB color;
    bool is_hold;
    bool judged;
    bool miss;
    bool mh;                 // multi-hit flag
    bool holding = false;    // true while a hold note is actively held
    bool draw_hold_head = true; // false when holding and respack holdKeepHead=false
};

struct FrameSnapshot {
    double t;
    std::vector<LineSnapshot> lines;
    std::vector<NoteSnapshot> notes;
    hud::HudState hud;
};

// Build a full frame snapshot for time t
// Apply Python-style expand transform: compress world coords toward screen centre
// by 1/expand, making a larger portion of the playfield visible.
// Matches phic_renderer's apply_expand_xy(x, y, W, H, expand).
inline void apply_expand_xy(double& x, double& y, int W, int H, double expand) {
    if (expand <= 1.000001) return;
    double cx = W * 0.5, cy = H * 0.5;
    double s = 1.0 / expand;
    x = cx + (x - cx) * s;
    y = cy + (y - cy) * s;
}

inline FrameSnapshot build_frame(
    double t,
    const ChartData& chart,
    const std::vector<NoteState>& states,
    const engine::Judge& judge,
    const config::RenderConfig& cfg)
{
    FrameSnapshot frame;
    frame.t = t;

    // Evaluate all line states.
    // Fast path: stack storage for charts <= 256 lines.
    // Fallback: heap storage for larger charts (avoids silently dropping notes).
    std::array<engine::LineState, 256> ls_arr{};
    std::vector<engine::LineState> ls_heap;
    const size_t n_lines = chart.lines.size();
    if (n_lines > ls_arr.size()) ls_heap.resize(n_lines);
    frame.lines.reserve(n_lines);
    for (size_t i = 0; i < n_lines; ++i) {
        auto& ln = chart.lines[i];
        auto ls = engine::eval_line_state(
            ln, t, cfg.force_line_alpha01,
            cfg.force_line_alpha01_by_lid
                ? &(*cfg.force_line_alpha01_by_lid) : nullptr);
        if (i < ls_arr.size()) ls_arr[i] = ls;
        else                   ls_heap[i] = ls;
        frame.lines.push_back({
            ln.lid, ls.x, ls.y, ls.rot, ls.cos_rot, ls.sin_rot,
            ls.alpha01, ls.scroll,
            ln.compiled_color ? ln.compiled_color(t)
                              : (ln.color ? ln.color->eval(t) : ln.color_rgb)
        });
    }

    // Evaluate visible notes — binary search bounds on sorted notes (6B1 + 6B5)
    static constexpr double MAX_HOLD_SEC = 12.0;
    double flow_mul = cfg.note_flow_speed_multiplier;
    static thread_local size_t s_last_note_count = 32;
    frame.notes.reserve(s_last_note_count + 8);

    // Helper: emit one note into the frame snapshot (shared by normal + early paths)
    auto emit_note = [&](size_t i) {
        const auto& note = chart.notes[i];
        const auto& ns   = states[i];

        if (ns.judged && note.kind != 3) return;
        if (ns.miss) return;
        if (note.t_end < t - 0.5) return;
        if (note.line_id < 0 || note.line_id >= static_cast<int>(n_lines)) return;
        const auto& ls = (static_cast<size_t>(note.line_id) < ls_arr.size())
            ? ls_arr[note.line_id]
            : ls_heap[note.line_id];

        auto head = engine::note_world_pos_cs(
            ls.x, ls.y, ls.cos_rot, ls.sin_rot, ls.scroll, note,
            note.scroll_hit, false, flow_mul,
            cfg.note_speed_mul_affects_travel,
            note.kind == 3 && ns.holding);  // clamp head to line while holding

        double wx_tail = head.x, wy_tail = head.y;
        if (note.kind == 3) {
            auto tail = engine::note_world_pos_cs(
                ls.x, ls.y, ls.cos_rot, ls.sin_rot, ls.scroll, note,
                note.scroll_end, true, flow_mul,
                cfg.note_speed_mul_affects_travel, false);
            wx_tail = tail.x;
            wy_tail = tail.y;
        }

        auto color = note_type_color(note.kind);
        double note_alpha = note.alpha01 * cfg.note_alpha;
        switch (cfg.line_alpha_mode) {
        case config::LineAlphaMode::Always:
            note_alpha *= ls.alpha01;
            break;
        case config::LineAlphaMode::NegativeOnly:
            if (ls.alpha01 < 0.5)
                note_alpha *= ls.alpha01 * 2.0;
            break;
        default: break;
        }

        frame.notes.push_back({
            note.nid, note.kind, head.x, head.y,
            wx_tail, wy_tail, note_alpha, ls.rot, note.size_px, color,
            note.kind == 3, ns.judged, ns.miss, note.mh,
            ns.holding,
            !(note.kind == 3 && ns.holding)  // draw_hold_head: hide head sprite while holding
        });
    };

    // Normal approach window: notes with t_hit in [t-12, t+approach*2]
    auto lo_it = std::lower_bound(chart.notes.begin(), chart.notes.end(),
        t - MAX_HOLD_SEC,
        [](const Note& n, double v) { return n.t_hit < v; });
    auto hi_it = std::upper_bound(chart.notes.begin(), chart.notes.end(),
        t + cfg.approach * 2.0,
        [](double v, const Note& n) { return v < n.t_hit; });

    for (auto it = lo_it; it != hi_it; ++it) {
        const size_t i = static_cast<size_t>(it - chart.notes.begin());
        const auto& note = *it;

        // Optional time-based culling via precomputed t_enter (disabled by default).
        // Enable with no_cull_enter_time = false in config.
        // t_enter must have been computed with the same expand_factor as cfg.expand_factor.
        if (!cfg.no_cull_enter_time) {
            if (t < note.t_enter) continue;
        }
        emit_note(i);
    }

    // Early visible notes ("acting notes"): notes outside the normal approach window
    // that should be drawn because their t_enter is much earlier than t_hit.
    // These are typically notes on lines with speed=0 segments ("release" pattern).
    if (!chart.early_notes.empty()) {
        // Track which indices were already emitted by the normal loop
        // (early_notes entries whose t_hit falls in the normal window are already handled)
        double normal_lo = (lo_it != chart.notes.end()) ? lo_it->t_hit : 1e18;
        double normal_hi = (hi_it != chart.notes.begin()) ? (hi_it - 1)->t_hit : -1e18;

        for (size_t idx : chart.early_notes) {
            const auto& note = chart.notes[idx];
            // Skip if already in the normal window
            if (note.t_hit >= normal_lo - 0.001 && note.t_hit <= normal_hi + 0.001)
                continue;
            // Only draw if we're past t_enter and before t_hit + hold tail
            if (t < note.t_enter) continue;
            if (note.t_end < t - 0.5) continue;
            emit_note(idx);
        }
    }

    s_last_note_count = frame.notes.size();  // 6B5: update adaptive reserve hint

    // Sort by (non-hold first for z-order, then by kind) to batch same-texture notes
    // and reduce SDL texture state thrashing in SdlExecutor
    std::stable_sort(frame.notes.begin(), frame.notes.end(),
        [](const NoteSnapshot& a, const NoteSnapshot& b) {
            if (a.is_hold != b.is_hold) return a.is_hold < b.is_hold;
            return a.kind < b.kind;
        });

    // Update HUD — use precomputed metadata to avoid O(N) loops
    auto sr = engine::compute_score(judge.acc_sum, judge.max_combo,
                                     static_cast<int>(chart.notes.size()));
    hud::update_hud(frame.hud, sr.score, sr.acc_ratio,
                    judge.combo, judge.max_combo, t,
                    chart.chart_end_t, chart.playable_count);

    return frame;
}

} // namespace phigros::render
