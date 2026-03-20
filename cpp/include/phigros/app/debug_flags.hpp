#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace phigros::app {

enum class DebugFlag : uint64_t {
    NONE                     = 0,
    JUDGE_LINE_INFO_WINDOW   = 1ull << 0,
    JUDGE_LINE_INFO_ABOVE_LINE = 1ull << 1,
    JUDGE_LINE_NUMBER        = 1ull << 2,
    NOTE_LINE_NUMBER         = 1ull << 3,
    NOTE_INFO                = 1ull << 4,
    NOTE_JUDGE_WINDOW        = 1ull << 5,
    FRAME_TIME               = 1ull << 6,
    AUDIO_INFO               = 1ull << 7,
    LINE_GEOMETRY            = 1ull << 8,
    MIRROR_STATUS            = 1ull << 9,
    LINE_INFO_COLOR_MAPPING  = 1ull << 10,
    NOTE_HITBOX              = 1ull << 11,
    SPEED_VISUALIZATION      = 1ull << 12,
    PERFORMANCE_PROFILER     = 1ull << 13,
    FRAME_TIME_GRAPH         = 1ull << 14,
    NOTE_TRAIL               = 1ull << 15,
    TOUCH_VISUALIZATION      = 1ull << 16,
    VELOCITY_VECTORS         = 1ull << 17,
    COMBO_ZONES              = 1ull << 18,
    TIMING_WINDOWS           = 1ull << 19,
    AUDIO_WAVEFORM           = 1ull << 20,
    AUDIO_SPECTRUM           = 1ull << 21,
    SCORE_BREAKDOWN          = 1ull << 22,
    CHART_METADATA           = 1ull << 23,
    HOLD_STATE               = 1ull << 24,
    MISS_INDICATOR           = 1ull << 25,
    LINE_ALPHA_BAR           = 1ull << 26,
    NOTE_DENSITY_GRAPH       = 1ull << 27,
    SCROLL_SPEED_OVERLAY     = 1ull << 28,
    EXPAND_BORDER            = 1ull << 29,
    JUDGMENT_HISTORY         = 1ull << 30,
    CENTER_CROSSHAIR         = 1ull << 31,
    SIMULTANEOUS_INDICATOR   = 1ull << 32,
    NOTE_APPROACH_GUIDE      = 1ull << 33,
};

