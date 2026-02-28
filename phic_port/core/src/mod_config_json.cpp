#include "phic/core/mod_config_json.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace phic {

namespace {

bool to_bool(const nlohmann::json& v, bool fallback) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number_integer()) return v.get<int>() != 0;
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
        if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    }
    return fallback;
}

double to_num(const nlohmann::json& v, double fallback) {
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) {
        try {
            return std::stod(v.get<std::string>());
        } catch (...) {
        }
    }
    return fallback;
}

int to_int(const nlohmann::json& v, int fallback) {
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number()) return static_cast<int>(std::lround(v.get<double>()));
    if (v.is_string()) {
        try {
            return std::stoi(v.get<std::string>());
        } catch (...) {
        }
    }
    return fallback;
}

bool parse_bool(const nlohmann::json& j, const char* key, bool fallback) {
    const auto it = j.find(key);
    if (it == j.end()) return fallback;
    return to_bool(*it, fallback);
}

double parse_num(const nlohmann::json& j, const char* key, double fallback) {
    const auto it = j.find(key);
    if (it == j.end()) return fallback;
    return to_num(*it, fallback);
}

int parse_int(const nlohmann::json& j, const char* key, int fallback) {
    const auto it = j.find(key);
    if (it == j.end()) return fallback;
    return to_int(*it, fallback);
}

double parse_alpha01_val(const nlohmann::json& v, double fallback) {
    const double raw = to_num(v, fallback);
    if (!std::isfinite(raw)) {
        return fallback;
    }
    if (raw <= 1.000001) {
        return std::clamp(raw, 0.0, 1.0);
    }
    return std::clamp(raw / 255.0, 0.0, 1.0);
}

bool parse_note_kind_val(const nlohmann::json& v, NoteKind& out) {
    int k = 0;
    if (v.is_number()) {
        k = static_cast<int>(std::lround(v.get<double>()));
    } else if (v.is_string()) {
        std::string s = v.get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (s == "tap" || s == "click" || s == "note" || s == "n1") k = 1;
        else if (s == "drag" || s == "slide" || s == "n2") k = 2;
        else if (s == "hold" || s == "long" || s == "n3") k = 3;
        else if (s == "flick" || s == "flk" || s == "n4") k = 4;
        else {
            try {
                k = std::stoi(s);
            } catch (...) {
                return false;
            }
        }
    } else {
        return false;
    }
    if (k == 2) out = NoteKind::Drag;
    else if (k == 3) out = NoteKind::Hold;
    else if (k == 4) out = NoteKind::Flick;
    else out = NoteKind::Tap;
    return true;
}

bool parse_side_mode_val(const nlohmann::json& v, ModConfig::SideMode& out) {
    if (v.is_boolean()) {
        out = v.get<bool>() ? ModConfig::SideMode::ForceAbove : ModConfig::SideMode::ForceBelow;
        return true;
    }
    if (!v.is_string() && !v.is_number()) {
        return false;
    }
    std::string s = v.is_string() ? v.get<std::string>() : std::to_string(to_int(v, 0));
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (s == "above" || s == "up" || s == "top" || s == "1" || s == "true") {
        out = ModConfig::SideMode::ForceAbove;
        return true;
    }
    if (s == "below" || s == "down" || s == "bottom" || s == "0" || s == "false") {
        out = ModConfig::SideMode::ForceBelow;
        return true;
    }
    if (s == "flip" || s == "toggle" || s == "invert") {
        out = ModConfig::SideMode::Flip;
        return true;
    }
    return false;
}

void parse_note_kind_list(const nlohmann::json& v, std::vector<NoteKind>& out) {
    auto add_kind = [&](const nlohmann::json& item) {
        NoteKind kind = NoteKind::Tap;
        if (parse_note_kind_val(item, kind)) {
            out.push_back(kind);
        }
    };
    if (v.is_array()) {
        for (const auto& item : v) add_kind(item);
    } else {
        add_kind(v);
    }
}

void parse_int_list(const nlohmann::json& v, std::vector<int>& out) {
    auto add_val = [&](const nlohmann::json& item) {
        if (item.is_number() || item.is_string()) {
            out.push_back(to_int(item, 0));
        }
    };
    if (v.is_array()) {
        for (const auto& item : v) add_val(item);
    } else {
        add_val(v);
    }
}

