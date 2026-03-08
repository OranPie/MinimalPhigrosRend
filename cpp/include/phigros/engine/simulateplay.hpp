#pragma once
#include "phigros/core/types.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/kinematics.hpp"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace phigros::engine {

enum class SimMode { Conservative, Aggressive, Extreme };

struct SimPointerTrailSample {
    double x = 0.0;
    double y = 0.0;
    double t = 0.0;
};

struct SimPointerVisual {
    int id = -1;
    bool down = false;
    bool flick = false;
    double x = 0.0;
    double y = 0.0;
    double fade_alpha = 0.0;
    double fade_progress = 0.0;
    std::vector<SimPointerTrailSample> trail;
};

struct SimHitEvent {
    int note_idx = -1;
    double judge_t = 0.0;
    double delta_ms = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string grade;
};

class SimulatePlayer {
public:
    explicit SimulatePlayer(SimMode mode = SimMode::Conservative,
                            int max_pointers = 2)
        : mode_(mode), max_pointers_(std::max(1, max_pointers))
    {
        timing_window_ = Judge::BAD;
        init_pointers();
    }

    void set_humanize(bool enabled, double jitter_ms) {
        humanize_ = enabled;
        jitter_ms_ = std::max(0.0, jitter_ms);
    }

    void set_visuals(bool render_pointer, bool render_trail,
                     double trail_seconds, double cursor_radius_px) {
        render_pointer_ = render_pointer;
        render_trail_ = render_trail;
        trail_seconds_ = std::max(0.02, trail_seconds);
        cursor_radius_px_ = std::max(4.0, cursor_radius_px);
    }

    bool render_enabled() const { return render_pointer_; }
    bool trail_enabled() const { return render_pointer_ && render_trail_; }
    double cursor_radius_px() const { return cursor_radius_px_; }
    const std::vector<SimPointerVisual>& visuals() const { return visuals_; }

    void reset() {
        t_prev_ = -1e9;
        dt_frame_est_ = 1.0 / 60.0;
        active_holds_.clear();
        plans_.clear();
        init_pointers();
    }

    void step(double t, const std::vector<Note>& notes,
              std::vector<NoteState>& states,
              const std::vector<Line>& lines,
              Judge& judge, int W, int H,
              std::vector<int>* hit_out = nullptr,
              std::vector<SimHitEvent>* hit_events = nullptr)
    {
        (void)W; (void)H;
        if (max_pointers_ <= 0) return;
        ensure_storage(notes.size());

        if (t_prev_ > -1e8) {
            double dt = t - t_prev_;
            dt_frame_est_ = dt_frame_est_ * 0.9 + dt * 0.1;
        }
        t_prev_ = t;

        release_finished_taps(t);
        release_completed_holds(t, notes, states);

        int idx_next = find_next_unjudged(notes, states, t);
        int lo = std::max(0, idx_next - 48);
        int hi = std::min(static_cast<int>(notes.size()), idx_next + 720);

        for (int i = lo; i < hi; ++i) {
            const auto& n = notes[i];
            auto& s = states[i];
            if (n.fake || s.judged || s.hold_finalized) continue;
            if (n.kind == 3 && (s.hit || s.holding)) continue;
            if (n.kind != 3 && s.hit) continue;
            plan_note(i, n, notes, states, lines, t);
        }

        for (int i = lo; i < hi; ++i) {
            const auto& n = notes[i];
            auto& s = states[i];
            if (n.fake || s.judged || s.hold_finalized) continue;
            auto& plan = plans_[i];
            if (!plan.active || plan.fired || plan.pointer_idx < 0) continue;
            if (t + 1e-9 < plan.judge_t) continue;

            auto& pointer = pointers_[plan.pointer_idx];
            pointer.x = plan.x;
            pointer.y = plan.y;
            pointer.target_x = plan.x;
            pointer.target_y = plan.y;
            pointer.down = true;
            pointer.down_until = plan.judge_t + release_linger_s(n.kind);
            pointer.flick = (n.kind == 4);
            pointer.flick_until = pointer.flick ? (plan.judge_t + 0.05) : -1e9;
            pointer.fade_start_t = -1e9;
            pointer.fade_until = -1e9;

            std::optional<std::string> grade;
            if (n.kind == 3) {
                grade = judge.start_hold(s, plan.judge_t);
                if (grade) {
                    active_holds_.insert(i);
                    pointer.holding_note = i;
                    pointer.down_until = std::max(pointer.down_until, n.t_end);
                }
            } else {
                grade = judge.try_hit(s, plan.judge_t);
            }

            plan.fired = true;
            plan.active = false;
            pointer.assigned_note = -1;

            if (grade) {
                if (hit_out) hit_out->push_back(i);
                if (hit_events) {
                    hit_events->push_back({
                        i,
                        plan.judge_t,
                        (plan.judge_t - n.t_hit) * 1000.0,
                        plan.x,
                        plan.y,
                        *grade
                    });
                }
            }
        }

        update_pointer_motion(t);
        record_visual_trails(t);
    }

private:
    struct PointerState {
        int id = -1;
        double x = 0.0;
        double y = 0.0;
        double target_x = 0.0;
        double target_y = 0.0;
        bool down = false;
        bool flick = false;
        double down_until = -1e9;
        double flick_until = -1e9;
        double fade_start_t = -1e9;
        double fade_until = -1e9;
        int assigned_note = -1;
        int holding_note = -1;
        std::vector<SimPointerTrailSample> trail;
    };

