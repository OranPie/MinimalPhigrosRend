#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace phic {

enum class NoteKind : uint8_t {
    Tap = 1,
    Drag = 2,
    Hold = 3,
    Flick = 4,
};

enum class JudgeKind : uint8_t {
    None = 0,
    Perfect = 1,
    Good = 2,
    Bad = 3,
    Miss = 4,
};

enum class JudgeSource : uint8_t {
    Input = 1,
    Autoplay = 2,
    TimeoutMiss = 3,
};

struct RuntimeLine {
    int id = 0;
};

struct RuntimeNote {
    int id = 0;
    int line_id = 0;
    int lane = 0;
    bool above = true;
    bool fake = false;
    double t_hit = 0.0;
    double hold_end = 0.0;
    double speed_mul = 1.0;
    double alpha01 = 1.0;
    NoteKind kind = NoteKind::Tap;
};

struct ChartData {
    std::string title;
    std::vector<RuntimeLine> lines;
    std::vector<RuntimeNote> notes;
};

struct ModConfig {
    enum class SideMode : uint8_t {
        Keep = 0,
        ForceAbove = 1,
        ForceBelow = 2,
        Flip = 3,
    };

    enum class FadeMode : uint8_t {
        Time = 0,
        Constant = 1,
        Linear = 2,
    };

    struct NoteFilter {
        bool active = false;
        std::vector<int> line_ids{};
        std::vector<NoteKind> kinds{};
        std::vector<NoteKind> exclude_kinds{};
        bool has_above = false;
        bool above = true;
        bool has_fake = false;
        bool fake = false;
        bool has_t_hit_min = false;
        double t_hit_min = 0.0;
        bool has_t_hit_max = false;
        double t_hit_max = 0.0;
        bool has_t_end_min = false;
        double t_end_min = 0.0;
        bool has_t_end_max = false;
        double t_end_max = 0.0;
    };

    struct NoteSet {
        bool has_kind = false;
        NoteKind kind = NoteKind::Tap;
        bool has_speed_mul = false;
        double speed_mul = 1.0;
        bool has_alpha = false;
        double alpha01 = 1.0;
        bool has_side = false;
        SideMode side_mode = SideMode::Keep;
    };

    struct NoteRule {
        NoteFilter filter{};
        NoteSet set{};
        bool apply_to_hold = true;
    };

    bool full_blue = false;
    bool full_blue_convert_non_hold_to_tap = true;

    bool mirror = false;
    bool reverse_time = false;
    bool randomize_lane = false;
    bool hold_convert_tap = false;

    double lane_scale = 1.0;
    double lane_scale_center = -1.0;

    double transpose_sec = 0.0;
    double stretch_factor = 1.0;
    double stretch_anchor_sec = 0.0;

    bool quantize = false;
    double quantize_step_sec = 0.05;

    bool wave = false;
    double wave_amplitude_lane = 1.0;
    double wave_period_sec = 1.0;

    bool stutter = false;
    int stutter_repeat = 2;
    double stutter_interval_sec = 0.02;
    double stutter_alpha_decay = 0.8;
    int compress_zip_count = 1;
    bool attach_enable = false;
    NoteKind attach_kind = NoteKind::Flick;
    int attach_lane_offset = 1;
    double attach_time_offset_sec = 0.0;
    bool attach_has_side = false;
    SideMode attach_side_mode = SideMode::Keep;
    NoteFilter attach_filter{};

    bool fade_enable = false;
    FadeMode fade_mode = FadeMode::Time;
    bool fade_has_time_start = false;
    double fade_time_start = 0.0;
    bool fade_has_time_end = false;
    double fade_time_end = 0.0;
    double fade_alpha_start = 0.0;
    double fade_alpha_end = 1.0;
    double fade_alpha_min = 0.0;
    double fade_alpha_max = 1.0;
    double fade_constant_alpha = 0.5;
    NoteFilter fade_filter{};

    std::vector<NoteRule> note_rules{};
    bool note_overrides_enable = false;
    bool note_overrides_apply_to_hold = true;
    NoteSet note_overrides_set{};

    int thin_out_every = 1;
    int random_seed = 0;
    int lane_count = 8;
};

struct EngineStats {
    int combo = 0;
    int max_combo = 0;
    int judged_cnt = 0;
    int hit_total = 0;
    double acc_sum = 0.0;

    double accuracy() const {
        return judged_cnt > 0 ? (acc_sum / static_cast<double>(judged_cnt)) : 0.0;
    }
};

struct RenderConfig {
    int width = 1280;
    int height = 720;
    double approach_sec = 3.0;
    double note_speed = 1.0;
    bool autoplay = false;

    bool no_cull = false;
    bool no_cull_screen = false;
    bool no_cull_enter_time = false;
    bool note_outline = false;
    double note_scale_x = 1.0;
    double note_scale_y = 1.0;
    double note_flow_speed_multiplier = 1.0;
    double expand = 1.0;
    double overrender = 2.0;
    double trail_alpha = 0.0;
    int trail_blur = 0;
    int trail_dim = 0;

    int bg_blur = 10;
    int bg_dim = 120;
    double bgm_volume = 0.8;
    int hitsound_min_interval_ms = 30;
    double hitfx_scale_mul = 1.0;
    bool multicolor_lines = false;
    std::string line_alpha_affects_notes = "negative_only";
    bool no_title_overlay = false;
    bool advance_seq_overlay = false;
    std::string font_path;
    double font_size_multiplier = 1.0;
    int hold_fx_interval_ms = 200;
    double hold_tail_tol = 0.8;
    double judge_width = 0.12;
    double judge_height = 0.06;
    double flick_threshold = 0.02;
    int rpe_easing_shift = 0;
    std::string lang;
    bool quiet = false;
    bool no_color = false;
    bool basic_debug = false;
    bool debug_line_label = false;
    bool debug_line_stats = false;
    bool debug_judge_windows = false;
    bool debug_pointer = false;
    bool debug_note_info = false;
    bool debug_particles = false;
    bool hit_debug = false;

    ModConfig mods{};
};

struct InputEvent {
    enum class Type : uint8_t {
        PointerDown = 1,
    };

    Type type = Type::PointerDown;
    int lane = 0;
    double event_time = 0.0;
};

struct FrameCommand {
    enum class Type : uint8_t {
        DrawNote = 1,
    };

    Type type = Type::DrawNote;
    int note_id = 0;
    int lane = 0;
    NoteKind kind = NoteKind::Tap;
    float x = 0.0f;
    float y = 0.0f;
    float alpha = 1.0f;
    double t_hit_sec = 0.0;
    double hold_end_sec = 0.0;
};

struct JudgeEvent {
    int note_id = 0;
    int lane = 0;
    JudgeKind kind = JudgeKind::None;
    JudgeSource source = JudgeSource::Input;
    double event_time = 0.0;
};

constexpr double kPerfectWindowSec = 0.045;
constexpr double kGoodWindowSec = 0.090;
constexpr double kBadWindowSec = 0.150;

constexpr double judge_weight(JudgeKind kind) {
    switch (kind) {
        case JudgeKind::Perfect:
            return 1.0;
        case JudgeKind::Good:
            return 0.6;
        case JudgeKind::Bad:
        case JudgeKind::Miss:
        case JudgeKind::None:
            return 0.0;
    }
    return 0.0;
}

}  // namespace phic