void merge_note_filter_from_json(ModConfig::NoteFilter& filter, const nlohmann::json& obj) {
    if (!obj.is_object()) {
        return;
    }
    filter.active = true;

    if (const auto it = obj.find("line_ids"); it != obj.end()) parse_int_list(*it, filter.line_ids);
    else if (const auto it = obj.find("line_id"); it != obj.end()) parse_int_list(*it, filter.line_ids);

    if (const auto it = obj.find("kinds"); it != obj.end()) parse_note_kind_list(*it, filter.kinds);
    else if (const auto it = obj.find("kind"); it != obj.end()) parse_note_kind_list(*it, filter.kinds);

    if (const auto it = obj.find("not_kinds"); it != obj.end()) parse_note_kind_list(*it, filter.exclude_kinds);
    else if (const auto it = obj.find("exclude_kind"); it != obj.end()) parse_note_kind_list(*it, filter.exclude_kinds);
    else if (const auto it = obj.find("not_kind"); it != obj.end()) parse_note_kind_list(*it, filter.exclude_kinds);

    if (const auto it = obj.find("above"); it != obj.end()) {
        filter.has_above = true;
        filter.above = to_bool(*it, true);
    }
    if (const auto it = obj.find("fake"); it != obj.end()) {
        filter.has_fake = true;
        filter.fake = to_bool(*it, false);
    }
    if (const auto it = obj.find("t_hit_min"); it != obj.end()) {
        filter.has_t_hit_min = true;
        filter.t_hit_min = to_num(*it, 0.0);
    } else if (const auto it = obj.find("time_min"); it != obj.end()) {
        filter.has_t_hit_min = true;
        filter.t_hit_min = to_num(*it, 0.0);
    }
    if (const auto it = obj.find("t_hit_max"); it != obj.end()) {
        filter.has_t_hit_max = true;
        filter.t_hit_max = to_num(*it, 0.0);
    } else if (const auto it = obj.find("time_max"); it != obj.end()) {
        filter.has_t_hit_max = true;
        filter.t_hit_max = to_num(*it, 0.0);
    }
    if (const auto it = obj.find("t_end_min"); it != obj.end()) {
        filter.has_t_end_min = true;
        filter.t_end_min = to_num(*it, 0.0);
    }
    if (const auto it = obj.find("t_end_max"); it != obj.end()) {
        filter.has_t_end_max = true;
        filter.t_end_max = to_num(*it, 0.0);
    }
}

void parse_note_set_from_json(ModConfig::NoteSet& set, const nlohmann::json& obj) {
    if (!obj.is_object()) {
        return;
    }
    if (const auto it = obj.find("kind"); it != obj.end()) {
        NoteKind kind = NoteKind::Tap;
        if (parse_note_kind_val(*it, kind)) {
            set.has_kind = true;
            set.kind = kind;
        }
    }
    if (const auto it = obj.find("speed_mul"); it != obj.end()) {
        set.has_speed_mul = true;
        set.speed_mul = to_num(*it, set.speed_mul);
    }
    if (const auto it = obj.find("alpha"); it != obj.end()) {
        set.has_alpha = true;
        set.alpha01 = parse_alpha01_val(*it, set.alpha01);
    }
    const auto side_it = obj.find("side");
    const auto above_it = obj.find("above");
    if (side_it != obj.end()) {
        ModConfig::SideMode side_mode = ModConfig::SideMode::Keep;
        if (parse_side_mode_val(*side_it, side_mode)) {
            set.has_side = true;
            set.side_mode = side_mode;
        }
    } else if (above_it != obj.end()) {
        ModConfig::SideMode side_mode = ModConfig::SideMode::Keep;
        if (parse_side_mode_val(*above_it, side_mode)) {
            set.has_side = true;
            set.side_mode = side_mode;
        }
    }
}

}  // namespace

