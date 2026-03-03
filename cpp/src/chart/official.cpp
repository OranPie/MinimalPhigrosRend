#include "phigros/chart/official.hpp"
#include "phigros/math/easing.hpp"
#include "phigros/math/tracks.hpp"
#include "phigros/math/util.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

using json = nlohmann::json;
using namespace phigros::math;

namespace phigros::chart {

static double official_unit_sec(double bpm) {
    return 1.875 / bpm;
}

static double u_to_sec(double u, double bpm) {
    return u * official_unit_sec(bpm);
}

static IntegralTrack build_official_scroll_px(const json& speed_events, double bpm, double Uh_px) {
    if (!speed_events.is_array() || speed_events.empty())
        return IntegralTrack();

    struct Ev { double st, et, val; };
    std::vector<Ev> evs;
    for (auto& e : speed_events) {
        evs.push_back({
            e.value("startTime", 0.0),
            e.value("endTime", 0.0),
            e.value("value", 0.0)
        });
    }
    std::sort(evs.begin(), evs.end(), [](auto& a, auto& b) { return a.st < b.st; });

    std::vector<Seg1D> segs;
    double prefix = 0.0;

    for (auto& ev : evs) {
        double t0 = u_to_sec(ev.st, bpm);
        double t1 = u_to_sec(ev.et, bpm);
        double v = ev.val * Uh_px;
        segs.push_back({t0, t1, v, v, prefix});
        double dt = std::max(0.0, t1 - t0);
        prefix += 0.5 * (v + v) * dt;
    }

    // Extend to t=0 if needed
    if (!segs.empty() && segs[0].t0 > 0) {
        double v0 = segs[0].v0;
        segs.insert(segs.begin(), {0.0, segs[0].t0, v0, v0, 0.0});
        // Rebuild prefix sums
        prefix = 0.0;
        for (auto& s : segs) {
            s.prefix = prefix;
            double dt = std::max(0.0, s.t1 - s.t0);
            prefix += 0.5 * (s.v0 + s.v1) * dt;
        }
    }

    return IntegralTrack(std::move(segs));
}

// Returns (pos_x_track, pos_y_track) as PiecewiseEased
static std::pair<PiecewiseEased, PiecewiseEased>
build_official_pos_tracks(const json& move_events, double bpm, int fmt, int W, int H) {
    if (!move_events.is_array() || move_events.empty())
        return {PiecewiseEased(W * 0.5), PiecewiseEased(H * 0.5)};

    if (fmt != 1 && fmt != 3)
        throw std::runtime_error("Unsupported official formatVersion (expected 1 or 3)");

    std::vector<EasedSeg> sx, sy;
    struct Ev { double st; json data; };
    std::vector<Ev> evs;
    for (auto& e : move_events) evs.push_back({e.value("startTime", 0.0), e});
    std::sort(evs.begin(), evs.end(), [](auto& a, auto& b) { return a.st < b.st; });

    for (auto& ev : evs) {
        const auto& e = ev.data;
        double t0 = u_to_sec(e.value("startTime", 0.0), bpm);
        double t1 = u_to_sec(e.value("endTime", 0.0), bpm);
        double x0, y0, x1, y1;

        if (fmt == 1) {
            double p0 = e.value("start", 0.0);
            double p1 = e.value("end", 0.0);
            double x0_u = std::floor(p0 / 1000.0);
            double y0_u = p0 - x0_u * 1000.0;
            double x1_u = std::floor(p1 / 1000.0);
            double y1_u = p1 - x1_u * 1000.0;
            x0 = (x0_u / 880.0) * W;
            x1 = (x1_u / 880.0) * W;
            y0 = H * (1.0 - y0_u / 520.0);
            y1 = H * (1.0 - y1_u / 520.0);
        } else {
            x0 = e.value("start", 0.0) * W;
            x1 = e.value("end", 0.0) * W;
            y0 = H * (1.0 - e.value("start2", 0.0));
            y1 = H * (1.0 - e.value("end2", 0.0));
        }
        sx.push_back({t0, t1, x0, x1, 0}); // easing_type 0 = linear
        sy.push_back({t0, t1, y0, y1, 0});
    }

    // Extend to t=0
    if (!sx.empty() && sx[0].t0 > 0) {
        sx.insert(sx.begin(), {0.0, sx[0].t0, sx[0].v0, sx[0].v0, 0});
        sy.insert(sy.begin(), {0.0, sy[0].t0, sy[0].v0, sy[0].v0, 0});
    }
    return {PiecewiseEased(std::move(sx), W * 0.5),
            PiecewiseEased(std::move(sy), H * 0.5)};
}

static PiecewiseEased build_official_rot_track(const json& rot_events, double bpm) {
    if (!rot_events.is_array() || rot_events.empty())
        return PiecewiseEased(0.0);

    struct Ev { double st; json data; };
    std::vector<Ev> evs;
    for (auto& e : rot_events) evs.push_back({e.value("startTime", 0.0), e});
    std::sort(evs.begin(), evs.end(), [](auto& a, auto& b) { return a.st < b.st; });

    std::vector<EasedSeg> segs;
    for (auto& ev : evs) {
        const auto& e = ev.data;
        double t0 = u_to_sec(e.value("startTime", 0.0), bpm);
        double t1 = u_to_sec(e.value("endTime", 0.0), bpm);
        double a0 = -e.value("start", 0.0) * M_PI / 180.0;
        double a1 = -e.value("end", 0.0) * M_PI / 180.0;
        segs.push_back({t0, t1, a0, a1, 0});
    }
    if (!segs.empty() && segs[0].t0 > 0)
        segs.insert(segs.begin(), {0.0, segs[0].t0, segs[0].v0, segs[0].v0, 0});
    return PiecewiseEased(std::move(segs), 0.0);
}

static PiecewiseEased build_official_alpha_track(const json& disp_events, double bpm) {
    if (!disp_events.is_array() || disp_events.empty())
        return PiecewiseEased(1.0);

    struct Ev { double st; json data; };
    std::vector<Ev> evs;
    for (auto& e : disp_events) evs.push_back({e.value("startTime", 0.0), e});
    std::sort(evs.begin(), evs.end(), [](auto& a, auto& b) { return a.st < b.st; });

    std::vector<EasedSeg> segs;
    for (auto& ev : evs) {
        const auto& e = ev.data;
        double t0 = u_to_sec(e.value("startTime", 0.0), bpm);
        double t1 = u_to_sec(e.value("endTime", 0.0), bpm);
        double a0 = e.value("start", 0.0);
        double a1 = e.value("end", 0.0);
        segs.push_back({t0, t1, a0, a1, 0});
    }
    if (!segs.empty() && segs[0].t0 > 0)
        segs.insert(segs.begin(), {0.0, segs[0].t0, segs[0].v0, segs[0].v0, 0});
    return PiecewiseEased(std::move(segs), 1.0);
}

ChartData load_official(const json& data, int W, int H) {
    int fmt = data.value("formatVersion", 3);
    double offset = data.value("offset", 0.0);

    double Uw = 0.05625 * W;
    double Uh = 0.6 * H;

    ChartData result;
    result.offset = offset;

    const auto& jls = data["judgeLineList"];
    int line_count = static_cast<int>(jls.size());

    for (int i = 0; i < line_count; ++i) {
        const auto& jl = jls[i];
        double bpm = jl.value("bpm", 120.0);

        auto [px_track, py_track] = build_official_pos_tracks(
            jl.value("judgeLineMoveEvents", json::array()), bpm, fmt, W, H);
        auto rot_track = build_official_rot_track(
            jl.value("judgeLineRotateEvents", json::array()), bpm);
        auto alpha_track = build_official_alpha_track(
            jl.value("judgeLineDisappearEvents", json::array()), bpm);
        auto scroll_track = build_official_scroll_px(
            jl.value("speedEvents", json::array()), bpm, Uh);

        RGB rgb = hsv_to_rgb(static_cast<double>(i) / std::max(1, line_count), 0.65, 0.95);

        Line line;
        line.lid = i;
        auto px_ptr = std::make_shared<PiecewiseEased>(std::move(px_track));
        auto py_ptr = std::make_shared<PiecewiseEased>(std::move(py_track));
        auto rot_ptr = std::make_shared<PiecewiseEased>(std::move(rot_track));
        auto alpha_ptr = std::make_shared<PiecewiseEased>(std::move(alpha_track));
        line.pos_x = [px_ptr](double t) { return px_ptr->eval(t); };
        line.pos_y = [py_ptr](double t) { return py_ptr->eval(t); };
        line.rot = [rot_ptr](double t) { return rot_ptr->eval(t); };
        line.alpha = [alpha_ptr](double t) { return alpha_ptr->eval(t); };
        line.scroll_px = std::move(scroll_track);
        line.color_rgb = rgb;
        line.name = jl.value("name", "");
        result.lines.push_back(std::move(line));

        // Notes
        int nid_base = i * 100000;
        int nid = nid_base;

        auto add_note = [&](const json& n, bool above_flag) {
            int kind = n.value("type", 1);
            double t_hit = u_to_sec(n.value("time", 0.0), bpm);
            double hold_u = n.value("holdTime", 0.0);
            double t_end = (kind == 3 && hold_u > 0)
                ? t_hit + u_to_sec(hold_u, bpm) : t_hit;

            Note note;
            note.nid = nid++;
            note.line_id = i;
            note.kind = kind;
            note.above = above_flag;
            note.fake = false;
            note.t_hit = t_hit;
            note.t_end = t_end;
            note.x_local_px = n.value("positionX", 0.0) * Uw;
            note.y_offset_px = 0.0;
            note.speed_mul = n.value("speed", 1.0);
            note.size_px = 1.0;
            note.alpha01 = 1.0;
            result.notes.push_back(std::move(note));
        };

        // Y-axis flipped: notesAbove → above=false, notesBelow → above=true
        if (jl.contains("notesAbove"))
            for (auto& n : jl["notesAbove"]) add_note(n, false);
        if (jl.contains("notesBelow"))
            for (auto& n : jl["notesBelow"]) add_note(n, true);
    }

    // Cache scroll samples
    std::unordered_map<int, Line*> line_map;
    for (auto& ln : result.lines) line_map[ln.lid] = &ln;

    for (auto& n : result.notes) {
        auto* ln = line_map[n.line_id];
        n.scroll_hit = ln->scroll_px.integral(n.t_hit);
        if (n.kind == 3 && n.t_end > n.t_hit) {
            // Official: bake speed_mul into scroll_end
            double dur = std::max(0.0, n.t_end - n.t_hit);
            double sp = std::max(0.0, n.speed_mul);
            n.scroll_end = n.scroll_hit + sp * dur * Uh;
            n.speed_mul = 1.0;
        } else {
            n.scroll_end = ln->scroll_px.integral(n.t_end);
        }
    }

    // Sort by t_hit
    std::sort(result.notes.begin(), result.notes.end(),
              [](const Note& a, const Note& b) { return a.t_hit < b.t_hit; });

    return result;
}

} // namespace phigros::chart
