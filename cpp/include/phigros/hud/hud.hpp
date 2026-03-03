#pragma once
#include <algorithm>
#include <cmath>
#include <string>

namespace phigros::hud {

struct HudState {
    int score = 0;
    double accuracy = 0.0;
    int combo = 0;
    int max_combo = 0;
    double progress = 0.0;
    std::string title;
    std::string subtitle;
    std::string score_text = "0000000";
    std::string acc_text = "100.00%";
    bool show_combo = false; // only show when combo >= 3
    int total_notes = 0;
};

inline std::string format_score(int score) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%07d", std::max(0, std::min(score, 1000000)));
    return buf;
}

inline std::string format_accuracy(double ratio) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f%%", std::max(0.0, std::min(ratio * 100.0, 100.0)));
    return buf;
}

inline double progress_ratio(double t, double chart_end) {
    if (chart_end <= 1e-9) return 0.0;
    double r = t / chart_end;
    return std::max(0.0, std::min(r, 1.0));
}

inline void update_hud(HudState& hud, int score, double acc_ratio, int combo,
                       int max_combo, double t, double chart_end, int total_notes) {
    hud.score = score;
    hud.accuracy = acc_ratio;
    hud.combo = combo;
    hud.max_combo = max_combo;
    hud.progress = progress_ratio(t, chart_end);
    hud.score_text = format_score(score);
    hud.acc_text = format_accuracy(acc_ratio);
    hud.show_combo = combo >= 3;
    hud.total_notes = total_notes;
}

} // namespace phigros::hud
