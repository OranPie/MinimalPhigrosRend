#pragma once

#include "phigros/core/types.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/engine/simulateplay.hpp"
#include <vector>

namespace phigros::engine {

inline int find_exact_autoplay_start(const std::vector<NoteState>& states, double t) {
    int lo = 0, hi = static_cast<int>(states.size());
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (states[mid].judged || states[mid].holding || states[mid].note->t_hit < t - Judge::BAD)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

inline void exact_autoplay_step(
    double prev_t,
    double t,
    const std::vector<Note>& notes,
    std::vector<NoteState>& states,
    const std::vector<Line>& lines,
    Judge& judge,
    int W,
    int H,
    std::vector<SimHitEvent>* hit_events = nullptr)
{
    (void)W;
    (void)H;
    const int idx = find_exact_autoplay_start(states, t);
    const int hi = std::min(static_cast<int>(notes.size()), idx + 1024);

    for (int i = idx; i < hi; ++i) {
        const auto& n = notes[i];
        auto& s = states[i];
        if (n.fake || s.judged || s.hold_finalized || s.holding) continue;
        if (n.t_hit > t + 1e-9) break;
        if (n.t_hit <= prev_t + 1e-9) continue;

        std::optional<std::string> grade;
        if (n.kind == 3) grade = judge.start_hold(s, n.t_hit);
        else             grade = judge.try_hit(s, n.t_hit);
        if (!grade) continue;

        if (hit_events) {
            double x = 0.0, y = 0.0;
            if (n.line_id >= 0 && n.line_id < static_cast<int>(lines.size())) {
                auto ls = eval_line_state(lines[n.line_id], n.t_hit);
                auto pos = note_world_pos_cs(ls.x, ls.y, ls.cos_rot, ls.sin_rot,
                                             ls.scroll, n, n.scroll_hit,
                                             false, 1.0, false, n.kind == 3);
                x = pos.x;
                y = pos.y;
            }
            hit_events->push_back({i, n.t_hit, 0.0, x, y, *grade});
        }
    }
}

} // namespace phigros::engine