    struct NotePlan {
        bool active = false;
        bool fired = false;
        int pointer_idx = -1;
        double judge_t = 0.0;
        double x = 0.0;
        double y = 0.0;
    };

    SimMode mode_;
    int max_pointers_;
    double timing_window_;
    double dt_frame_est_ = 1.0 / 60.0;
    double t_prev_ = -1e9;
    bool humanize_ = false;
    bool render_pointer_ = false;
    bool render_trail_ = false;
    double jitter_ms_ = 0.0;
    double trail_seconds_ = 0.16;
    double cursor_radius_px_ = 20.0;
    double release_fade_seconds_ = 0.10;
    std::unordered_set<int> active_holds_;
    std::vector<PointerState> pointers_;
    std::vector<SimPointerVisual> visuals_;
    std::vector<NotePlan> plans_;

    void ensure_storage(size_t note_count) {
        if (plans_.size() != note_count) plans_.assign(note_count, NotePlan{});
        if (static_cast<int>(pointers_.size()) != max_pointers_) init_pointers();
    }

    void init_pointers() {
        pointers_.assign(max_pointers_, PointerState{});
        visuals_.assign(max_pointers_, SimPointerVisual{});
        for (int i = 0; i < max_pointers_; ++i) {
            pointers_[i].id = i;
            visuals_[i].id = i;
        }
    }

