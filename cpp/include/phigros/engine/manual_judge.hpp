#pragma once
#include "phigros/engine/judge_input.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/effects.hpp"
#include "phigros/render/renderer.hpp"  // FrameSnapshot / NoteSnapshot
#include "phigros/core/types.hpp"
#include <unordered_map>
#include <cmath>
#include <limits>
#include <functional>

namespace phigros::engine {

// Spatial + temporal note judgment for interactive --play mode.
// Replaces SimulatePlayer. Called once per rendered frame with the current
// InputManager state and FrameSnapshot note world positions.
struct ManualJudge {
    float catch_radius_factor = 0.12f; // fraction of screen height

    // ptr_slot_id → note index (for tracking active holds)
    std::unordered_map<int64_t, int> holding_map;

    // Default hitfx tints (typically from respack config).
    math::RGB hitfx_color_perfect{235, 255, 236};
    math::RGB hitfx_color_good{235, 180, 225};

    // Optional callback invoked whenever a note is judged (for ReplayWriter)
    std::function<void(int note_idx, float t, const std::string& grade)> on_judgment;

    // Process one rendered frame.
    void process_frame(
        const JudgeInputFrame& input,
        const phigros::render::FrameSnapshot& frame,
        const std::vector<Note>& notes,
        std::vector<NoteState>& states,
        Judge& judge,
        EffectManager& effects,
        double t, int W, int H)
    {
        float radius = catch_radius_factor * static_cast<float>(H);
        float r2 = radius * radius;

        // 1. Handle early hold releases from lifted fingers/keys
        for (int ai = 0; ai < input.count; ++ai) {
            const auto& a = input.actions[ai];
            if (!a.release) continue;
            auto it = holding_map.find(a.id);
            if (it != holding_map.end()) {
                int nidx = it->second;
                if (nidx >= 0 && nidx < static_cast<int>(states.size())) {
                    auto& ns = states[nidx];
                    if (ns.holding && !ns.hold_finalized) {
                        ns.holding = false;
                        ns.released_early = true;
                        ns.release_t = t;
                    }
                }
                holding_map.erase(it);
            }
        }

        // 2. Handle presses — find and hit nearest in-window note
        for (int ai = 0; ai < input.count; ++ai) {
            const auto& a = input.actions[ai];
            if (!a.press) continue;

            int best_nidx = -1;
            float best_dist2 = std::numeric_limits<float>::max();
            double best_dt = std::numeric_limits<double>::max();

            for (const auto& ns : frame.notes) {
                if (ns.judged || ns.miss) continue;
                int nidx = ns.nid;
                if (nidx < 0 || nidx >= static_cast<int>(notes.size())) continue;
                const auto& note = notes[nidx];
                if (note.fake) continue;

                // Timing window check
                double dt = std::abs(t - note.t_hit);
                if (dt > Judge::BAD) continue;

                // Flick notes: pointer needs flick gesture; keyboard press counts
                if (note.kind == 4 && a.has_position && !a.flick) continue;

                if (a.has_position) {
                    // Spatial check for pointer input
                    float dx = a.x - static_cast<float>(ns.wx);
                    float dy = a.y - static_cast<float>(ns.wy);
                    float d2 = dx * dx + dy * dy;
                    if (d2 > r2) continue;

                    if (dt < best_dt - 0.001 || (std::abs(dt - best_dt) < 0.001 && d2 < best_dist2)) {
                        best_dt = dt;
                        best_dist2 = d2;
                        best_nidx = nidx;
                    }
                } else {
                    // Keyboard: temporal-only — find nearest unjudged note
                    if (dt < best_dt) {
                        best_dt = dt;
                        best_nidx = nidx;
                    }
                }
            }

            if (best_nidx < 0) continue;
            const auto& note = notes[best_nidx];
            auto& ns = states[best_nidx];

            if (note.kind == 3) {
                auto grade = judge.start_hold(ns, t);
                if (grade) {
                    holding_map[a.id] = best_nidx;
                    _emit_effect(effects, frame, best_nidx, t, note, *grade);
                    if (on_judgment) on_judgment(best_nidx, (float)t, "hold_start:" + *grade);
                }
            } else {
                auto grade = judge.try_hit(ns, t);
                if (grade) {
                    _emit_effect(effects, frame, best_nidx, t, note, *grade);
                    if (on_judgment) on_judgment(best_nidx, (float)t, *grade);
                }
            }
        }

        // 3. Auto-catch drag notes (kind=2) by any nearby down pointer or held key
        for (const auto& ns : frame.notes) {
            if (ns.judged || ns.miss || ns.kind != 2) continue;
            int nidx = ns.nid;
            if (nidx < 0 || nidx >= static_cast<int>(notes.size())) continue;
            const auto& note = notes[nidx];
            if (note.fake) continue;
            double dt = std::abs(t - note.t_hit);
            if (dt > Judge::BAD) continue;

            bool caught = false;
            for (int ai = 0; ai < input.count && !caught; ++ai) {
                const auto& a = input.actions[ai];
                if (!a.down) continue;

                if (a.has_position) {
                    float dx = a.x - static_cast<float>(ns.wx);
                    float dy = a.y - static_cast<float>(ns.wy);
                    if (dx * dx + dy * dy <= r2) caught = true;
                } else {
                    // Any held key auto-catches drags
                    caught = true;
                }
            }

            if (caught) {
                auto grade = judge.try_hit(states[nidx], t);
                if (grade) {
                    _emit_effect(effects, frame, nidx, t, note, *grade);
                    if (on_judgment) on_judgment(nidx, (float)t, *grade);
                }
            }
        }
    }

private:
    math::RGB _resolve_hitfx_color(const Note& note, const std::string& grade) const {
        if (note.tint_hitfx_rgb) return *note.tint_hitfx_rgb;
        if (grade == "GOOD" || grade == "BAD") return hitfx_color_good;
        return hitfx_color_perfect;
    }

    void _emit_effect(EffectManager& effects,
                      const phigros::render::FrameSnapshot& frame,
                      int nidx, double t, const Note& note, const std::string& grade) {
        // Find this note's world position in the snapshot
        for (const auto& ns : frame.notes) {
            if (ns.nid == nidx) {
                math::RGB color = _resolve_hitfx_color(note, grade);
                effects.add_hitfx(ns.wx, ns.wy, t, color);
                effects.add_particle_burst(ns.wx, ns.wy, t * 1000.0, 500.0, color);
                return;
            }
        }
    }
};

} // namespace phigros::engine
