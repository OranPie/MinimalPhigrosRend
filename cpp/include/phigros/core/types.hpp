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

    // Precomputed visibility
    double t_enter = -1e9;
    bool mh = false; // multi-hit simultaneous flag
};

// Track callable: eval(t) → double
using TrackFn = std::function<double(double)>;

// Color callable: eval(t) → RGB (used by compiled charts to override per-line color)
using ColorFn = std::function<math::RGB(double)>;

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
};

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
};

// Result of loading a chart
struct ChartData {
    double offset = 0.0; // seconds
    std::vector<Line> lines;
    std::vector<Note> notes; // sorted by t_hit

    // Precomputed at load time — avoids O(N) loops every build_frame() call
    double chart_end_t    = 0.0; // max(note.t_end) over all notes
    int    playable_count = 0;   // count of non-fake notes

    // True when loaded from .phbc — t_enter already baked; skip precompute_t_enter()
    bool is_compiled = false;

    // Compute chart_end_t and playable_count from notes. Call once after parsing.
    void finalize() {
        chart_end_t = 0.0;
        playable_count = 0;
        for (const auto& n : notes) {
            if (n.t_end > chart_end_t) chart_end_t = n.t_end;
            if (!n.fake) ++playable_count;
        }
    }
};

} // namespace phigros
