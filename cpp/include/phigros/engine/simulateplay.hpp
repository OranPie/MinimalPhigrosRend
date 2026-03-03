#pragma once
#include "phigros/core/types.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/kinematics.hpp"
#include <vector>
#include <unordered_set>
#include <cmath>
#include <algorithm>

namespace phigros::engine {

enum class SimMode { Conservative, Aggressive, Extreme };

class SimulatePlayer {
public:
    explicit SimulatePlayer(SimMode mode = SimMode::Conservative,
                            int max_pointers = 2)
        : mode_(mode), max_pointers_(max_pointers)
    {
        timing_window_ = (mode == SimMode::Conservative)
            ? Judge::PERFECT : Judge::BAD;
    }

    // Main per-frame step: hit/start notes, maintain holds
    void step(double t, const std::vector<Note>& notes,
              std::vector<NoteState>& states,
              const std::vector<Line>& lines,
              Judge& judge, int W, int H)
    {
        if (max_pointers_ <= 0) return;

        // Frame-time estimation (exponential smoothing)
        if (t_prev_ > -1e8) {
            double dt = t - t_prev_;
            dt_frame_est_ = dt_frame_est_ * 0.9 + dt * 0.1;
        }
        t_prev_ = t;

        int idx_next = find_next_unjudged(notes, states, t);

        // Phase 1: Release completed holds
        release_completed_holds(t, notes, states);

        // Phase 2: Start holds (highest priority)
        {
            int lo = std::max(0, idx_next - 60);
            int hi = std::min(static_cast<int>(notes.size()), idx_next + 900);
            for (int i = lo; i < hi; ++i) {
                auto& n = notes[i];
                auto& s = states[i];
                if (n.kind != 3 || s.judged || s.hit || s.holding || n.fake)
                    continue;
                if (active_holds_.count(i)) continue;

                double dt = std::abs(t - n.t_hit);
                if (dt > timing_window_) continue;
                if (!should_fire(t, n.t_hit)) continue;

                auto grade = judge.start_hold(s, t);
                if (grade) {
                    active_holds_.insert(i);
                }
            }
        }

        // Phase 3: Hit taps and flicks
        {
            int lo = std::max(0, idx_next - 40);
            int hi = std::min(static_cast<int>(notes.size()), idx_next + 600);
            for (int i = lo; i < hi; ++i) {
                auto& n = notes[i];
                auto& s = states[i];
                if (n.kind != 1 && n.kind != 4) continue;
                if (s.judged || s.hit || n.fake) continue;

                double dt_abs = std::abs(t - n.t_hit);
                if (dt_abs > Judge::BAD) continue;

                if (n.kind == 1) { // Tap
                    if (!should_fire(t, n.t_hit)) continue;
                } else { // Flick — needs prep time
                    double prepare = std::max(dt_frame_est_ * 3.0, 0.04);
                    if (t < n.t_hit && (n.t_hit - t) > prepare) continue;
                }

                judge.try_hit(s, t);
            }
        }

        // Phase 4: Hit drags
        {
            int lo = std::max(0, idx_next - 40);
            int hi = std::min(static_cast<int>(notes.size()), idx_next + 600);
            for (int i = lo; i < hi; ++i) {
                auto& n = notes[i];
                auto& s = states[i];
                if (n.kind != 2) continue;
                if (s.judged || s.hit || n.fake) continue;

                double prepare = std::max(dt_frame_est_ * 2.5, 0.04);
                if (t < n.t_hit) {
                    if ((n.t_hit - t) > prepare) continue;
                } else {
                    if ((t - n.t_hit) > Judge::PERFECT) continue;
                }

                judge.try_hit(s, t);
            }
        }
    }

private:
    SimMode mode_;
    int max_pointers_;
    double timing_window_;
    double dt_frame_est_ = 1.0 / 60.0;
    double t_prev_ = -1e9;
    std::unordered_set<int> active_holds_;

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

    void release_completed_holds(double t, const std::vector<Note>& notes,
                                 std::vector<NoteState>& states) {
        std::vector<int> to_remove;
        for (int idx : active_holds_) {
            auto& n = notes[idx];
            auto& s = states[idx];
            if (t >= n.t_end) {
                s.holding = false;
                to_remove.push_back(idx);
            }
        }
        for (int idx : to_remove) active_holds_.erase(idx);
    }

    bool should_fire(double t, double t_hit) const {
        double dt = t - t_hit;
        if (dt >= 0.0) return true;
        return (-dt) <= dt_frame_est_ * 0.6;
    }
};

} // namespace phigros::engine
