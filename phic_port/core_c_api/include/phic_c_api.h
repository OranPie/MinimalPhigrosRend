#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phic_engine phic_engine_t;

typedef enum phic_error_code {
    PHIC_OK = 0,
    PHIC_ERR_INVALID_ARGUMENT = 1,
    PHIC_ERR_PARSE = 2,
    PHIC_ERR_BUFFER_TOO_SMALL = 3,
    PHIC_ERR_INTERNAL = 255,
} phic_error_code_t;

typedef enum phic_note_kind {
    PHIC_NOTE_TAP = 1,
    PHIC_NOTE_DRAG = 2,
    PHIC_NOTE_HOLD = 3,
    PHIC_NOTE_FLICK = 4,
} phic_note_kind_t;

typedef enum phic_judge_kind {
    PHIC_JUDGE_NONE = 0,
    PHIC_JUDGE_PERFECT = 1,
    PHIC_JUDGE_GOOD = 2,
    PHIC_JUDGE_BAD = 3,
    PHIC_JUDGE_MISS = 4,
} phic_judge_kind_t;

typedef enum phic_judge_source {
    PHIC_JUDGE_SRC_INPUT = 1,
    PHIC_JUDGE_SRC_AUTOPLAY = 2,
    PHIC_JUDGE_SRC_TIMEOUT_MISS = 3,
} phic_judge_source_t;

typedef enum phic_simulateplay_mode {
    PHIC_SIM_CONSERVATIVE = 0,
    PHIC_SIM_AGGRESSIVE = 1,
    PHIC_SIM_EXTREME = 2,
} phic_simulateplay_mode_t;

typedef struct phic_render_config {
    int width;
    int height;
    double approach_sec;
    double note_speed;
    int autoplay;

    int no_cull;
    int no_cull_screen;
    int no_cull_enter_time;
    int note_outline;
    double note_scale_x;
    double note_scale_y;
    double note_flow_speed_multiplier;
    double expand;
    double overrender;
    double trail_alpha;
    int trail_blur;
    int trail_dim;

    int simulateplay;
    int simulateplay_mode;
    int simulateplay_max_pointers;

    int mod_mirror;
    int mod_reverse;
    int mod_randomize;
    int mod_hold_convert;
    double mod_transpose_sec;
    double mod_stretch_factor;
    double mod_stretch_anchor_sec;
    int mod_quantize;
    double mod_quantize_step_sec;
    int mod_wave;
    double mod_wave_amplitude_lane;
    double mod_wave_period_sec;
    int mod_stutter;
    int mod_stutter_repeat;
    double mod_stutter_interval_sec;
    int mod_thin_out_every;
    int mod_seed;
    int mod_lane_count;

    double bgm_volume;
    double hitfx_scale_mul;
    double font_size_multiplier;
    double hold_tail_tol;
    double judge_width;
    double judge_height;
    double flick_threshold;
    int bg_blur;
    int bg_dim;
    int hitsound_min_interval_ms;
    int hold_fx_interval_ms;
    int multicolor_lines;
    int no_title_overlay;
    int advance_seq_overlay;
    int rpe_easing_shift;
} phic_render_config_t;

typedef struct phic_input_event {
    int lane;
    double event_time;
} phic_input_event_t;

typedef struct phic_frame_command {
    int note_id;
    int lane;
    int kind;
    float x;
    float y;
    float alpha;
} phic_frame_command_t;

typedef struct phic_frame_command_v2 {
    int note_id;
    int lane;
    int kind;
    float x;
    float y;
    float alpha;
    double t_hit_sec;
    double hold_end_sec;
} phic_frame_command_v2_t;

typedef struct phic_engine_stats {
    int combo;
    int max_combo;
    int judged_cnt;
    int hit_total;
    double acc_sum;
    double accuracy;
    double time_sec;
} phic_engine_stats_t;

typedef struct phic_judge_event {
    int note_id;
    int lane;
    int kind;
    int source;
    double event_time;
} phic_judge_event_t;

typedef struct phic_judge_event_v2 {
    int note_id;
    int lane;
    int kind;
    int source;
    int note_kind;
    double event_time;
} phic_judge_event_v2_t;

int phic_abi_version(void);

phic_engine_t* phic_engine_create(const phic_render_config_t* cfg);
void phic_engine_destroy(phic_engine_t* engine);

int phic_engine_load_chart(
    phic_engine_t* engine,
    const unsigned char* bytes,
    size_t byte_count,
    const char* format_hint
);

int phic_engine_step(
    phic_engine_t* engine,
    double dt_sec,
    const phic_input_event_t* events,
    size_t event_count,
    phic_frame_command_t* out_commands,
    size_t command_capacity,
    size_t* out_command_count,
    phic_engine_stats_t* out_stats
);

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
);

int phic_engine_step_auto(
    phic_engine_t* engine,
    double dt_sec,
    phic_frame_command_t* out_commands,
    size_t command_capacity,
    size_t* out_command_count,
    phic_engine_stats_t* out_stats
);

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
);

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
);

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
);

int phic_engine_seek(phic_engine_t* engine, double time_sec);
int phic_engine_reset(phic_engine_t* engine);

const char* phic_engine_last_error(const phic_engine_t* engine);

#ifdef __cplusplus
}
#endif