inline DebugFlag operator|(DebugFlag a, DebugFlag b) {
    return static_cast<DebugFlag>(
        static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

inline DebugFlag operator&(DebugFlag a, DebugFlag b) {
    return static_cast<DebugFlag>(
        static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
}

inline DebugFlag& operator|=(DebugFlag& a, DebugFlag b) {
    a = a | b;
    return a;
}

inline bool has_flag(DebugFlag flags, DebugFlag flag) {
    return (static_cast<uint64_t>(flags & flag) != 0);
}

inline std::string normalize_debug_flag_token(std::string token) {
    for (char& ch : token) {
        if (ch == '-' || ch == ' ' || ch == '.') ch = '_';
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return token;
}

inline const std::vector<std::pair<const char*, DebugFlag>>& debug_flag_table() {
    static const std::vector<std::pair<const char*, DebugFlag>> kFlags = {
        {"JUDGE_LINE_INFO_WINDOW", DebugFlag::JUDGE_LINE_INFO_WINDOW},
        {"JUDGE_LINE_INFO_ABOVE_LINE", DebugFlag::JUDGE_LINE_INFO_ABOVE_LINE},
        {"JUDGE_LINE_NUMBER", DebugFlag::JUDGE_LINE_NUMBER},
        {"NOTE_LINE_NUMBER", DebugFlag::NOTE_LINE_NUMBER},
        {"NOTE_INFO", DebugFlag::NOTE_INFO},
        {"NOTE_JUDGE_WINDOW", DebugFlag::NOTE_JUDGE_WINDOW},
        {"FRAME_TIME", DebugFlag::FRAME_TIME},
        {"AUDIO_INFO", DebugFlag::AUDIO_INFO},
        {"LINE_GEOMETRY", DebugFlag::LINE_GEOMETRY},
        {"MIRROR_STATUS", DebugFlag::MIRROR_STATUS},
        {"LINE_INFO_COLOR_MAPPING", DebugFlag::LINE_INFO_COLOR_MAPPING},
        {"NOTE_HITBOX", DebugFlag::NOTE_HITBOX},
        {"SPEED_VISUALIZATION", DebugFlag::SPEED_VISUALIZATION},
        {"PERFORMANCE_PROFILER", DebugFlag::PERFORMANCE_PROFILER},
        {"FRAME_TIME_GRAPH", DebugFlag::FRAME_TIME_GRAPH},
        {"NOTE_TRAIL", DebugFlag::NOTE_TRAIL},
        {"TOUCH_VISUALIZATION", DebugFlag::TOUCH_VISUALIZATION},
        {"VELOCITY_VECTORS", DebugFlag::VELOCITY_VECTORS},
        {"COMBO_ZONES", DebugFlag::COMBO_ZONES},
        {"TIMING_WINDOWS", DebugFlag::TIMING_WINDOWS},
        {"AUDIO_WAVEFORM", DebugFlag::AUDIO_WAVEFORM},
        {"AUDIO_SPECTRUM", DebugFlag::AUDIO_SPECTRUM},
        {"SCORE_BREAKDOWN", DebugFlag::SCORE_BREAKDOWN},
        {"CHART_METADATA", DebugFlag::CHART_METADATA},
        {"HOLD_STATE", DebugFlag::HOLD_STATE},
        {"MISS_INDICATOR", DebugFlag::MISS_INDICATOR},
        {"LINE_ALPHA_BAR", DebugFlag::LINE_ALPHA_BAR},
        {"NOTE_DENSITY_GRAPH", DebugFlag::NOTE_DENSITY_GRAPH},
        {"SCROLL_SPEED_OVERLAY", DebugFlag::SCROLL_SPEED_OVERLAY},
        {"EXPAND_BORDER", DebugFlag::EXPAND_BORDER},
        {"JUDGMENT_HISTORY", DebugFlag::JUDGMENT_HISTORY},
        {"CENTER_CROSSHAIR", DebugFlag::CENTER_CROSSHAIR},
        {"SIMULTANEOUS_INDICATOR", DebugFlag::SIMULTANEOUS_INDICATOR},
        {"NOTE_APPROACH_GUIDE", DebugFlag::NOTE_APPROACH_GUIDE},
    };
    return kFlags;
}

inline DebugFlag all_debug_flags() {
    DebugFlag flags = DebugFlag::NONE;
    for (const auto& [_, flag] : debug_flag_table())
        flags |= flag;
    return flags;
}

inline bool parse_debug_flags(const std::string& raw,
                              DebugFlag& out,
                              std::string* error = nullptr) {
    if (raw.empty()) {
        out = DebugFlag::NONE;
        return true;
    }

    char* end = nullptr;
    unsigned long long numeric = std::strtoull(raw.c_str(), &end, 0);
    if (end != nullptr && *end == '\0') {
        out = static_cast<DebugFlag>(numeric);
        return true;
    }

    out = DebugFlag::NONE;
    size_t start = 0;
    while (start < raw.size()) {
        size_t stop = raw.find_first_of("|,+", start);
        std::string token = raw.substr(start, stop == std::string::npos ? std::string::npos : stop - start);
        token.erase(std::remove_if(token.begin(), token.end(),
                                   [](unsigned char ch) { return std::isspace(ch) != 0; }),
                    token.end());
        token = normalize_debug_flag_token(token);
        if (!token.empty()) {
            if (token == "ALL") {
                out = all_debug_flags();
                if (stop == std::string::npos) break;
                start = stop + 1;
                continue;
            }
            bool matched = false;
            for (const auto& [name, flag] : debug_flag_table()) {
                if (token == name) {
                    out |= flag;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                if (error) *error = "Unknown debug flag: " + token;
                return false;
            }
        }
        if (stop == std::string::npos) break;
        start = stop + 1;
    }

    return true;
}

} // namespace phigros::app
