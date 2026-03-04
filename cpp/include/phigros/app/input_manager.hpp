#pragma once
#include "phigros/app/sdl_compat.hpp"
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace phigros::app {

// One active pointer (mouse or touch finger).
struct PointerSlot {
    int64_t id = -1;          // -1 = inactive; 0 = mouse; touch fingers use their SDL FingerID
    float x = 0, y = 0;       // current screen pixel position
    float vx = 0, vy = 0;     // estimated velocity (pixels / second)
    bool down = false;
    bool press_edge = false;   // true only on the frame the pointer went down
    bool release_edge = false; // true only on the frame the pointer went up
    bool flick = false;        // true at release when speed > flick_vel_threshold
    // internal velocity tracking
    float _px = 0, _py = 0;   // position at start of this frame (for velocity calc)

    bool active() const { return id >= 0; }
};

struct InputManager {
    static constexpr int MAX = 10;
    PointerSlot slots[MAX];
    int active_count = 0;
    float flick_vel_threshold = 1200.0f; // pixels/second

    // Call once per frame BEFORE processing events. Clears per-frame edge flags.
    void begin_frame() {
        for (auto& s : slots) {
            s.press_edge = s.release_edge = s.flick = false;
            s._px = s.x; s._py = s.y;
        }
    }

    // Call once per frame AFTER processing all events. Computes velocity.
    void end_frame(double dt) {
        float inv_dt = (dt > 1e-6) ? static_cast<float>(1.0 / dt) : 0.0f;
        for (auto& s : slots) {
            if (!s.active()) continue;
            s.vx = (s.x - s._px) * inv_dt;
            s.vy = (s.y - s._py) * inv_dt;
        }
    }

    void process_event(const SDL_Event& e, int W, int H) {
        if (e.type == PHIGROS_SDL_MOUSE_DOWN) {
            auto* s = get_or_alloc(0);
            if (s) {
                s->x = PHIGROS_MOUSE_X(e);
                s->y = PHIGROS_MOUSE_Y(e);
                s->down = true;
                s->press_edge = true;
                s->vx = s->vy = 0;
            }
        } else if (e.type == PHIGROS_SDL_MOUSE_UP) {
            auto* s = find_slot(0);
            if (s) {
                s->x = PHIGROS_MOUSE_X(e);
                s->y = PHIGROS_MOUSE_Y(e);
                float speed = std::sqrt(s->vx * s->vx + s->vy * s->vy);
                s->flick = speed > flick_vel_threshold;
                s->down = false;
                s->release_edge = true;
            }
        } else if (e.type == PHIGROS_SDL_MOUSE_MOVE) {
            auto* s = find_slot(0);
            if (s && s->down) {
                s->x = PHIGROS_MOTION_X(e);
                s->y = PHIGROS_MOTION_Y(e);
            }
        } else if (e.type == PHIGROS_SDL_FINGER_DOWN) {
            int64_t fid = PHIGROS_FINGER_ID(e) + 1; // offset by 1 (0 = mouse)
            auto* s = get_or_alloc(fid);
            if (s) {
                s->x = e.tfinger.x * static_cast<float>(W);
                s->y = e.tfinger.y * static_cast<float>(H);
                s->down = true;
                s->press_edge = true;
                s->vx = s->vy = 0;
            }
        } else if (e.type == PHIGROS_SDL_FINGER_UP) {
            int64_t fid = PHIGROS_FINGER_ID(e) + 1;
            auto* s = find_slot(fid);
            if (s) {
                s->x = e.tfinger.x * static_cast<float>(W);
                s->y = e.tfinger.y * static_cast<float>(H);
                float speed = std::sqrt(s->vx * s->vx + s->vy * s->vy);
                s->flick = speed > flick_vel_threshold;
                s->down = false;
                s->release_edge = true;
                // Deactivate after release_edge is consumed next frame
            }
        } else if (e.type == PHIGROS_SDL_FINGER_MOVE) {
            int64_t fid = PHIGROS_FINGER_ID(e) + 1;
            auto* s = find_slot(fid);
            if (s && s->down) {
                s->x = e.tfinger.x * static_cast<float>(W);
                s->y = e.tfinger.y * static_cast<float>(H);
            }
        }

        (void)W; (void)H; // suppress unused warning when no finger events
    }

    // Deactivate released fingers after edges have been consumed.
    void flush_released() {
        for (auto& s : slots) {
            if (s.active() && s.release_edge && !s.down) {
                s.id = -1;
                --active_count;
                if (active_count < 0) active_count = 0;
            }
        }
    }

private:
    PointerSlot* find_slot(int64_t id) {
        for (auto& s : slots)
            if (s.id == id) return &s;
        return nullptr;
    }
    PointerSlot* get_or_alloc(int64_t id) {
        auto* existing = find_slot(id);
        if (existing) return existing;
        for (auto& s : slots) {
            if (!s.active()) {
                s = PointerSlot{};
                s.id = id;
                ++active_count;
                return &s;
            }
        }
        return nullptr; // no free slots
    }
};

} // namespace phigros::app
