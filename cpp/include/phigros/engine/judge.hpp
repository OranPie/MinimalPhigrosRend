#pragma once
#include "phigros/core/types.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace phigros::engine {

// Judge weight table
inline double judge_weight(const std::string& grade) {
    if (grade == "PERFECT") return 1.0;
    if (grade == "GOOD") return 0.6;
    return 0.0; // BAD, MISS
}

struct ScoreResult {
    int score;
    double acc_ratio;
    double combo_ratio;
};

inline ScoreResult compute_score(double acc_sum, int max_combo, int total_notes) {
    double acc_r = total_notes > 0 ? (acc_sum / total_notes) : 0.0;
    double combo_r = total_notes > 0 ? (static_cast<double>(max_combo) / total_notes) : 0.0;
    int sc = static_cast<int>(acc_r * 900000.0 + combo_r * 100000.0);
    return {sc, acc_r, combo_r};
}

class Judge {
public:
    static constexpr double PERFECT = 0.045;
    static constexpr double GOOD    = 0.090;
    static constexpr double BAD     = 0.150;

    int combo = 0;
    int max_combo = 0;
    double acc_sum = 0.0;
    int judged_cnt = 0;
    int hit_total = 0;

    void bump() {
        ++hit_total;
        ++combo;
        max_combo = std::max(max_combo, combo);
    }

    void break_combo() { combo = 0; }

    // Returns grade string or nullopt
    std::optional<std::string> try_hit(NoteState& ns, double t) {
        double dt = std::abs(t - ns.note->t_hit);
        if (dt <= PERFECT) {
            bump();
            ns.judged = true;
            ns.hit = true;
            ns.judge_t = t;
            ns.judge_delta_ms = (t - ns.note->t_hit) * 1000.0;
            ns.judge_grade = "PERFECT";
            acc_sum += judge_weight("PERFECT");
            ++judged_cnt;
            return "PERFECT";
        }
        if (dt <= GOOD) {
            bump();
            ns.judged = true;
            ns.hit = true;
            ns.judge_t = t;
            ns.judge_delta_ms = (t - ns.note->t_hit) * 1000.0;
            ns.judge_grade = "GOOD";
            acc_sum += judge_weight("GOOD");
            ++judged_cnt;
            return "GOOD";
        }
        if (dt <= BAD) {
            break_combo();
            ns.judged = true;
            ns.hit = true;
            ns.judge_t = t;
            ns.judge_delta_ms = (t - ns.note->t_hit) * 1000.0;
            ns.judge_grade = "BAD";
            acc_sum += judge_weight("BAD");
            ++judged_cnt;
            return "BAD";
        }
        return std::nullopt;
    }

    std::optional<std::string> grade_window(double t_note, double t) const {
        double dt = std::abs(t - t_note);
        if (dt <= PERFECT) return "PERFECT";
        if (dt <= GOOD) return "GOOD";
        if (dt <= BAD) return "BAD";
        return std::nullopt;
    }

    void mark_miss(NoteState& ns) {
        ns.judged = true;
        ns.miss = true;
        ns.judge_grade = "MISS";
        break_combo();
        acc_sum += judge_weight("MISS");
        ++judged_cnt;
    }

    // Start a hold (deferred judgment — scoring at finalize_hold)
    std::optional<std::string> start_hold(NoteState& ns, double t) {
        double dt = std::abs(t - ns.note->t_hit);
        std::string grade;
        if (dt <= PERFECT) grade = "PERFECT";
        else if (dt <= GOOD) grade = "GOOD";
        else if (dt <= BAD) grade = "BAD";
        else return std::nullopt;

        ns.hit = true;
        ns.holding = true;
        ns.hold_grade = grade;
        ns.judge_t = t;
        ns.judge_delta_ms = (t - ns.note->t_hit) * 1000.0;
        ns.judge_grade = grade;
        return grade;
    }

    // Finalize a hold note (called when hold ends or is released)
    void finalize_hold(NoteState& ns) {
        if (ns.hold_finalized) return;
        ns.hold_finalized = true;
        ns.judged = true;
        ns.holding = false;

        if (ns.hit && !ns.hold_failed) {
            ns.judge_grade = ns.hold_grade;
            acc_sum += judge_weight(ns.hold_grade);
            ++judged_cnt;
            if (ns.hold_grade == "BAD") break_combo();
            else bump();
        } else {
            ns.miss = true;
            ns.judge_grade = "MISS";
            break_combo();
            acc_sum += judge_weight("MISS");
            ++judged_cnt;
        }
    }
};

} // namespace phigros::engine
