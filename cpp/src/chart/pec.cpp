#include "phigros/chart/pec.hpp"
#include "phigros/chart/bpm_map.hpp"
#include "phigros/math/easing.hpp"
#include "phigros/math/tracks.hpp"
#include "phigros/math/util.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace phigros::math;

namespace phigros::chart {

static double pec_x_to_px(double x, int W) {
    double fx = x;
    if (fx >= 1024.0 || fx <= -1024.0) fx -= 1024.0;
    double sc = static_cast<double>(W) / 2048.0;
    return (fx + 1024.0) * sc;
}

static double pec_y_to_px(double y, int H) {
    double fy = y;
    if (fy >= 700.0 || fy <= -700.0) fy -= 700.0;
    double sc = static_cast<double>(H) / 1400.0;
    return static_cast<double>(H) * 0.5 - fy * sc;
}

// Split a string by whitespace
static std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> parts;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) parts.push_back(tok);
    return parts;
}

static double to_double(const std::string& s) {
    try { return std::stod(s); } catch (...) { return 0.0; }
}

static int to_int(const std::string& s) {
    try { return std::stoi(s); } catch (...) { return 0; }
}

struct PecCmd {
    std::string head;
    std::vector<std::string> parts; // everything after head
};

// Build tracks for one line from PEC event commands
struct LineTracksResult {
    PiecewiseEased px, py, pr, pa;
    IntegralTrack scroll;
};

