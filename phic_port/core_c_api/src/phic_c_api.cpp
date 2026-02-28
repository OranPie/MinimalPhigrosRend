#include "phic_c_api.h"

#include "phic/core/engine.hpp"
#include "phic/core/parser.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

struct phic_engine {
    phic::Engine engine;
    std::string last_error;

    bool simulateplay = false;
    int simulateplay_mode = PHIC_SIM_CONSERVATIVE;
    int simulateplay_max_pointers = 2;

    std::size_t sim_cursor = 0;
    std::unordered_set<int> sim_fired;
    std::vector<double> sim_lane_cooldown_until;
    std::mt19937 sim_rng{12345};
    bool sim_seeded = false;
    double sim_now = 0.0;

    explicit phic_engine(const phic::RenderConfig& cfg) : engine(cfg) {}
};

namespace {

static_assert(offsetof(phic_render_config_t, width) == 0, "phic_render_config_t layout mismatch");
static_assert(offsetof(phic_render_config_t, height) == 4, "phic_render_config_t layout mismatch");
static_assert(offsetof(phic_render_config_t, approach_sec) == 8, "phic_render_config_t layout mismatch");
static_assert(offsetof(phic_render_config_t, note_speed) == 16, "phic_render_config_t layout mismatch");
static_assert(offsetof(phic_render_config_t, simulateplay) == 104, "phic_render_config_t layout mismatch");
static_assert(offsetof(phic_render_config_t, mod_seed) == 220, "phic_render_config_t layout mismatch");
static_assert(offsetof(phic_render_config_t, mod_lane_count) == 224, "phic_render_config_t layout mismatch");
static_assert(offsetof(phic_render_config_t, bgm_volume) == 232, "phic_render_config_t layout mismatch");
static_assert(offsetof(phic_render_config_t, rpe_easing_shift) == 316, "phic_render_config_t layout mismatch");
static_assert(sizeof(phic_render_config_t) == 320, "phic_render_config_t size mismatch");
static_assert(offsetof(phic_frame_command_v2_t, t_hit_sec) == 24, "phic_frame_command_v2_t layout mismatch");
static_assert(offsetof(phic_frame_command_v2_t, hold_end_sec) == 32, "phic_frame_command_v2_t layout mismatch");
static_assert(sizeof(phic_frame_command_v2_t) == 40, "phic_frame_command_v2_t size mismatch");
static_assert(offsetof(phic_judge_event_v2_t, note_kind) == 16, "phic_judge_event_v2_t layout mismatch");
static_assert(offsetof(phic_judge_event_v2_t, event_time) == 24, "phic_judge_event_v2_t layout mismatch");
static_assert(sizeof(phic_judge_event_v2_t) == 32, "phic_judge_event_v2_t size mismatch");

void reset_sim_state(phic_engine& e) {
    e.sim_cursor = 0;
    e.sim_fired.clear();
    e.sim_lane_cooldown_until.clear();
    e.sim_now = 0.0;
}

phic::RenderConfig from_c_config(const phic_render_config_t* cfg) {
    phic::RenderConfig out;
    if (cfg == nullptr) {
        return out;
    }
    out.width = cfg->width > 0 ? cfg->width : out.width;
    out.height = cfg->height > 0 ? cfg->height : out.height;
    out.approach_sec = cfg->approach_sec > 0.0 ? cfg->approach_sec : out.approach_sec;
    out.note_speed = cfg->note_speed > 0.0 ? cfg->note_speed : out.note_speed;
    out.autoplay = cfg->autoplay != 0;

    out.no_cull = cfg->no_cull != 0;
    out.no_cull_screen = cfg->no_cull_screen != 0;
    out.no_cull_enter_time = cfg->no_cull_enter_time != 0;
    out.note_outline = cfg->note_outline != 0;
    out.note_scale_x = cfg->note_scale_x > 0.0 ? cfg->note_scale_x : out.note_scale_x;
    out.note_scale_y = cfg->note_scale_y > 0.0 ? cfg->note_scale_y : out.note_scale_y;
    out.note_flow_speed_multiplier = cfg->note_flow_speed_multiplier > 0.0 ? cfg->note_flow_speed_multiplier : out.note_flow_speed_multiplier;
    out.expand = cfg->expand > 0.0 ? cfg->expand : out.expand;
    out.overrender = cfg->overrender > 0.0 ? cfg->overrender : out.overrender;
    out.trail_alpha = cfg->trail_alpha;
    out.trail_blur = cfg->trail_blur;
    out.trail_dim = cfg->trail_dim;
    out.bgm_volume = cfg->bgm_volume >= 0.0 ? cfg->bgm_volume : out.bgm_volume;
    out.hitfx_scale_mul = cfg->hitfx_scale_mul > 0.0 ? cfg->hitfx_scale_mul : out.hitfx_scale_mul;
    out.font_size_multiplier = cfg->font_size_multiplier > 0.0 ? cfg->font_size_multiplier : out.font_size_multiplier;
    out.hold_tail_tol = cfg->hold_tail_tol > 0.0 ? cfg->hold_tail_tol : out.hold_tail_tol;
    out.judge_width = cfg->judge_width > 0.0 ? cfg->judge_width : out.judge_width;
    out.judge_height = cfg->judge_height > 0.0 ? cfg->judge_height : out.judge_height;
    out.flick_threshold = cfg->flick_threshold > 0.0 ? cfg->flick_threshold : out.flick_threshold;
    out.bg_blur = cfg->bg_blur;
    out.bg_dim = cfg->bg_dim;
    out.hitsound_min_interval_ms = cfg->hitsound_min_interval_ms;
    out.hold_fx_interval_ms = cfg->hold_fx_interval_ms;
    out.multicolor_lines = cfg->multicolor_lines != 0;
    out.no_title_overlay = cfg->no_title_overlay != 0;
    out.advance_seq_overlay = cfg->advance_seq_overlay != 0;
    out.rpe_easing_shift = cfg->rpe_easing_shift;

    out.mods.mirror = cfg->mod_mirror != 0;
    out.mods.reverse_time = cfg->mod_reverse != 0;
    out.mods.randomize_lane = cfg->mod_randomize != 0;
    out.mods.hold_convert_tap = cfg->mod_hold_convert != 0;
    out.mods.transpose_sec = cfg->mod_transpose_sec;
    out.mods.stretch_factor = cfg->mod_stretch_factor > 0.0 ? cfg->mod_stretch_factor : out.mods.stretch_factor;
    out.mods.stretch_anchor_sec = cfg->mod_stretch_anchor_sec;
    out.mods.quantize = cfg->mod_quantize != 0;
    out.mods.quantize_step_sec = cfg->mod_quantize_step_sec > 0.0 ? cfg->mod_quantize_step_sec : out.mods.quantize_step_sec;
    out.mods.wave = cfg->mod_wave != 0;
    out.mods.wave_amplitude_lane = cfg->mod_wave_amplitude_lane;
    out.mods.wave_period_sec = cfg->mod_wave_period_sec > 0.0 ? cfg->mod_wave_period_sec : out.mods.wave_period_sec;
    out.mods.stutter = cfg->mod_stutter != 0;
    out.mods.stutter_repeat = cfg->mod_stutter_repeat > 0 ? cfg->mod_stutter_repeat : out.mods.stutter_repeat;
    out.mods.stutter_interval_sec = cfg->mod_stutter_interval_sec > 0.0 ? cfg->mod_stutter_interval_sec : out.mods.stutter_interval_sec;
    out.mods.thin_out_every = cfg->mod_thin_out_every > 0 ? cfg->mod_thin_out_every : out.mods.thin_out_every;
    out.mods.random_seed = cfg->mod_seed;
    out.mods.lane_count = cfg->mod_lane_count > 0 ? cfg->mod_lane_count : out.mods.lane_count;

    return out;
}

void fill_stats(const phic::Engine::StepResult& step, phic_engine_stats_t* out) {
    if (out == nullptr) {
        return;
    }
    out->combo = step.stats.combo;
    out->max_combo = step.stats.max_combo;
    out->judged_cnt = step.stats.judged_cnt;
    out->hit_total = step.stats.hit_total;
    out->acc_sum = step.stats.acc_sum;
    out->accuracy = step.stats.accuracy();
    out->time_sec = step.time_sec;
}

int copy_commands(
    const phic::Engine::StepResult& step,
    phic_frame_command_t* out_commands,
    size_t command_capacity,
    size_t* out_command_count
) {
    if (out_command_count == nullptr) {
        return PHIC_ERR_INVALID_ARGUMENT;
    }

    const size_t needed = step.frame_commands.size();
    *out_command_count = needed;
    if (out_commands == nullptr || command_capacity < needed) {
        return needed == 0 ? PHIC_OK : PHIC_ERR_BUFFER_TOO_SMALL;
    }

    for (size_t i = 0; i < needed; ++i) {
        const auto& src = step.frame_commands[i];
        auto& dst = out_commands[i];
        dst.note_id = src.note_id;
        dst.lane = src.lane;
        dst.kind = static_cast<int>(src.kind);
        dst.x = src.x;
        dst.y = src.y;
        dst.alpha = src.alpha;
    }

    return PHIC_OK;
}

int copy_judge_events(
    const phic::Engine::StepResult& step,
    phic_judge_event_t* out_events,
    size_t event_capacity,
    size_t* out_event_count
) {
    if (out_event_count == nullptr) {
        return PHIC_ERR_INVALID_ARGUMENT;
    }

    const size_t needed = step.judge_events.size();
    *out_event_count = needed;
    if (out_events == nullptr || event_capacity < needed) {
        return needed == 0 ? PHIC_OK : PHIC_ERR_BUFFER_TOO_SMALL;
    }

    for (size_t i = 0; i < needed; ++i) {
        const auto& src = step.judge_events[i];
        auto& dst = out_events[i];
        dst.note_id = src.note_id;
        dst.lane = src.lane;
        dst.kind = static_cast<int>(src.kind);
        dst.source = static_cast<int>(src.source);
        dst.event_time = src.event_time;
    }

    return PHIC_OK;
}

int copy_commands_v2(
    const phic::Engine::StepResult& step,
    phic_frame_command_v2_t* out_commands,
    size_t command_capacity,
    size_t* out_command_count
) {
    if (out_command_count == nullptr) {
        return PHIC_ERR_INVALID_ARGUMENT;
    }

    const size_t needed = step.frame_commands.size();
    *out_command_count = needed;
    if (out_commands == nullptr || command_capacity < needed) {
        return needed == 0 ? PHIC_OK : PHIC_ERR_BUFFER_TOO_SMALL;
    }

    for (size_t i = 0; i < needed; ++i) {
        const auto& src = step.frame_commands[i];
        auto& dst = out_commands[i];
        dst.note_id = src.note_id;
        dst.lane = src.lane;
        dst.kind = static_cast<int>(src.kind);
        dst.x = src.x;
        dst.y = src.y;
        dst.alpha = src.alpha;
        dst.t_hit_sec = src.t_hit_sec;
        dst.hold_end_sec = src.hold_end_sec;
    }

    return PHIC_OK;
}

int copy_judge_events_v2(
    const phic::Engine::StepResult& step,
    const phic::Engine& engine,
    phic_judge_event_v2_t* out_events,
    size_t event_capacity,
    size_t* out_event_count
) {
    if (out_event_count == nullptr) {
        return PHIC_ERR_INVALID_ARGUMENT;
    }

    const auto& chart = engine.chart();
    const size_t needed = step.judge_events.size();
    *out_event_count = needed;
    if (out_events == nullptr || event_capacity < needed) {
        return needed == 0 ? PHIC_OK : PHIC_ERR_BUFFER_TOO_SMALL;
    }

    for (size_t i = 0; i < needed; ++i) {
        const auto& src = step.judge_events[i];
        auto& dst = out_events[i];
        dst.note_id = src.note_id;
        dst.lane = src.lane;
        dst.kind = static_cast<int>(src.kind);
        dst.source = static_cast<int>(src.source);
        dst.note_kind = static_cast<int>(phic::NoteKind::Tap);
        if (src.note_id > 0) {
            const size_t idx = static_cast<size_t>(src.note_id - 1);
            if (idx < chart.notes.size()) {
                dst.note_kind = static_cast<int>(chart.notes[idx].kind);
            }
        }
        dst.event_time = src.event_time;
    }

    return PHIC_OK;
}

std::vector<phic::InputEvent> generate_auto_inputs(phic_engine& e, double dt_sec) {
    std::vector<phic::InputEvent> out;
    if (!e.simulateplay) {
        return out;
    }

    const auto& chart = e.engine.chart();
    const auto& notes = chart.notes;
    if (notes.empty()) {
        return out;
    }

    const int mode = std::clamp(e.simulateplay_mode, static_cast<int>(PHIC_SIM_CONSERVATIVE), static_cast<int>(PHIC_SIM_EXTREME));
    const double lookahead = (mode == PHIC_SIM_EXTREME) ? 0.090 : (mode == PHIC_SIM_AGGRESSIVE ? 0.055 : 0.025);
    const double lane_cooldown = (mode == PHIC_SIM_EXTREME) ? 0.0 : (mode == PHIC_SIM_AGGRESSIVE ? 0.010 : 0.018);
    const double jitter_abs = (mode == PHIC_SIM_EXTREME) ? 0.010 : (mode == PHIC_SIM_AGGRESSIVE ? 0.004 : 0.0);
    const int max_ptr = std::max(1, e.simulateplay_max_pointers);

    const int lane_count = std::max(1, e.engine.config().mods.lane_count);
    if (e.sim_lane_cooldown_until.size() != static_cast<size_t>(lane_count)) {
        e.sim_lane_cooldown_until.assign(static_cast<size_t>(lane_count), -1e9);
    }
    if (!e.sim_seeded) {
        e.sim_seeded = true;
        e.sim_rng.seed(static_cast<std::mt19937::result_type>(e.engine.config().mods.random_seed == 0 ? 12345 : e.engine.config().mods.random_seed));
    }

    const double speed_mul = std::max(1e-6, e.engine.config().note_speed);
    const double now = e.sim_now + std::max(0.0, dt_sec) * speed_mul;

    while (e.sim_cursor < notes.size() && notes[e.sim_cursor].t_hit < now - 0.200) {
        ++e.sim_cursor;
    }

    std::uniform_real_distribution<double> jitter(-jitter_abs, jitter_abs);

    for (size_t i = e.sim_cursor; i < notes.size(); ++i) {
        const auto& n = notes[i];
        if (n.t_hit > now + lookahead) {
            break;
        }
        if (e.sim_fired.find(n.id) != e.sim_fired.end()) {
            continue;
        }

        const int lane = std::clamp(n.lane, 0, lane_count - 1);
        if (e.sim_lane_cooldown_until[static_cast<size_t>(lane)] > now) {
            continue;
        }
        if (static_cast<int>(out.size()) >= max_ptr) {
            break;
        }

        phic::InputEvent ev;
        ev.type = phic::InputEvent::Type::PointerDown;
        ev.lane = lane;
        ev.event_time = std::max(now - 0.001, n.t_hit + jitter(e.sim_rng));
        out.push_back(ev);

        e.sim_fired.insert(n.id);
        e.sim_lane_cooldown_until[static_cast<size_t>(lane)] = now + lane_cooldown;
    }

    while (e.sim_cursor < notes.size() && e.sim_fired.find(notes[e.sim_cursor].id) != e.sim_fired.end()) {
        ++e.sim_cursor;
    }

    e.sim_now = now;
    return out;
}

}  // namespace

