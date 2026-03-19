#pragma once
#include "phigros/core/types.hpp"
#include "phigros/engine/judge.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace phigros::engine {

// ---- Miss detection for non-hold notes ----
inline void detect_misses(
    std::vector<NoteState>& states,
    int idx_next, double t,
    double miss_window,
    Judge& judge)
{
    int st0 = std::max(0, idx_next - 200);
    int st1 = std::min(static_cast<int>(states.size()), idx_next + 800);

    for (int i = st0; i < st1; ++i) {
        auto& s = states[i];
        if (s.judged || s.note->fake) continue;
        if (s.note->kind == 3) continue; // holds handled by hold_finalize

        if (t > s.note->t_hit + miss_window) {
            judge.mark_miss(s);
        }
    }
}

// ---- Hold maintenance: track active holds ----
inline void hold_maintenance(
    std::vector<NoteState>& states,
    int idx_next, double t,
    double hold_tail_tol,
    Judge& judge)
{
    int st0 = std::max(0, idx_next - 50);
    int st1 = std::min(static_cast<int>(states.size()), idx_next + 500);

    for (int i = st0; i < st1; ++i) {
        auto& s = states[i];
        if (!s.holding || s.note->fake) continue;
        if (s.note->kind != 3) continue;

        auto& n = *s.note;

        // Release hold when past end time
        if (t >= n.t_end) {
            s.holding = false;
        }
    }
}

// ---- Hold finalization: grade completed holds ----
inline void hold_finalize(
    std::vector<NoteState>& states,
    int idx_next, double t,
    double hold_tail_tol,
    double miss_window,
    Judge& judge)
{
    int st0 = std::max(0, idx_next - 200);
    int st1 = std::min(static_cast<int>(states.size()), idx_next + 800);

    for (int i = st0; i < st1; ++i) {
        auto& s = states[i];
        if (s.note->fake) continue;
        if (s.note->kind != 3) continue;
        if (s.hold_finalized) continue;

        auto& n = *s.note;

        // Not hit yet and past miss window → immediately finalize as MISS.
        // Calling finalize_hold here (instead of bare break_combo) ensures
        // the hold is marked judged+miss right away, so build_frame stops
        // rendering it as a normal active note for the remainder of t_end.
        if (!s.hit && !s.hold_failed && t > n.t_hit + miss_window) {
            s.hold_failed = true;
            judge.finalize_hold(s);
        }

        // Released early: finalize based on progress.
        // Do NOT call break_combo() here; finalize_hold handles it when hold_failed.
        if (s.released_early && !s.hold_finalized) {
            double dur = n.t_end - n.t_hit;
            double progress = dur > 1e-9 ? (s.release_t - n.t_hit) / dur : 1.0;

            if (progress < hold_tail_tol) {
                s.hold_failed = true;
            }
            judge.finalize_hold(s);
        }

        // Time past t_end → finalize
        if (t >= n.t_end && !s.hold_finalized) {
            judge.finalize_hold(s);
        }
    }
}

} // namespace phigros::engine
