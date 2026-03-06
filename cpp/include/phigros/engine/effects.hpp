#pragma once
#include "phigros/core/types.hpp"
#include "phigros/engine/kinematics.hpp"
#include "phigros/math/util.hpp"
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>

namespace phigros::engine {

struct HitFX {
    double x, y;
    double t0;
    math::RGB rgba;
    double rot = 0.0;
    double rot_speed = 0.0;   // radians/sec — for sheet rotation animation
    std::string variant;      // "" = default, "good" = GOOD effect sheet

    bool alive(double t, double duration = 0.5) const {
        return t < t0 + duration;
    }
};

struct Particle {
    double speed;
    double angle;
};

// Brief expanding ring drawn at hit position (fallback when no respack sheet).
struct FlashFX {
    double x, y;
    double t0;
    math::RGB color;
    float radius_start = 12.0f;
    float radius_end   = 70.0f;

    static constexpr double DURATION = 0.18;
    bool alive(double t) const { return t < t0 + DURATION; }
};

class ParticleBurst {
public:
    double x, y;
    double start_ms;
    double duration_ms;
    math::RGB rgba;
    std::vector<Particle> particles;

    ParticleBurst(double x_, double y_, double start_ms_, double duration_ms_,
                  math::RGB rgba_, int count = 4, unsigned seed = 0)
        : x(x_), y(y_), start_ms(start_ms_),
          duration_ms(std::max(1.0, duration_ms_)), rgba(rgba_)
    {
        std::mt19937 rng(seed ? seed : static_cast<unsigned>(
            std::abs(start_ms_ * 1000.0 + x_)));
        std::uniform_real_distribution<double> speed_dist(185.0, 265.0);
        std::uniform_real_distribution<double> angle_dist(0.0, 6.283185307);

        particles.reserve(count);
        for (int i = 0; i < count; ++i)
            particles.push_back({speed_dist(rng), angle_dist(rng)});
    }

    bool alive(double now_ms) const {
        return now_ms < start_ms + duration_ms;
    }

    struct State {
        double x, y;
        int size;
        math::RGB color;
        int alpha;
    };

    std::vector<State> get_particles(double now_ms) const {
        double tick = (now_ms - start_ms) / duration_ms;
        tick = std::max(0.0, std::min(1.0, tick));

        int alpha = static_cast<int>(255.0 * (1.0 - tick));
        // Cubic curve: peaks quickly, then shrinks — matches Python reference
        double sz = 20.0 * (((0.2078 * tick - 1.6524) * tick + 1.6399) * tick + 0.4988);
        sz = std::max(2.0, sz);

        std::vector<State> out;
        out.reserve(particles.size());
        for (const auto& p : particles) {
            // Deceleration curve: fast burst then gradual stop
            double dist = p.speed * (9.0 * tick / (8.0 * tick + 1.0)) / 2.0;
            out.push_back({
                x + dist * std::cos(p.angle),
                y + dist * std::sin(p.angle),
                static_cast<int>(sz), rgba, alpha
            });
        }
        return out;
    }

    // In-place version avoids per-call allocation (caller must clear out between bursts).
    void get_particles_inplace(double now_ms, std::vector<State>& out) const {
        double tick = (now_ms - start_ms) / duration_ms;
        tick = std::max(0.0, std::min(1.0, tick));
        int alpha = static_cast<int>(255.0 * (1.0 - tick));
        double sz = std::max(2.0, 20.0 * (((0.2078 * tick - 1.6524) * tick + 1.6399) * tick + 0.4988));
        for (const auto& p : particles) {
            double dist = p.speed * (9.0 * tick / (8.0 * tick + 1.0)) / 2.0;
            out.push_back({
                x + dist * std::cos(p.angle),
                y + dist * std::sin(p.angle),
                static_cast<int>(sz), rgba, alpha
            });
        }
    }
};

class EffectManager {
public:
    std::vector<HitFX> hitfx;
    std::vector<ParticleBurst> particles;
    std::vector<FlashFX> flashes;
    int particle_count = 8;  // particles per burst; set from RenderConfig

    // Add a hit-flash animation at (x,y). Also spawns a FlashFX ring.
    void add_hitfx(double x, double y, double t, math::RGB color, double rot = 0.0,
                   double rot_speed = 0.0, const std::string& variant = "") {
        hitfx.push_back({x, y, t, color, rot, rot_speed, variant});
        flashes.push_back({x, y, t, color});
    }

    // Add a particle burst. Pass count=-1 to use particle_count.
    void add_particle_burst(double x, double y, double t_ms, double dur_ms,
                            math::RGB color, int count = -1) {
        int n = (count >= 0) ? count : particle_count;
        particles.emplace_back(x, y, t_ms, dur_ms, color, n);
    }

    void update(double t, double t_ms, double hitfx_duration = 0.5) {
        auto prune = [](auto& vec, auto pred) {
            vec.erase(std::remove_if(vec.begin(), vec.end(), pred), vec.end());
        };
        prune(hitfx,     [&](const HitFX& fx)       { return !fx.alive(t, hitfx_duration); });
        prune(particles, [&](const ParticleBurst& pb){ return !pb.alive(t_ms); });
        prune(flashes,   [&](const FlashFX& f)       { return !f.alive(t); });
    }

    // Generate hold tick effects for active holds
    void hold_tick_fx(std::vector<NoteState>& states, int idx_next,
                      double t, int hold_fx_interval_ms,
                      const std::vector<Line>& lines,
                      math::RGB default_hitfx_color = {235, 255, 236}) {
        int st0 = std::max(0, idx_next - 50);
        int st1 = std::min(static_cast<int>(states.size()), idx_next + 500);
        int now_ms = static_cast<int>(t * 1000.0);

        for (int i = st0; i < st1; ++i) {
            auto& s = states[i];
            if (!s.holding || s.note->fake || s.note->kind != 3) continue;

            if (now_ms >= s.next_hold_fx_ms) {
                s.next_hold_fx_ms = now_ms + hold_fx_interval_ms;

                auto& n = *s.note;
                if (n.line_id >= 0 &&
                    n.line_id < static_cast<int>(lines.size())) {
                    auto ls = eval_line_state(lines[n.line_id], t);
                    auto pos = note_world_pos_cs(ls.x, ls.y, ls.cos_rot, ls.sin_rot,
                                                 ls.scroll, n, n.scroll_hit,
                                                 false, 1.0, false, true);  // hold_keep_head=true → line position
                    math::RGB color = n.tint_hitfx_rgb.value_or(default_hitfx_color);
                    add_hitfx(pos.x, pos.y, t, color, ls.rot);
                    add_particle_burst(pos.x, pos.y, t * 1000.0, 500.0, color);
                }
            }
        }
    }
};

} // namespace phigros::engine