static LineTracksResult build_tracks_for_line(
    int lid, const std::vector<PecCmd>& ev_cmds,
    const std::vector<PecCmd>& notes_cmds,
    const BpmMap& bpm_map, int W, int H)
{
    double px_per_unit_per_sec = 120.0 * (static_cast<double>(H) / 900.0);

    double cur_x = 0.0, cur_y = 0.0, cur_rot = 0.0, cur_alpha = 255.0, cur_speed = 1.0;
    std::vector<EasedSeg> x_segs, y_segs, r_segs, a_segs;
    std::vector<std::pair<double, double>> speed_keys;
    double t_cur = 0.0;

    auto emit_const = [&](double t0, double t1) {
        if (t1 <= t0 + 1e-9) return;
        x_segs.push_back({t0, t1, cur_x, cur_x, 0});
        y_segs.push_back({t0, t1, cur_y, cur_y, 0});
        r_segs.push_back({t0, t1, cur_rot, cur_rot, 0});
        a_segs.push_back({t0, t1, cur_alpha, cur_alpha, 0});
    };

    // Collect events for this line, sorted by time
    struct TimedCmd { double t; std::string head; std::vector<std::string> parts; };
    std::vector<TimedCmd> events;

    for (auto& cmd : ev_cmds) {
        if (cmd.parts.empty()) continue;
        if (to_int(cmd.parts[0]) != lid) continue;

        double bt = 0.0;
        bool has_bt = false;
        if ((cmd.head == "cv" || cmd.head == "cp" || cmd.head == "cd" || cmd.head == "ca") && cmd.parts.size() >= 2) {
            bt = to_double(cmd.parts[1]); has_bt = true;
        } else if ((cmd.head == "cm" || cmd.head == "cr" || cmd.head == "cf") && cmd.parts.size() >= 2) {
            bt = to_double(cmd.parts[1]); has_bt = true;
        }
        if (!has_bt) continue;
        events.push_back({bpm_map.beat_to_sec(bt), cmd.head, cmd.parts});
    }
    std::sort(events.begin(), events.end(), [](auto& a, auto& b) { return a.t < b.t; });

    for (auto& ev : events) {
        double t0 = ev.t;
        auto& h = ev.head;
        auto& p = ev.parts;

        if (t0 > t_cur) { emit_const(t_cur, t0); t_cur = t0; }

        if (h == "cp" && p.size() >= 4) {
            cur_x = to_double(p[2]);
            cur_y = to_double(p[3]);
            continue;
        }
        if (h == "cd" && p.size() >= 3) {
            cur_rot = to_double(p[2]);
            continue;
        }
        if (h == "ca" && p.size() >= 3) {
            double v = to_double(p[2]);
            if (v < 0) v = 0.0;
            cur_alpha = clamp(v, 0.0, 255.0);
            continue;
        }
        if (h == "cv" && p.size() >= 3) {
            cur_speed = to_double(p[2]);
            speed_keys.emplace_back(t0, cur_speed);
            continue;
        }
        if (h == "cm" && p.size() >= 6) {
            double t1 = bpm_map.beat_to_sec(to_double(p[2]));
            double x1 = to_double(p[3]);
            double y1 = to_double(p[4]);
            int et = to_int(p[5]);
            if (t1 > t0 + 1e-9) {
                auto ef = et;
                x_segs.push_back({t0, t1, cur_x, x1, ef});
                y_segs.push_back({t0, t1, cur_y, y1, ef});
                r_segs.push_back({t0, t1, cur_rot, cur_rot, 0});
                a_segs.push_back({t0, t1, cur_alpha, cur_alpha, 0});
                cur_x = x1; cur_y = y1;
                t_cur = t1;
            }
            continue;
        }
        if (h == "cr" && p.size() >= 5) {
            double t1 = bpm_map.beat_to_sec(to_double(p[2]));
            double r1 = to_double(p[3]);
            int et = to_int(p[4]);
            if (t1 > t0 + 1e-9) {
                r_segs.push_back({t0, t1, cur_rot, r1, et});
                x_segs.push_back({t0, t1, cur_x, cur_x, 0});
                y_segs.push_back({t0, t1, cur_y, cur_y, 0});
                a_segs.push_back({t0, t1, cur_alpha, cur_alpha, 0});
                cur_rot = r1;
                t_cur = t1;
            }
            continue;
        }
        if (h == "cf" && p.size() >= 4) {
            double t1 = bpm_map.beat_to_sec(to_double(p[2]));
            double a1 = to_double(p[3]);
            int et = (p.size() >= 5) ? to_int(p[4]) : 0;
            if (a1 < 0) a1 = 0.0;
            a1 = clamp(a1, 0.0, 255.0);
            if (t1 > t0 + 1e-9) {
                a_segs.push_back({t0, t1, cur_alpha, a1, et});
                x_segs.push_back({t0, t1, cur_x, cur_x, 0});
                y_segs.push_back({t0, t1, cur_y, cur_y, 0});
                r_segs.push_back({t0, t1, cur_rot, cur_rot, 0});
                cur_alpha = a1;
                t_cur = t1;
            }
            continue;
        }
    }

    // Determine end time from notes on this line
    double end_hint = 0.0;
    for (auto& cmd : notes_cmds) {
        if (!cmd.head.empty() && cmd.head[0] == 'n' && !cmd.parts.empty()) {
            if (to_int(cmd.parts[0]) != lid) continue;
            if (cmd.head == "n2" && cmd.parts.size() >= 3)
                end_hint = std::max(end_hint, bpm_map.beat_to_sec(to_double(cmd.parts[2])));
            else if (cmd.parts.size() >= 2)
                end_hint = std::max(end_hint, bpm_map.beat_to_sec(to_double(cmd.parts[1])));
        }
    }
    double end_time = std::max(end_hint + 5.0, t_cur + 2.0);
    emit_const(t_cur, end_time);

    // Build speed → scroll IntegralTrack
    if (speed_keys.empty()) speed_keys.emplace_back(0.0, cur_speed);
    std::sort(speed_keys.begin(), speed_keys.end());

    std::set<double> cuts_set;
    cuts_set.insert(0.0);
    for (auto& [t, v] : speed_keys) cuts_set.insert(t);
    cuts_set.insert(end_time);
    std::vector<double> cuts(cuts_set.begin(), cuts_set.end());
    std::sort(cuts.begin(), cuts.end());

    std::vector<Seg1D> scroll_segs;
    double prefix = 0.0;
    for (size_t i = 0; i + 1 < cuts.size(); ++i) {
        double st = cuts[i], et = cuts[i + 1];
        if (et <= st) continue;
        // Find active speed: last key <= st
        double v = speed_keys[0].second;
        for (auto& [kt, kv] : speed_keys) {
            if (kt <= st + 1e-9) v = kv; else break;
        }
        double vpx = v * px_per_unit_per_sec;
        scroll_segs.push_back({st, et, vpx, vpx, prefix});
        prefix += vpx * (et - st);
    }

    return {
        PiecewiseEased(std::move(x_segs), 0.0),
        PiecewiseEased(std::move(y_segs), 0.0),
        PiecewiseEased(std::move(r_segs), 0.0),
        PiecewiseEased(std::move(a_segs), 255.0),
        IntegralTrack(std::move(scroll_segs))
    };
}

