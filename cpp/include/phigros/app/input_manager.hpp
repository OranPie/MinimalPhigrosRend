#pragma once
#include "phigros/core/logger.hpp"
#include "phigros/app/sdl_compat.hpp"
#include "phigros/engine/judge_input.hpp"
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace phigros::app {

// One active pointer (mouse or touch finger).
struct PointerSlot {
    int64_t id = -1;          // -1 = inactive; 0 = mouse; touch fingers use their SDL FingerID
    float x = 0, y = 0;       // current screen pixel position
    float vx = 0, vy = 0;     // estimated velocity (pixels / second), EMA-smoothed
    float peak_speed = 0.0f;  // peak instantaneous speed seen during this gesture
    float last_speed = 0.0f;   // speed in the latest processed frame
    bool down = false;
    bool press_edge = false;   // true only on the frame the pointer went down
    bool release_edge = false; // true only on the frame the pointer went up
    bool flick = false;        // true at release when peak_speed > flick_vel_threshold
    // internal velocity tracking
    float _px = 0, _py = 0;   // position at start of this frame (for velocity calc)

    bool active() const { return id >= 0; }
};

struct KeyAction {
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    bool down = false;
    bool press_edge = false;
    bool release_edge = false;
    bool active() const { return scancode != SDL_SCANCODE_UNKNOWN && (down || press_edge || release_edge); }
};

struct InputManager {
    static constexpr int MAX = 10;
    PointerSlot slots[MAX];
    int active_count = 0;
    // Lower threshold on mobile (screens smaller → pixel density higher, motions smaller)
#ifdef PHIGROS_FLICK_THRESHOLD_PX_S
    float flick_vel_threshold = PHIGROS_FLICK_THRESHOLD_PX_S;
#else
    float flick_vel_threshold = 1200.0f; // pixels/second
#endif

    // EMA smoothing half-life for velocity estimation (seconds).
    // Velocity at each frame = alpha*new_raw + (1-alpha)*old_ema, where
    // alpha = min(1, dt / VEL_EMA_HALF_LIFE).  Larger = more responsive but noisier.
    static constexpr float VEL_EMA_HALF_LIFE = 0.050f; // 50 ms

    static constexpr int MAX_KEYS = 10;
    KeyAction keys[MAX_KEYS];
    int num_gameplay_keys = 4;
    SDL_Scancode gameplay_scancodes[MAX_KEYS] = {
        SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_J, SDL_SCANCODE_K
    };

    // Call once per frame BEFORE processing events. Clears per-frame edge flags.
    void begin_frame() {
        PHLOG_TRACE(Input, "Input begin_frame active_pointers=" << active_count);
        for (auto& s : slots) {
            s.press_edge = s.release_edge = s.flick = false;
            s._px = s.x; s._py = s.y;
        }
        for (int i = 0; i < num_gameplay_keys; ++i) {
            keys[i].press_edge = false;
            keys[i].release_edge = false;
        }
    }

    // Call once per frame AFTER processing all events. Computes EMA velocity and tracks peak speed.
    void end_frame(double dt) {
        float inv_dt = (dt > 1e-6) ? static_cast<float>(1.0 / dt) : 0.0f;
        // EMA smoothing: blend toward the raw per-frame velocity over VEL_EMA_HALF_LIFE seconds
        float alpha = std::min(1.0f, static_cast<float>(dt) / VEL_EMA_HALF_LIFE);
        for (auto& s : slots) {
            if (!s.active()) continue;
            float raw_vx = (s.x - s._px) * inv_dt;
            float raw_vy = (s.y - s._py) * inv_dt;
            // Exponential moving average for velocity
            s.vx = alpha * raw_vx + (1.0f - alpha) * s.vx;
            s.vy = alpha * raw_vy + (1.0f - alpha) * s.vy;
            // Track peak speed during the gesture for flick detection
            float spd = std::sqrt(s.vx * s.vx + s.vy * s.vy);
            s.last_speed = spd;
            if (spd > s.peak_speed) s.peak_speed = spd;
            if (spd > flick_vel_threshold) s.flick = true;
            PHLOG_TRACE(Input, "PointerState id=" << s.id
                << " pos=(" << s.x << "," << s.y << ")"
                << " vel=(" << s.vx << "," << s.vy << ")"
                << " peak=" << s.peak_speed
                << " down=" << s.down);
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
                s->peak_speed = 0.0f;
                s->last_speed = 0.0f;
                PHLOG_TRACE(Input, "MouseDown (" << s->x << "," << s->y << ")");
            }
        } else if (e.type == PHIGROS_SDL_MOUSE_UP) {
            auto* s = find_slot(0);
            if (s) {
                s->x = PHIGROS_MOUSE_X(e);
                s->y = PHIGROS_MOUSE_Y(e);
                float dx = s->x - s->_px;
                float dy = s->y - s->_py;
                bool instant_flick =
                    dx * dx + dy * dy > flick_vel_threshold * flick_vel_threshold * 0.0004f;
                // Use peak speed seen during the gesture for reliable flick detection
                s->flick = s->flick || s->peak_speed > flick_vel_threshold || instant_flick;
                s->down = false;
                s->release_edge = true;
                PHLOG_TRACE(Input, "MouseUp (" << s->x << "," << s->y
                    << ") peak_speed=" << s->peak_speed
                    << (s->flick ? " FLICK" : ""));
            }
        } else if (e.type == PHIGROS_SDL_MOUSE_MOVE) {
            auto* s = find_slot(0);
            if (s && s->down) {
                s->x = PHIGROS_MOTION_X(e);
                s->y = PHIGROS_MOTION_Y(e);
                float dx = s->x - s->_px;
                float dy = s->y - s->_py;
                if (dx * dx + dy * dy > flick_vel_threshold * flick_vel_threshold * 0.0004f) {
                    s->peak_speed = std::max(s->peak_speed, flick_vel_threshold + 1.0f);
                    s->flick = true;
                }
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
                s->peak_speed = 0.0f;
                s->last_speed = 0.0f;
                PHLOG_TRACE(Input, "FingerDown id=" << fid
                    << " (" << s->x << "," << s->y << ")");
            }
        } else if (e.type == PHIGROS_SDL_FINGER_UP) {
            int64_t fid = PHIGROS_FINGER_ID(e) + 1;
            auto* s = find_slot(fid);
            if (s) {
                s->x = e.tfinger.x * static_cast<float>(W);
                s->y = e.tfinger.y * static_cast<float>(H);
                float dx = s->x - s->_px;
                float dy = s->y - s->_py;
                bool instant_flick =
                    dx * dx + dy * dy > flick_vel_threshold * flick_vel_threshold * 0.0004f;
                if (instant_flick) {
                    s->peak_speed = std::max(s->peak_speed, flick_vel_threshold + 1.0f);
                    s->flick = true;
                }
                // Use peak speed seen during the gesture for reliable flick detection
                s->flick = s->flick || s->peak_speed > flick_vel_threshold || instant_flick;
                s->down = false;
                s->release_edge = true;
                PHLOG_TRACE(Input, "FingerUp id=" << fid
                    << " (" << s->x << "," << s->y << ")"
                    << " peak_speed=" << s->peak_speed
                    << (s->flick ? " FLICK" : ""));
                // Deactivate after release_edge is consumed next frame
            }
        } else if (e.type == PHIGROS_SDL_FINGER_MOVE) {
            int64_t fid = PHIGROS_FINGER_ID(e) + 1;
            auto* s = find_slot(fid);
            if (s && s->down) {
                s->x = e.tfinger.x * static_cast<float>(W);
                s->y = e.tfinger.y * static_cast<float>(H);
            }
        } else if (e.type == PHIGROS_SDL_EVENT_KEY_DOWN) {
            auto sc = PHIGROS_KEY_SCANCODE(e);
            for (int i = 0; i < num_gameplay_keys; ++i) {
                if (gameplay_scancodes[i] == sc && !keys[i].down) {
                    keys[i].scancode = sc;
                    keys[i].down = true;
                    keys[i].press_edge = true;
                    PHLOG_TRACE(Input, "KeyDown scancode=" << static_cast<int>(sc)
                        << " slot=" << i);
                    break;
                }
            }
        } else if (e.type == PHIGROS_SDL_EVENT_KEY_UP) {
            auto sc = PHIGROS_KEY_SCANCODE(e);
            for (int i = 0; i < num_gameplay_keys; ++i) {
                if (gameplay_scancodes[i] == sc && keys[i].down) {
                    keys[i].down = false;
                    keys[i].release_edge = true;
                    PHLOG_TRACE(Input, "KeyUp scancode=" << static_cast<int>(sc)
                        << " slot=" << i);
                    break;
                }
            }
        }

        (void)W; (void)H; // suppress unused warning when no finger events
    }

    // Deactivate released fingers after edges have been consumed.
    void flush_released() {
        for (auto& s : slots) {
            if (s.active() && s.release_edge && !s.down) {
                PHLOG_TRACE(Input, "PointerRelease flush id=" << s.id);
                s.id = -1;
                --active_count;
                if (active_count < 0) active_count = 0;
            }
        }
    }

    // Build a platform-agnostic JudgeInputFrame from current pointer + key state.
    engine::JudgeInputFrame to_judge_input() const {
        engine::JudgeInputFrame frame;
        for (const auto& s : slots) {
            if (!s.active()) continue;
            frame.add({s.id, true, s.x, s.y,
                       s.press_edge, s.release_edge, s.down, s.flick});
        }
        for (int i = 0; i < num_gameplay_keys; ++i) {
            if (!keys[i].active()) continue;
            frame.add({-(int64_t)(i + 1), false, 0, 0,
                       keys[i].press_edge, keys[i].release_edge, keys[i].down, false});
        }
        PHLOG_TRACE(Input, "JudgeInputFrame actions=" << frame.count);
        return frame;
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
                PHLOG_TRACE(Input, "Pointer alloc id=" << id
                    << " active_count=" << active_count);
                return &s;
            }
        }
        PHLOG_WARN(Input, "Pointer allocation failed for id=" << id
            << " max_slots=" << MAX);
        return nullptr; // no free slots
    }
};

} // namespace phigros::app
