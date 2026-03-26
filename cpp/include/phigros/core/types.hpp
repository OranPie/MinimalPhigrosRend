#pragma once
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <memory>
#include "phigros/math/util.hpp"
#include "phigros/math/tracks.hpp"

namespace phigros {

// Note kind constants
enum class NoteKind : int { Tap = 1, Drag = 2, Hold = 3, Flick = 4 };

struct Note {
    int nid = 0;
    int line_id = 0;
    int kind = 1;         // 1=tap, 2=drag, 3=hold, 4=flick
    bool above = true;
    bool fake = false;
    double t_hit = 0.0;
    double t_end = 0.0;
    double x_local_px = 0.0;
    double y_offset_px = 0.0;
    double speed_mul = 1.0;
    double size_px = 1.0;
    double alpha01 = 1.0;

    math::RGB tint_rgb{255, 255, 255};
    std::optional<math::RGB> tint_hitfx_rgb;

    // Cached scroll at key times
    double scroll_hit = 0.0;
    double scroll_end = 0.0;

    // RPE custom hitsound
    std::string hitsound_path;

    // RPE visibleTime: note is invisible until t_hit - visible_time seconds before hit.
    // Default 999999 = always visible. Stored in seconds.
    double visible_time = 999999.0;

    // Precomputed visibility
    double t_enter = -1e9;
    bool mh = false; // multi-hit simultaneous flag
};

// Track callable: eval(t) → double
using TrackFn = std::function<double(double)>;

// Color callable: eval(t) → RGB (used by compiled charts to override per-line color)
using ColorFn = std::function<math::RGB(double)>;

// RPE control event: per-scroll-distance note property modifier.
// The 'x' field is the note's perpendicular distance from the judge line in RPE y-units
// (i.e. scroll distance: positive = note is ahead of the line, not yet hit).
// Used for alphaControl, posControl, sizeControl, yControl, skewControl.
struct CtrlPoint {
    float x     = 0.0f;  // note's y-distance from judge line (RPE y-units, scroll distance)
    float value = 1.0f;  // property value at this distance
    int   easing = 1;    // easing type (1=linear)
};

struct Line {
    int lid = 0;

    // Core tracks (callable)
    TrackFn pos_x;
    TrackFn pos_y;
    TrackFn rot;
    TrackFn alpha;

    // Scroll (integral track, owned directly)
    math::IntegralTrack scroll_px;

    // Optional compiled override: if set, used instead of scroll_px.integral() in kinematics.
    // Set by compile_chart(); not used by live source-format charts.
    TrackFn scroll_fn;

    // Static color
    math::RGB color_rgb{255, 255, 255};

    // Extended (RPE) fields
    std::shared_ptr<math::PiecewiseColor> color;

    // Optional compiled color override: if set, used instead of color / color_rgb in build_frame.
    ColorFn compiled_color;
    std::shared_ptr<math::PiecewiseEased> scale_x;
    std::shared_ptr<math::PiecewiseEased> scale_y;
    std::shared_ptr<math::PiecewiseText> text;
    std::string texture_path;
    std::pair<double, double> anchor{0.5, 0.5};
    bool is_gif = false;
    std::shared_ptr<math::PiecewiseEased> gif_progress;
    int father = -1;
    bool rotate_with_father = true;
    std::string name;

    // RPE attachUI: binds this line to a UI element (pause/combonumber/combo/score/bar/name/level).
    // When set, the line is hidden and the UI element is controlled by this line's events.
    // Rendering of the actual UI element is handled by the HUD layer; this field is informational.
    std::string attach_ui;

    // RPE z-ordering (higher = drawn on top). Default 0 = index order.
    int z_order = 0;

    // RPE isCover: line acts as a cover layer (drawn over notes).
    bool is_cover = false;

    // RPE inclineEvents: perspective tilt angle (degrees). Positive = tilt right.
    std::shared_ptr<math::PiecewiseEased> incline;