ChartData load_pec_text(const std::string& text, int W, int H) {
    ChartData result;

    // Parse lines (strip comments)
    std::vector<std::string> raw_lines;
    std::istringstream stream(text);
    std::string ln;
    while (std::getline(stream, ln)) {
        // Trim
        size_t start = ln.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        ln = ln.substr(start);
        size_t end = ln.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) ln = ln.substr(0, end + 1);
        if (ln.empty() || ln.substr(0, 2) == "//") continue;
        raw_lines.push_back(ln);
    }
    if (raw_lines.empty()) return result;

    // First line: offset in ms
    result.offset = to_double(raw_lines[0]) / 1000.0;

    // Parse BPM items
    std::vector<std::pair<double, double>> bpm_items;
    for (size_t i = 1; i < raw_lines.size(); ++i) {
        auto parts = split_ws(raw_lines[i]);
        if (parts.size() >= 3 && parts[0] == "bp")
            bpm_items.emplace_back(to_double(parts[1]), to_double(parts[2]));
    }
    if (bpm_items.empty()) bpm_items.emplace_back(0.0, 120.0);
    BpmMap bpm_map = BpmMap::build(std::move(bpm_items));

    // Separate note commands and event commands
    std::vector<PecCmd> notes_cmds, ev_cmds;
    int max_line = -1;

    for (size_t i = 1; i < raw_lines.size(); ++i) {
        auto parts = split_ws(raw_lines[i]);
        if (parts.empty()) continue;
        std::string head = parts[0];
        std::vector<std::string> rest(parts.begin() + 1, parts.end());

        if (head[0] == 'n' || head == "#" || head == "&") {
            notes_cmds.push_back({head, rest});
        } else if (head != "bp") {
            ev_cmds.push_back({head, rest});
        }

        // Track max line id
        if ((head == "cv" || head == "cp" || head == "cd" || head == "ca" ||
             head == "cm" || head == "cr" || head == "cf") && !rest.empty()) {
            max_line = std::max(max_line, to_int(rest[0]));
        }
        if (head[0] == 'n' && !rest.empty())
            max_line = std::max(max_line, to_int(rest[0]));
    }

    int line_count = std::max(0, max_line + 1);
    if (line_count > 30) line_count = 30;

    // Build tracks for each line
    for (int lid = 0; lid < line_count; ++lid) {
        auto [px, py, pr, pa, scroll] = build_tracks_for_line(lid, ev_cmds, notes_cmds, bpm_map, W, H);

        auto px_ptr = std::make_shared<PiecewiseEased>(std::move(px));
        auto py_ptr = std::make_shared<PiecewiseEased>(std::move(py));
        auto pr_ptr = std::make_shared<PiecewiseEased>(std::move(pr));
        auto pa_ptr = std::make_shared<PiecewiseEased>(std::move(pa));

        Line line;
        line.lid = lid;
        line.pos_x = [px_ptr, W](double t) { return pec_x_to_px(px_ptr->eval(t), W); };
        line.pos_y = [py_ptr, H](double t) { return pec_y_to_px(py_ptr->eval(t), H); };
        line.rot = [pr_ptr](double t) { return pr_ptr->eval(t) * M_PI / 180.0; };
        line.alpha = [pa_ptr](double t) -> double {
            double v = pa_ptr->eval(t);
            if (v <= 1.000001) return clamp(v, 0.0, 1.0);
            return clamp(v / 255.0, 0.0, 1.0);
        };
        line.scroll_px = std::move(scroll);
        line.color_rgb = {255, 255, 255};
        result.lines.push_back(std::move(line));
    }

    // Parse notes
    int nid = 0;
    struct PendingNote {
        int line_id; int kind; double t_hit; double t_end;
        double x_local_px; bool above; bool fake; double speed_mul; double size_px;
    };
    std::optional<PendingNote> pending;

    for (auto& cmd : notes_cmds) {
        auto& head = cmd.head;
        auto& parts = cmd.parts;

        if (!head.empty() && head[0] == 'n') {
            pending.reset();
            if (parts.empty()) continue;
            int tp = to_int(head.substr(1));
            if (tp < 1 || tp > 4) continue;
            int lid = to_int(parts[0]);
            if (lid < 0 || lid >= line_count) continue;

            double b0, b1, x;
            int direction;
            bool fake;
            if (tp == 2) {
                if (parts.size() < 6) continue;
                b0 = to_double(parts[1]);
                b1 = to_double(parts[2]);
                x = to_double(parts[3]);
                direction = to_int(parts[4]);
                fake = (to_int(parts[5]) == 1);
            } else {
                if (parts.size() < 5) continue;
                b0 = to_double(parts[1]);
                b1 = b0;
                x = to_double(parts[2]);
                direction = to_int(parts[3]);
                fake = (to_int(parts[4]) == 1);
            }

            double t_hit = bpm_map.beat_to_sec(b0);
            double t_end = bpm_map.beat_to_sec(b1);

            pending = PendingNote{
                lid,
                (tp == 2) ? 3 : tp, // n2 (hold) → kind 3
                t_hit,
                (tp == 2) ? t_end : t_hit,
                x * (static_cast<double>(W) / 2048.0),
                (direction == 1),
                fake,
                1.0,
                1.0
            };
            continue;
        }

        if (head == "#" && pending.has_value()) {
            if (!parts.empty())
                pending->speed_mul = to_double(parts[0]);
            continue;
        }

        if (head == "&" && pending.has_value()) {
            if (!parts.empty())
                pending->size_px = to_double(parts[0]);

            Note note;
            note.nid = nid++;
            note.line_id = pending->line_id;
            note.kind = pending->kind;
            note.above = pending->above;
            note.fake = pending->fake;
            note.t_hit = pending->t_hit;
            note.t_end = pending->t_end;
            note.x_local_px = pending->x_local_px;
            note.y_offset_px = 0.0;
            note.speed_mul = pending->speed_mul;
            note.size_px = pending->size_px;
            note.alpha01 = 1.0;
            result.notes.push_back(std::move(note));
            pending.reset();
        }
    }

    // Cache scroll samples (not done in Python PEC parser, but needed for correctness)
    for (auto& n : result.notes) {
        if (n.line_id >= 0 && n.line_id < static_cast<int>(result.lines.size())) {
            auto& ln = result.lines[n.line_id];
            n.scroll_hit = ln.scroll_px.integral(n.t_hit);
            n.scroll_end = ln.scroll_px.integral(n.t_end);
        }
    }

    std::sort(result.notes.begin(), result.notes.end(),
              [](const Note& a, const Note& b) { return a.t_hit < b.t_hit; });

    return result;
}

ChartData load_pec(const std::string& path, int W, int H) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open PEC file: " + path);
    std::string text((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    return load_pec_text(text, W, H);
}

} // namespace phigros::chart