    static uint32_t hash_u32(uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    int find_next_unjudged(const std::vector<Note>& notes,
                           const std::vector<NoteState>& states,
                           double t) const {
        for (size_t i = 0; i < notes.size(); ++i) {
            if (!states[i].judged && !states[i].holding &&
                notes[i].t_hit > t - Judge::BAD)
                return static_cast<int>(i);
        }
        return static_cast<int>(notes.size());
    }

    void begin_release_fade(PointerState& p, double t) {
        p.fade_start_t = t;
        p.fade_until = t + release_fade_seconds_;
    }

    void release_completed_holds(double t, const std::vector<Note>& notes,
                                 std::vector<NoteState>& states) {
        std::vector<int> to_remove;
        for (int idx : active_holds_) {
            auto& n = notes[idx];
            auto& s = states[idx];
            if (t >= n.t_end) {
                s.holding = false;
                to_remove.push_back(idx);
                for (auto& p : pointers_) {
                    if (p.holding_note == idx) {
                        p.holding_note = -1;
                        p.down_until = t;
                        p.down = false;
                        begin_release_fade(p, t);
                    }
                }
            }
        }
        for (int idx : to_remove) active_holds_.erase(idx);
    }

    void release_finished_taps(double t) {
        for (auto& p : pointers_) {
            if (p.holding_note < 0 && p.down && t >= p.down_until) {
                p.down = false;
                begin_release_fade(p, t);
            }
            if (t >= p.flick_until) p.flick = false;
        }
    }

    double release_linger_s(int kind) const {
        switch (kind) {
            case 2: return 0.055;
            case 4: return 0.045;
            default: return 0.032;
        }
    }

    double lookahead_s(int kind) const {
        double base = humanize_ ? 0.12 : 0.05;
        if (kind == 4) base += 0.035;
        if (kind == 3) base += 0.02;
        return std::max(base, dt_frame_est_ * 4.0);
    }

    double delta_for_note(int idx, const Note& n,
                          const std::vector<Note>& notes) const {
        if (!humanize_) return 0.0;

        double gap_prev = 0.25;
        for (int j = idx - 1; j >= 0; --j) {
            if (!notes[j].fake) {
                gap_prev = std::max(0.0, n.t_hit - notes[j].t_hit);
                break;
            }
        }

        double density = 0.0;
        if (gap_prev < 0.055) density = 1.0;
        else if (gap_prev < 0.09) density = 0.65;
        else if (gap_prev < 0.14) density = 0.35;

        uint32_t h = hash_u32(static_cast<uint32_t>(n.nid) * 2654435761u
                            ^ static_cast<uint32_t>((idx + 1) * 977u)
                            ^ static_cast<uint32_t>(n.kind * 131u));
        double rand01 = static_cast<double>(h & 0xffffu) / 65535.0;
        double rand_centered = rand01 * 2.0 - 1.0;

        double amp_ms = jitter_ms_ * (0.45 + density * 0.9);
        if (mode_ == SimMode::Conservative) amp_ms *= 0.6;
        else if (mode_ == SimMode::Extreme) amp_ms *= 1.6;
        if (n.kind == 4) amp_ms += 3.0;
        if (n.kind == 2) amp_ms += 1.5;

        double bias_ms = 0.0;
        if (n.kind == 4) bias_ms -= 4.0;
        if (density > 0.8) bias_ms += ((h >> 17) & 1u) ? 5.0 : -5.0;

        double ms = bias_ms + rand_centered * amp_ms;
        double limit = Judge::BAD * 1000.0 * 0.9;
        return std::clamp(ms, -limit, limit) / 1000.0;
    }

    Vec2 note_hit_pos(const Note& n, const std::vector<Line>& lines, double t_eval) const {
        if (n.line_id < 0 || n.line_id >= static_cast<int>(lines.size())) return {0.0, 0.0};
        auto ls = eval_line_state(lines[n.line_id], t_eval);
        return note_world_pos_cs(ls.x, ls.y, ls.cos_rot, ls.sin_rot,
                                 ls.scroll, n, n.scroll_hit,
                                 false, 1.0, false, n.kind == 3);
    }

    int choose_pointer(double t, double judge_t, const Vec2& pos) const {
        int best = -1;
        double best_score = std::numeric_limits<double>::infinity();
        for (int i = 0; i < static_cast<int>(pointers_.size()); ++i) {
            const auto& p = pointers_[i];
            if (p.holding_note >= 0) continue;
            double available_t = std::max(t, p.down_until);
            if (available_t > judge_t + Judge::BAD * 0.35) continue;
            double travel = std::hypot(p.x - pos.x, p.y - pos.y);
            double slack = std::max(0.012, judge_t - available_t);
            double req_speed = travel / slack;
            double score = req_speed + (p.down ? 220.0 : 0.0) + std::abs(available_t - judge_t) * 180.0;
            if (score < best_score) {
                best_score = score;
                best = i;
            }
        }
        return best;
    }

    void plan_note(int idx, const Note& n,
                   const std::vector<Note>& notes,
                   const std::vector<NoteState>& states,
                   const std::vector<Line>& lines,
                   double t) {
        auto& plan = plans_[idx];
        if (plan.fired || states[idx].judged) return;

        double judge_t = n.t_hit + delta_for_note(idx, n, notes);
        if (std::abs(judge_t - n.t_hit) > Judge::BAD) judge_t = n.t_hit;
        if (t + lookahead_s(n.kind) < judge_t) return;

        Vec2 pos = note_hit_pos(n, lines, judge_t);
        if (!plan.active) {
            int pointer_idx = choose_pointer(t, judge_t, pos);
            if (pointer_idx < 0) return;
            plan.active = true;
            plan.pointer_idx = pointer_idx;
            pointers_[pointer_idx].assigned_note = idx;
        }

        plan.judge_t = judge_t;
        plan.x = pos.x;
        plan.y = pos.y;
        auto& pointer = pointers_[plan.pointer_idx];
        pointer.target_x = pos.x;
        pointer.target_y = pos.y;
    }

    void update_pointer_motion(double t) {
        double dt = std::max(1.0 / 480.0, dt_frame_est_);
        double max_speed = humanize_ ? 2600.0 : 1.0e9;
        for (size_t i = 0; i < pointers_.size(); ++i) {
            auto& p = pointers_[i];
            if (p.holding_note >= 0) {
                p.x = p.target_x;
                p.y = p.target_y;
                p.down = true;
            } else {
                double dx = p.target_x - p.x;
                double dy = p.target_y - p.y;
                double dist = std::hypot(dx, dy);
                double step = max_speed * dt;
                if (dist <= step || step <= 0.0) {
                    p.x = p.target_x;
                    p.y = p.target_y;
                } else {
                    p.x += dx / dist * step;
                    p.y += dy / dist * step;
                }
            }
            double fade_alpha = p.down ? 1.0 : 0.0;
            double fade_progress = 0.0;
            if (!p.down && t < p.fade_until && p.fade_until > p.fade_start_t) {
                double u = (t - p.fade_start_t) / (p.fade_until - p.fade_start_t);
                u = std::clamp(u, 0.0, 1.0);
                fade_progress = u;
                fade_alpha = 1.0 - u * u;
            }
            visuals_[i].id = p.id;
            visuals_[i].down = p.down;
            visuals_[i].flick = p.flick && (t < p.flick_until);
            visuals_[i].x = p.x;
            visuals_[i].y = p.y;
            visuals_[i].fade_alpha = fade_alpha;
            visuals_[i].fade_progress = fade_progress;
        }
    }

    void record_visual_trails(double t) {
        for (size_t i = 0; i < pointers_.size(); ++i) {
            auto& p = pointers_[i];
            auto& out = visuals_[i].trail;
            if (render_pointer_ && (p.down || p.assigned_note >= 0 || p.holding_note >= 0)) {
                out.push_back({p.x, p.y, t});
            }
            while (!out.empty() && t - out.front().t > trail_seconds_) {
                out.erase(out.begin());
            }
        }
    }
};

} // namespace phigros::engine
