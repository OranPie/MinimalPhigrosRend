#pragma once
#include "phigros/core/logger.hpp"
#include "phigros/engine/judge_input.hpp"
#include "phigros/engine/judge.hpp"
#include "phigros/engine/effects.hpp"
#include "phigros/render/renderer.hpp"  // FrameSnapshot / NoteSnapshot
#include "phigros/core/types.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
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

    // ptr_slot_id → line_id (for tracking which drag chain each pointer follows)
    // A pointer starts following a drag chain when it auto-catches a drag note.
    // It leaves the chain when it releases.
    std::unordered_map<int64_t, int> drag_chain_map;

    // Default hitfx tints (typically from respack config).
    math::RGB hitfx_color_perfect{255, 236, 159};
    math::RGB hitfx_color_good{180, 225, 255};
    uint8_t hitfx_alpha_perfect = 0xe1;
    uint8_t hitfx_alpha_good = 0xeb;

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

        // Per-frame deduplication: prevent two inputs from judging the same note
        std::unordered_set<int> judged_this_frame;

        // 1. Handle early hold releases from lifted fingers/keys
        for (int ai = 0; ai < input.count; ++ai) {
            const auto& a = input.actions[ai];
            if (!a.release) continue;
            // Release hold tracking
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
            // Release drag chain tracking
            drag_chain_map.erase(a.id);
        }

        // 2. Handle presses — find and hit nearest in-window note
        for (int ai = 0; ai < input.count; ++ai) {
            const auto& a = input.actions[ai];
            if (!a.press) continue;

            // A pointer already holding a hold note can still press new notes
            // (hold is on a separate finger slot), but we prevent double-judging
            // via judged_this_frame.

            int best_nidx = -1;
            float best_dist2 = std::numeric_limits<float>::max();
            double best_dt = std::numeric_limits<double>::max();

            for (const auto& ns : frame.notes) {
                if (ns.judged || ns.miss) continue;
                int nidx = ns.nid;
                if (nidx < 0 || nidx >= static_cast<int>(notes.size())) continue;
                const auto& note = notes[nidx];
                if (note.fake) continue;
                // Skip if already judged earlier this frame by another input
                if (judged_this_frame.count(nidx)) continue;

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
                    float note_r2 = r2 * static_cast<float>(
                        std::max(0.0, note.judge_area) * std::max(0.0, note.judge_area));
                    if (d2 > note_r2) continue;

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

            if (best_nidx < 0) {
                PHLOG_TRACE(Input, "Press had no candidate ptr=" << a.id
                    << " t=" << t
                    << (a.has_position ? " positional=1" : " positional=0"));
                continue;
            }
            const auto& note = notes[best_nidx];
            auto& ns = states[best_nidx];

            if (note.kind == 3) {
                auto grade = judge.start_hold(ns, t);
                if (grade) {
                    holding_map[a.id] = best_nidx;
                    // Starting a hold clears any drag chain this pointer was following
                    drag_chain_map.erase(a.id);
                    judged_this_frame.insert(best_nidx);
                    _emit_effect(effects, frame, best_nidx, t, note, *grade);
                    if (on_judgment) on_judgment(best_nidx, (float)t, "hold_start:" + *grade);
                    PHLOG_DEBUG(Input, "HoldStart note=" << best_nidx
                        << " grade=" << *grade << " t=" << t << " ptr=" << a.id);
                }
            } else {
                auto grade = judge.try_hit(ns, t);
                if (grade) {
                    judged_this_frame.insert(best_nidx);
                    _emit_effect(effects, frame, best_nidx, t, note, *grade);
                    if (on_judgment) on_judgment(best_nidx, (float)t, *grade);
                    PHLOG_DEBUG(Input, "Hit note=" << best_nidx
                        << " grade=" << *grade << " t=" << t << " ptr=" << a.id);
                }
            }
        }

        // 3. Auto-catch drag notes (kind=2) by any nearby down pointer or held key.
        //    Pointers actively holding a hold note are excluded — they are "busy"
        //    and should not accidentally capture a passing drag chain.
        //    Drag chain ownership: once pointer P catches a drag on line L, it is
        //    recorded in drag_chain_map[P] = L. Subsequent drag notes on line L
        //    are preferentially caught only by pointer P; other lines are still
        //    open to any free pointer.
        for (const auto& ns : frame.notes) {
            if (ns.judged || ns.miss || ns.kind != 2) continue;
            int nidx = ns.nid;
            if (nidx < 0 || nidx >= static_cast<int>(notes.size())) continue;
            const auto& note = notes[nidx];
            if (note.fake) continue;
            if (judged_this_frame.count(nidx)) continue;
            double dt = std::abs(t - note.t_hit);
            if (dt > Judge::BAD) continue;

            // Determine if any pointer owns this note's drag chain
            int64_t chain_owner = -1;
            for (const auto& [pid, lid] : drag_chain_map) {
                if (lid == note.line_id) { chain_owner = pid; break; }
            }

            bool caught = false;
            int64_t catching_ptr = -1;

            for (int ai = 0; ai < input.count && !caught; ++ai) {
                const auto& a = input.actions[ai];
                if (!a.down) continue;

                // Skip pointers that are busy holding a hold note
                if (holding_map.count(a.id)) continue;

                // If another pointer owns this drag chain, skip
                if (chain_owner >= 0 && a.id != chain_owner) continue;

                if (a.has_position) {
                    float dx = a.x - static_cast<float>(ns.wx);
                    float dy = a.y - static_cast<float>(ns.wy);
                    float note_r2 = r2 * static_cast<float>(
                        std::max(0.0, note.judge_area) * std::max(0.0, note.judge_area));
                    if (dx * dx + dy * dy <= note_r2) {
                        caught = true;
                        catching_ptr = a.id;
                    }
                } else {
                    // Any held key auto-catches drags (keyboard play)
                    caught = true;
                    catching_ptr = a.id;
                }
            }

            if (caught) {
                auto grade = judge.try_hit(states[nidx], t);
                if (grade) {
                    judged_this_frame.insert(nidx);
                    // Record drag chain ownership for this pointer
                    if (catching_ptr >= 0) drag_chain_map[catching_ptr] = note.line_id;
                    _emit_effect(effects, frame, nidx, t, note, *grade);
                    if (on_judgment) on_judgment(nidx, (float)t, *grade);
                    PHLOG_TRACE(Input, "DragCatch note=" << nidx
                        << " line=" << note.line_id << " t=" << t << " ptr=" << catching_ptr);
                }
            } else {
                PHLOG_TRACE(Input, "DragMiss note=" << nidx
                    << " line=" << note.line_id << " t=" << t);
            }
        }
    }