void merge_mod_config_from_json(ModConfig& mods, const nlohmann::json& m) {
    mods.note_rules.clear();
    mods.note_overrides_enable = false;
    mods.note_overrides_set = {};
    mods.note_overrides_apply_to_hold = true;
    mods.fade_enable = false;
    mods.fade_filter = {};
    mods.attach_enable = false;
    mods.attach_filter = {};

    mods.full_blue = parse_bool(m, "full_blue", mods.full_blue);
    mods.full_blue_convert_non_hold_to_tap =
        parse_bool(m, "full_blue_convert_non_hold_to_tap", mods.full_blue_convert_non_hold_to_tap);
    mods.mirror = parse_bool(m, "mirror", mods.mirror);
    mods.reverse_time = parse_bool(m, "reverse", mods.reverse_time);
    mods.randomize_lane = parse_bool(m, "randomize", mods.randomize_lane);
    mods.hold_convert_tap = parse_bool(m, "hold_convert", mods.hold_convert_tap);
    mods.lane_scale = parse_num(m, "lane_scale", mods.lane_scale);
    mods.lane_scale_center = parse_num(m, "lane_scale_center", mods.lane_scale_center);
    mods.transpose_sec = parse_num(m, "transpose", mods.transpose_sec);
    mods.stretch_factor = parse_num(m, "stretch", mods.stretch_factor);
    mods.stretch_anchor_sec = parse_num(m, "stretch_anchor", mods.stretch_anchor_sec);
    mods.quantize = parse_bool(m, "quantize", mods.quantize);
    mods.quantize_step_sec = parse_num(m, "quantize_step", mods.quantize_step_sec);
    mods.wave = parse_bool(m, "wave", mods.wave);
    mods.wave_amplitude_lane = parse_num(m, "wave_amp", mods.wave_amplitude_lane);
    mods.wave_period_sec = parse_num(m, "wave_period", mods.wave_period_sec);
    mods.stutter = parse_bool(m, "stutter", mods.stutter);
    mods.stutter_repeat = parse_int(m, "stutter_repeat", mods.stutter_repeat);
    mods.stutter_interval_sec = parse_num(m, "stutter_interval", mods.stutter_interval_sec);
    mods.stutter_alpha_decay = parse_num(m, "stutter_alpha_decay", mods.stutter_alpha_decay);
    mods.compress_zip_count = parse_int(m, "compress_zip_count", mods.compress_zip_count);
    mods.thin_out_every = parse_int(m, "thin_out_every", mods.thin_out_every);
    mods.random_seed = parse_int(m, "seed", mods.random_seed);
    mods.lane_count = parse_int(m, "lane_count", mods.lane_count);

    const auto it_full_blue = m.find("full_blue");
    if (it_full_blue != m.end() && it_full_blue->is_object()) {
        mods.full_blue = parse_bool(*it_full_blue, "enable", mods.full_blue);
        mods.full_blue_convert_non_hold_to_tap =
            parse_bool(*it_full_blue, "convert_non_hold_to_tap", mods.full_blue_convert_non_hold_to_tap);
    }
    const auto it_scale = m.find("scale");
    if (it_scale != m.end() && it_scale->is_object()) {
        const bool enabled = parse_bool(*it_scale, "enable", true);
        if (enabled) {
            mods.lane_scale = parse_num(*it_scale, "x", parse_num(*it_scale, "x_scale", mods.lane_scale));
            mods.lane_scale_center = parse_num(*it_scale, "x_center", mods.lane_scale_center);
        }
    }
    const auto it_transpose = m.find("transpose");
    if (it_transpose != m.end() && it_transpose->is_object()) {
        if (parse_bool(*it_transpose, "enable", true)) {
            mods.transpose_sec = parse_num(*it_transpose, "offset", parse_num(*it_transpose, "time_offset", mods.transpose_sec));
        }
    }
    const auto it_stretch = m.find("stretch");
    if (it_stretch != m.end() && it_stretch->is_object()) {
        if (parse_bool(*it_stretch, "enable", true)) {
            mods.stretch_factor = parse_num(*it_stretch, "factor", parse_num(*it_stretch, "multiplier", mods.stretch_factor));
            mods.stretch_anchor_sec = parse_num(*it_stretch, "anchor", parse_num(*it_stretch, "anchor_time", mods.stretch_anchor_sec));
        }
    }
    const auto it_quantize = m.find("quantize");
    if (it_quantize != m.end() && it_quantize->is_object()) {
        mods.quantize = parse_bool(*it_quantize, "enable", true);
        mods.quantize_step_sec = parse_num(*it_quantize, "time_grid", parse_num(*it_quantize, "time_step", mods.quantize_step_sec));
    }
    const auto it_stutter = m.find("stutter");
    if (it_stutter != m.end() && it_stutter->is_object()) {
        mods.stutter = parse_bool(*it_stutter, "enable", true);
        mods.stutter_repeat = parse_int(*it_stutter, "repeat", parse_int(*it_stutter, "count", mods.stutter_repeat));
        mods.stutter_interval_sec = parse_num(*it_stutter, "interval", parse_num(*it_stutter, "delay", parse_num(*it_stutter, "offset", mods.stutter_interval_sec)));
        mods.stutter_alpha_decay = parse_num(*it_stutter, "alpha_decay", parse_num(*it_stutter, "opacity_decay", mods.stutter_alpha_decay));
    }
    const auto it_wave = m.find("wave");
    if (it_wave != m.end() && it_wave->is_object()) {
        mods.wave = parse_bool(*it_wave, "enable", true);
        mods.wave_amplitude_lane = parse_num(*it_wave, "amplitude", parse_num(*it_wave, "amp", mods.wave_amplitude_lane));
        mods.wave_period_sec = parse_num(*it_wave, "period", mods.wave_period_sec);
    }
    const auto it_zip = m.find("compress_zip");
    if (it_zip != m.end() && it_zip->is_object()) {
        const bool enabled = parse_bool(*it_zip, "enable", true);
        if (enabled) {
            mods.compress_zip_count = parse_int(*it_zip, "count", mods.compress_zip_count);
        }
    }

    const auto find_first_obj = [&](std::initializer_list<const char*> keys) -> const nlohmann::json* {
        for (const char* key : keys) {
            const auto it = m.find(key);
            if (it != m.end() && it->is_object()) {
                return &(*it);
            }
        }
        return nullptr;
    };

    if (const auto* fade_obj = find_first_obj({"fade", "alpha", "opacity"})) {
        if (parse_bool(*fade_obj, "enable", true)) {
            mods.fade_enable = true;
            const std::string mode = fade_obj->contains("mode") && (*fade_obj)["mode"].is_string()
                ? (*fade_obj)["mode"].get<std::string>() : "time";
            std::string mode_lc = mode;
            std::transform(mode_lc.begin(), mode_lc.end(), mode_lc.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (mode_lc == "constant") {
                mods.fade_mode = ModConfig::FadeMode::Constant;
            } else if (mode_lc == "linear") {
                mods.fade_mode = ModConfig::FadeMode::Linear;
            } else {
                mods.fade_mode = ModConfig::FadeMode::Time;
            }
            const auto ts = fade_obj->find("time_start");
            const auto te = fade_obj->find("time_end");
            const auto fis = fade_obj->find("fade_in_start");
            const auto foe = fade_obj->find("fade_out_end");
            mods.fade_has_time_start = (ts != fade_obj->end()) || (fis != fade_obj->end());
            mods.fade_has_time_end = (te != fade_obj->end()) || (foe != fade_obj->end());
            if (mods.fade_has_time_start) {
                mods.fade_time_start = ts != fade_obj->end() ? to_num(*ts, 0.0) : to_num(*fis, 0.0);
            }
            if (mods.fade_has_time_end) {
                mods.fade_time_end = te != fade_obj->end() ? to_num(*te, 0.0) : to_num(*foe, 0.0);
            }
            mods.fade_alpha_start = parse_num(*fade_obj, "alpha_start", mods.fade_alpha_start);
            mods.fade_alpha_end = parse_num(*fade_obj, "alpha_end", mods.fade_alpha_end);
            mods.fade_alpha_min = parse_num(*fade_obj, "alpha_min", mods.fade_alpha_min);
            mods.fade_alpha_max = parse_num(*fade_obj, "alpha_max", mods.fade_alpha_max);
            mods.fade_constant_alpha = parse_num(*fade_obj, "constant_alpha", parse_num(*fade_obj, "alpha", mods.fade_constant_alpha));
            mods.fade_alpha_min = std::clamp(mods.fade_alpha_min, 0.0, 1.0);
            mods.fade_alpha_max = std::clamp(mods.fade_alpha_max, 0.0, 1.0);
            if (mods.fade_alpha_max < mods.fade_alpha_min) std::swap(mods.fade_alpha_min, mods.fade_alpha_max);
            if (const auto fit = fade_obj->find("filter"); fit != fade_obj->end() && fit->is_object()) {
                merge_note_filter_from_json(mods.fade_filter, *fit);
            } else if (const auto fit = fade_obj->find("match"); fit != fade_obj->end() && fit->is_object()) {
                merge_note_filter_from_json(mods.fade_filter, *fit);
            }
        }
    }

    if (const auto* attach_obj = find_first_obj({"attach", "attach_note", "add_note"})) {
        if (parse_bool(*attach_obj, "enable", true)) {
            mods.attach_enable = true;
            NoteKind kind = mods.attach_kind;
            if (const auto it = attach_obj->find("kind"); it != attach_obj->end()) {
                parse_note_kind_val(*it, kind);
            } else if (const auto it = attach_obj->find("attach_kind"); it != attach_obj->end()) {
                parse_note_kind_val(*it, kind);
            }
            mods.attach_kind = kind;
            if (const auto it = attach_obj->find("lane_offset"); it != attach_obj->end()) {
                mods.attach_lane_offset = to_int(*it, mods.attach_lane_offset);
            } else if (const auto it = attach_obj->find("attach_lane_offset"); it != attach_obj->end()) {
                mods.attach_lane_offset = to_int(*it, mods.attach_lane_offset);
            } else {
                const auto xit = attach_obj->find("x_offset");
                const auto oxit = attach_obj->find("offset_x");
                if (xit != attach_obj->end() || oxit != attach_obj->end()) {
                    const double x = xit != attach_obj->end() ? to_num(*xit, 0.0) : to_num(*oxit, 0.0);
                    mods.attach_lane_offset = static_cast<int>(std::lround(x / 100.0));
                }
            }
            mods.attach_time_offset_sec =
                parse_num(*attach_obj, "time_offset", parse_num(*attach_obj, "t_offset", mods.attach_time_offset_sec));

            mods.attach_has_side = false;
            if (const auto it = attach_obj->find("side"); it != attach_obj->end()) {
                ModConfig::SideMode side_mode = ModConfig::SideMode::Keep;
                if (parse_side_mode_val(*it, side_mode)) {
                    mods.attach_has_side = true;
                    mods.attach_side_mode = side_mode;
                }
            } else if (const auto it = attach_obj->find("above"); it != attach_obj->end()) {
                ModConfig::SideMode side_mode = ModConfig::SideMode::Keep;
                if (parse_side_mode_val(*it, side_mode)) {
                    mods.attach_has_side = true;
                    mods.attach_side_mode = side_mode;
                }
            }

            if (const auto fit = attach_obj->find("filter"); fit != attach_obj->end() && fit->is_object()) {
                merge_note_filter_from_json(mods.attach_filter, *fit);
            } else if (const auto fit = attach_obj->find("match"); fit != attach_obj->end() && fit->is_object()) {
                merge_note_filter_from_json(mods.attach_filter, *fit);
            }
        }
    }

    const auto parse_note_rules = [&](const nlohmann::json& node) {
        if (!node.is_array()) {
            return;
        }
        for (const auto& rule_j : node) {
            if (!rule_j.is_object()) {
                continue;
            }
            const auto flt_it = rule_j.find("filter");
            const auto when_it = rule_j.find("when");
            const auto set_it = rule_j.find("set");
            const auto then_it = rule_j.find("then");
            const nlohmann::json* flt = (flt_it != rule_j.end() && flt_it->is_object()) ? &(*flt_it)
                : (when_it != rule_j.end() && when_it->is_object()) ? &(*when_it) : nullptr;
            const nlohmann::json* st = (set_it != rule_j.end() && set_it->is_object()) ? &(*set_it)
                : (then_it != rule_j.end() && then_it->is_object()) ? &(*then_it) : nullptr;
            if (flt == nullptr || st == nullptr) {
                continue;
            }
            ModConfig::NoteRule rule{};
            merge_note_filter_from_json(rule.filter, *flt);
            parse_note_set_from_json(rule.set, *st);
            rule.apply_to_hold = parse_bool(rule_j, "apply_to_hold", true);
            mods.note_rules.push_back(rule);
        }
    };

    if (const auto it = m.find("note_rules"); it != m.end()) {
        if (it->is_array()) {
            parse_note_rules(*it);
        } else if (it->is_object()) {
            if (const auto rit = it->find("rules"); rit != it->end()) {
                parse_note_rules(*rit);
            }
        }
    } else if (const auto it = m.find("rules"); it != m.end()) {
        parse_note_rules(*it);
    }

    if (const auto it = m.find("note_overrides"); it != m.end() && it->is_object() && !it->empty()) {
        mods.note_overrides_enable = true;
        mods.note_overrides_apply_to_hold = parse_bool(*it, "apply_to_hold", true);
        if (const auto sit = it->find("set"); sit != it->end() && sit->is_object()) {
            parse_note_set_from_json(mods.note_overrides_set, *sit);
        } else {
            parse_note_set_from_json(mods.note_overrides_set, *it);
        }
    }
}

}  // namespace phic