    // RPE control events: per-x-position note property modifiers.
    // Evaluated at render time based on note.x_local_px to modulate note appearance.
    std::vector<CtrlPoint> alpha_ctrl;  // alphaControl: modulate note alpha by scroll dist
    std::vector<CtrlPoint> pos_ctrl;    // posControl:   multiply note positionX by scroll dist
    std::vector<CtrlPoint> size_ctrl;   // sizeControl:  modulate note size by scroll dist
    std::vector<CtrlPoint> y_ctrl;      // yControl:     add perpendicular offset by scroll dist
    std::vector<CtrlPoint> skew_ctrl;   // skewControl:  modulate note skew by scroll dist
};

// Evaluate a control-point curve at a given x (RPE units, -675..675).
// Returns interpolated value; if pts is empty returns def.
inline float eval_ctrl(const std::vector<CtrlPoint>& pts, float x, float def = 1.0f) {
    if (pts.empty()) return def;
    if (x <= pts.front().x) return pts.front().value;
    if (x >= pts.back().x)  return pts.back().value;
    for (size_t i = 1; i < pts.size(); ++i) {
        if (x <= pts[i].x) {
            float t = (x - pts[i-1].x) / (pts[i].x - pts[i-1].x);
            // easing=1 → linear; others could be extended later
            return pts[i-1].value + t * (pts[i].value - pts[i-1].value);
        }
    }
    return pts.back().value;
}

struct NoteState {
    const Note* note = nullptr;
    bool judged = false;
    bool hit = false;
    bool holding = false;
    bool released_early = false;
    bool miss = false;
    int next_hold_fx_ms = 0;
    std::string hold_grade;
    bool hold_finalized = false;
    bool hold_failed = false;
    double release_t = 0.0; // time of early release (for hold scoring)
    double judge_t = 0.0;
    double judge_delta_ms = 0.0;
    std::string judge_grade;
};

// Result of loading a chart
struct ChartData {
    double offset = 0.0; // seconds
    std::vector<Line> lines;
    std::vector<Note> notes; // sorted by t_hit

    // Optional paths from RPE META (relative to chart root).
    // Populated by load_rpe(); empty if not present or not an RPE chart.
    std::string meta_song_path;
    std::string meta_bg_path;

    // Precomputed at load time — avoids O(N) loops every build_frame() call
    double chart_end_t    = 0.0; // max(note.t_end) over all notes
    int    playable_count = 0;   // count of non-fake notes

    // True when loaded from .phbc — t_enter already baked; skip precompute_t_enter()
    bool is_compiled = false;

    // "Acting notes" index: notes whose t_enter is much earlier than t_hit.
    // These are typically notes on lines with speed=0 segments ("release" pattern).
    // Sorted by t_enter. Each entry is an index into notes[].
    // Built by build_early_notes_index() after precompute_t_enter().
    std::vector<size_t> early_notes;  // indices into notes[], sorted by t_enter

    // Full note index sorted by t_enter (all notes, not just acting notes).
    // Used by render-stage candidate selection to avoid t_hit-based filtering.
    std::vector<size_t> notes_by_enter; // indices into notes[], sorted by t_enter

    void build_early_notes_index() {
        early_notes.clear();
        static constexpr double THRESHOLD = 15.0; // seconds gap to qualify
        for (size_t i = 0; i < notes.size(); ++i) {
            const auto& n = notes[i];
            if (n.fake) continue;
            if (n.t_hit - n.t_enter > THRESHOLD)
                early_notes.push_back(i);
        }
        std::stable_sort(early_notes.begin(), early_notes.end(),
            [this](size_t a, size_t b) {
                return notes[a].t_enter < notes[b].t_enter;
            });
    }

    void build_notes_by_enter_index() {
        notes_by_enter.clear();
        notes_by_enter.reserve(notes.size());
        for (size_t i = 0; i < notes.size(); ++i) {
            // Include fake notes: they have t_enter = -1e9 and sort to the front,
            // so they are always within the entered window and never trigger early break.
            notes_by_enter.push_back(i);
        }
        std::stable_sort(notes_by_enter.begin(), notes_by_enter.end(),
            [this](size_t a, size_t b) {
                if (notes[a].t_enter != notes[b].t_enter)
                    return notes[a].t_enter < notes[b].t_enter;
                return notes[a].t_hit < notes[b].t_hit;
            });
    }

    // Compute chart_end_t and playable_count from notes. Call once after parsing.
    void finalize() {
        chart_end_t = 0.0;
        playable_count = 0;
        for (auto& n : notes) {
            n.mh = false;
            if (n.t_end > chart_end_t) chart_end_t = n.t_end;
            if (!n.fake) ++playable_count;
        }

        static constexpr double MH_EPS = 1e-4;
        size_t i = 0;
        while (i < notes.size()) {
            if (notes[i].fake) { ++i; continue; }
            size_t j = i + 1;
            while (j < notes.size()) {
                if (notes[j].fake) { ++j; continue; }
                if (std::abs(notes[j].t_hit - notes[i].t_hit) > MH_EPS) break;
                ++j;
            }
            int grouped = 0;
            for (size_t k = i; k < j; ++k) {
                if (!notes[k].fake) ++grouped;
            }
            if (grouped >= 2) {
                for (size_t k = i; k < j; ++k) {
                    if (!notes[k].fake) notes[k].mh = true;
                }
            }
            i = j;
        }
    }
};

} // namespace phigros