private:
    math::RGB _resolve_hitfx_color(const Note& note, const std::string& grade) const {
        if (note.tint_hitfx_rgb) return *note.tint_hitfx_rgb;
        if (grade == "GOOD" || grade == "BAD") return hitfx_color_good;
        return hitfx_color_perfect;
    }

    std::string _resolve_hitfx_variant(const std::string& grade) const {
        return (grade == "GOOD" || grade == "BAD") ? "good" : "perfect";
    }

    uint8_t _resolve_hitfx_alpha(const std::string& grade) const {
        return (grade == "GOOD" || grade == "BAD") ? hitfx_alpha_good : hitfx_alpha_perfect;
    }

    void _emit_effect(EffectManager& effects,
                      const phigros::render::FrameSnapshot& frame,
                      int nidx, double t, const Note& note, const std::string& grade) {
        // Find this note's world position in the snapshot
        for (const auto& ns : frame.notes) {
            if (ns.nid == nidx) {
                math::RGB color = _resolve_hitfx_color(note, grade);
                effects.add_hitfx(ns.wx, ns.wy, t, color,
                                  0.0, 0.0,
                                  _resolve_hitfx_variant(grade),
                                  _resolve_hitfx_alpha(grade));
                effects.add_particle_burst(ns.wx, ns.wy, t * 1000.0, 500.0, color);
                PHLOG_TRACE(Input, "EmitEffect note=" << nidx
                    << " grade=" << grade
                    << " pos=(" << ns.wx << "," << ns.wy << ")");
                return;
            }
        }
        PHLOG_TRACE(Input, "EmitEffect skipped: note snapshot missing for note=" << nidx);
    }
};

} // namespace phigros::engine