extern "C" {

int phic_abi_version(void) { return 5; }

phic_engine_t* phic_engine_create(const phic_render_config_t* cfg) {
    try {
        const auto rcfg = from_c_config(cfg);
        auto* out = new phic_engine(rcfg);
        if (cfg != nullptr) {
            out->simulateplay = cfg->simulateplay != 0;
            out->simulateplay_mode = cfg->simulateplay_mode;
            out->simulateplay_max_pointers = cfg->simulateplay_max_pointers > 0 ? cfg->simulateplay_max_pointers : 2;
        }
        return out;
    } catch (...) {
        return nullptr;
    }
}

void phic_engine_destroy(phic_engine_t* engine) {
    delete engine;
}

int phic_engine_load_chart(
    phic_engine_t* engine,
    const unsigned char* bytes,
    size_t byte_count,
    const char* format_hint
) {
    if (engine == nullptr || bytes == nullptr || byte_count == 0) {
        return PHIC_ERR_INVALID_ARGUMENT;
    }

    std::string payload(reinterpret_cast<const char*>(bytes), byte_count);
    const std::string hint = format_hint != nullptr ? std::string(format_hint) : std::string();
    const auto parsed = phic::parse_chart_bytes(payload, hint);
    if (!parsed.ok) {
        engine->last_error = parsed.error;
        return PHIC_ERR_PARSE;
    }

    engine->engine.load_chart(parsed.chart);
    engine->last_error.clear();
    reset_sim_state(*engine);
    return PHIC_OK;
}

int phic_engine_step(
    phic_engine_t* engine,
    double dt_sec,
    const phic_input_event_t* events,
    size_t event_count,
    phic_frame_command_t* out_commands,
    size_t command_capacity,
    size_t* out_command_count,
    phic_engine_stats_t* out_stats
) {
    size_t judge_count = 0;
    return phic_engine_step_ex(
        engine,
        dt_sec,
        events,
        event_count,
        out_commands,
        command_capacity,
        out_command_count,
        out_stats,
        nullptr,
        0,
        &judge_count
    );
}

int phic_engine_step_ex(
    phic_engine_t* engine,
    double dt_sec,
    const phic_input_event_t* events,
    size_t event_count,
    phic_frame_command_t* out_commands,
    size_t command_capacity,
    size_t* out_command_count,
    phic_engine_stats_t* out_stats,
    phic_judge_event_t* out_judge_events,
    size_t judge_event_capacity,
    size_t* out_judge_event_count
) {
    if (engine == nullptr || out_command_count == nullptr || out_judge_event_count == nullptr) {
        return PHIC_ERR_INVALID_ARGUMENT;
    }

    std::vector<phic::InputEvent> in;
    in.reserve(event_count);
    if (events != nullptr) {
        for (size_t i = 0; i < event_count; ++i) {
            phic::InputEvent ev;
            ev.type = phic::InputEvent::Type::PointerDown;
            ev.lane = events[i].lane;
            ev.event_time = events[i].event_time;
            in.push_back(ev);
        }
    }

    const auto step = engine->engine.step(dt_sec, in);
    engine->sim_now = step.time_sec;
    fill_stats(step, out_stats);
    const int rc_cmd = copy_commands(step, out_commands, command_capacity, out_command_count);
    const int rc_evt = copy_judge_events(step, out_judge_events, judge_event_capacity, out_judge_event_count);
    if (rc_cmd == PHIC_ERR_BUFFER_TOO_SMALL) return rc_cmd;
    if (rc_evt == PHIC_ERR_BUFFER_TOO_SMALL) return rc_evt;
    return (rc_cmd == PHIC_OK && rc_evt == PHIC_OK) ? PHIC_OK : PHIC_ERR_INTERNAL;
}

int phic_engine_step_auto(
    phic_engine_t* engine,
    double dt_sec,
    phic_frame_command_t* out_commands,
    size_t command_capacity,
    size_t* out_command_count,
    phic_engine_stats_t* out_stats
) {
    size_t judge_count = 0;
    return phic_engine_step_auto_ex(
        engine,
        dt_sec,
        out_commands,
        command_capacity,
        out_command_count,
        out_stats,
        nullptr,
        0,
        &judge_count
    );
}

int phic_engine_step_auto_ex(
    phic_engine_t* engine,
    double dt_sec,
    phic_frame_command_t* out_commands,
    size_t command_capacity,
    size_t* out_command_count,
    phic_engine_stats_t* out_stats,
    phic_judge_event_t* out_judge_events,
    size_t judge_event_capacity,
    size_t* out_judge_event_count
) {
    if (engine == nullptr || out_command_count == nullptr || out_judge_event_count == nullptr) {
        return PHIC_ERR_INVALID_ARGUMENT;
    }

    const auto in = generate_auto_inputs(*engine, dt_sec);
    const auto step = engine->engine.step(dt_sec, in);
    engine->sim_now = step.time_sec;
    fill_stats(step, out_stats);
    const int rc_cmd = copy_commands(step, out_commands, command_capacity, out_command_count);
    const int rc_evt = copy_judge_events(step, out_judge_events, judge_event_capacity, out_judge_event_count);
    if (rc_cmd == PHIC_ERR_BUFFER_TOO_SMALL) return rc_cmd;
    if (rc_evt == PHIC_ERR_BUFFER_TOO_SMALL) return rc_evt;
    return (rc_cmd == PHIC_OK && rc_evt == PHIC_OK) ? PHIC_OK : PHIC_ERR_INTERNAL;
}

int phic_engine_step_v2(
    phic_engine_t* engine,
    double dt_sec,
    const phic_input_event_t* events,
    size_t event_count,
    phic_frame_command_v2_t* out_commands,
    size_t command_capacity,
    size_t* out_command_count,
    phic_engine_stats_t* out_stats,
    phic_judge_event_v2_t* out_judge_events,
    size_t judge_event_capacity,
    size_t* out_judge_event_count
) {
    if (engine == nullptr || out_command_count == nullptr || out_judge_event_count == nullptr) {
        return PHIC_ERR_INVALID_ARGUMENT;
    }

    std::vector<phic::InputEvent> in;
    in.reserve(event_count);
    if (events != nullptr) {
        for (size_t i = 0; i < event_count; ++i) {
            phic::InputEvent ev;
            ev.type = phic::InputEvent::Type::PointerDown;
            ev.lane = events[i].lane;
            ev.event_time = events[i].event_time;
            in.push_back(ev);
        }
    }

    const auto step = engine->engine.step(dt_sec, in);
    engine->sim_now = step.time_sec;
    fill_stats(step, out_stats);
    const int rc_cmd = copy_commands_v2(step, out_commands, command_capacity, out_command_count);
    const int rc_evt = copy_judge_events_v2(step, engine->engine, out_judge_events, judge_event_capacity, out_judge_event_count);
    if (rc_cmd == PHIC_ERR_BUFFER_TOO_SMALL) return rc_cmd;
    if (rc_evt == PHIC_ERR_BUFFER_TOO_SMALL) return rc_evt;
    return (rc_cmd == PHIC_OK && rc_evt == PHIC_OK) ? PHIC_OK : PHIC_ERR_INTERNAL;
}

int phic_engine_step_auto_v2(
    phic_engine_t* engine,
    double dt_sec,
    phic_frame_command_v2_t* out_commands,
    size_t command_capacity,
    size_t* out_command_count,
    phic_engine_stats_t* out_stats,
    phic_judge_event_v2_t* out_judge_events,
    size_t judge_event_capacity,
    size_t* out_judge_event_count
) {
    if (engine == nullptr || out_command_count == nullptr || out_judge_event_count == nullptr) {
        return PHIC_ERR_INVALID_ARGUMENT;
    }

    const auto in = generate_auto_inputs(*engine, dt_sec);
    const auto step = engine->engine.step(dt_sec, in);
    engine->sim_now = step.time_sec;
    fill_stats(step, out_stats);
    const int rc_cmd = copy_commands_v2(step, out_commands, command_capacity, out_command_count);
    const int rc_evt = copy_judge_events_v2(step, engine->engine, out_judge_events, judge_event_capacity, out_judge_event_count);
    if (rc_cmd == PHIC_ERR_BUFFER_TOO_SMALL) return rc_cmd;
    if (rc_evt == PHIC_ERR_BUFFER_TOO_SMALL) return rc_evt;
    return (rc_cmd == PHIC_OK && rc_evt == PHIC_OK) ? PHIC_OK : PHIC_ERR_INTERNAL;
}

int phic_engine_seek(phic_engine_t* engine, double time_sec) {
    if (engine == nullptr) {
        return PHIC_ERR_INVALID_ARGUMENT;
    }
    engine->engine.seek(time_sec);
    reset_sim_state(*engine);
    engine->sim_now = std::max(0.0, time_sec);
    return PHIC_OK;
}

int phic_engine_reset(phic_engine_t* engine) {
    if (engine == nullptr) {
        return PHIC_ERR_INVALID_ARGUMENT;
    }
    engine->engine.reset();
    reset_sim_state(*engine);
    return PHIC_OK;
}

const char* phic_engine_last_error(const phic_engine_t* engine) {
    if (engine == nullptr) {
        return "engine is null";
    }
    return engine->last_error.c_str();
}

}  // extern "C"
