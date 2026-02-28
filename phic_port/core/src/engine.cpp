#include "phic/core/engine.hpp"
#include "phic/core/mods.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>

namespace phic {

Engine::Engine(RenderConfig cfg) : cfg_(cfg) {}

void Engine::set_config(const RenderConfig& cfg) { cfg_ = cfg; }

const RenderConfig& Engine::config() const { return cfg_; }

void Engine::load_chart(ChartData chart) {
    apply_mods(chart, cfg_.mods);
    chart_ = std::move(chart);
    reset();
}

const ChartData& Engine::chart() const { return chart_; }

void Engine::reset() {
    stats_ = {};
    now_sec_ = 0.0;
    note_states_.assign(chart_.notes.size(), NoteState{});
    lane_note_indices_.clear();
    unresolved_cursor_ = 0;

    int max_lane = std::max(0, cfg_.mods.lane_count - 1);
    for (const auto& n : chart_.notes) {
        max_lane = std::max(max_lane, n.lane);
    }
    lane_note_indices_.assign(static_cast<std::size_t>(max_lane + 1), {});
    for (std::size_t i = 0; i < chart_.notes.size(); ++i) {
        const int lane = chart_.notes[i].lane;
        if (lane < 0) {
            continue;
        }
        const std::size_t lane_idx = static_cast<std::size_t>(lane);
        if (lane_idx >= lane_note_indices_.size()) {
            lane_note_indices_.resize(lane_idx + 1);
        }
        lane_note_indices_[lane_idx].push_back(i);
    }
    step_judge_events_.clear();
    step_judge_events_.reserve(std::min<std::size_t>(chart_.notes.size(), static_cast<std::size_t>(1024)));
}

void Engine::seek(double time_sec) {
    if (time_sec < 0.0) {
        time_sec = 0.0;
    }
    now_sec_ = time_sec;
    for (auto& st : note_states_) {
        st.judged = false;
        st.hit = false;
        st.miss = false;
    }
    stats_ = {};
    unresolved_cursor_ = 0;
    step_judge_events_.clear();
    process_misses();
}

JudgeKind Engine::grade_for_delta(double dt) const {
    const double adt = std::abs(dt);
    if (adt <= kPerfectWindowSec) {
        return JudgeKind::Perfect;
    }
    if (adt <= kGoodWindowSec) {
        return JudgeKind::Good;
    }
    if (adt <= kBadWindowSec) {
        return JudgeKind::Bad;
    }
    return JudgeKind::None;
}

void Engine::advance_unresolved_cursor() {
    while (unresolved_cursor_ < note_states_.size() && note_states_[unresolved_cursor_].judged) {
        ++unresolved_cursor_;
    }
}

std::size_t Engine::lower_bound_hit(double t) const {
    return static_cast<std::size_t>(
        std::lower_bound(chart_.notes.begin(), chart_.notes.end(), t, [](const RuntimeNote& n, double tt) {
            return n.t_hit < tt;
        }) - chart_.notes.begin()
    );
}

std::size_t Engine::upper_bound_hit(double t) const {
    return static_cast<std::size_t>(
        std::upper_bound(chart_.notes.begin(), chart_.notes.end(), t, [](double tt, const RuntimeNote& n) {
            return tt < n.t_hit;
        }) - chart_.notes.begin()
    );
}

void Engine::apply_judge(std::size_t note_idx, JudgeKind kind, JudgeSource source, double event_time) {
    if (note_idx >= note_states_.size()) {
        return;
    }
    auto& st = note_states_[note_idx];
    if (st.judged || kind == JudgeKind::None) {
        return;
    }
    const auto& note = chart_.notes[note_idx];

    st.judged = true;
    if (kind == JudgeKind::Miss) {
        st.miss = true;
        stats_.combo = 0;
    } else {
        st.hit = true;
        stats_.hit_total += 1;
        if (kind == JudgeKind::Bad) {
            stats_.combo = 0;
        } else {
            stats_.combo += 1;
            stats_.max_combo = std::max(stats_.max_combo, stats_.combo);
        }
    }

    stats_.acc_sum += judge_weight(kind);
    stats_.judged_cnt += 1;

    JudgeEvent ev;
    ev.note_id = note.id;
    ev.lane = note.lane;
    ev.kind = kind;
    ev.source = source;
    ev.event_time = event_time;
    step_judge_events_.push_back(ev);
}

void Engine::process_autoplay() {
    if (!cfg_.autoplay) {
        return;
    }
    advance_unresolved_cursor();
    while (unresolved_cursor_ < note_states_.size()) {
        const auto& st = note_states_[unresolved_cursor_];
        if (st.judged) {
            ++unresolved_cursor_;
            continue;
        }
        if (now_sec_ >= chart_.notes[unresolved_cursor_].t_hit) {
            apply_judge(unresolved_cursor_, JudgeKind::Perfect, JudgeSource::Autoplay, now_sec_);
            ++unresolved_cursor_;
        } else {
            break;
        }
    }
}

void Engine::process_inputs(const std::vector<InputEvent>& input_events) {
    for (const auto& ev : input_events) {
        if (ev.type != InputEvent::Type::PointerDown) {
            continue;
        }

        if (ev.lane < 0) {
            continue;
        }
        const std::size_t lane_idx = static_cast<std::size_t>(ev.lane);
        if (lane_idx >= lane_note_indices_.size()) {
            continue;
        }
        const auto& lane_indices = lane_note_indices_[lane_idx];
        if (lane_indices.empty()) {
            continue;
        }

        std::size_t candidate_idx = static_cast<std::size_t>(-1);
        double best_abs_dt = 1e9;

        const double event_time = (ev.event_time > 0.0) ? ev.event_time : now_sec_;
        const double lo_t = event_time - kBadWindowSec;
        const double hi_t = event_time + kBadWindowSec;

        auto it = std::lower_bound(lane_indices.begin(), lane_indices.end(), lo_t, [&](std::size_t idx, double t) {
            return chart_.notes[idx].t_hit < t;
        });
        for (; it != lane_indices.end(); ++it) {
            const std::size_t idx = *it;
            const auto& note = chart_.notes[idx];
            if (note.t_hit > hi_t) {
                break;
            }
            if (note_states_[idx].judged) {
                continue;
            }
            const double dt = event_time - note.t_hit;
            const JudgeKind grade = grade_for_delta(dt);
            if (grade == JudgeKind::None) {
                if (note.t_hit > event_time + kBadWindowSec) {
                    break;
                }
                continue;
            }
            const double abs_dt = std::abs(dt);
            if (abs_dt < best_abs_dt) {
                best_abs_dt = abs_dt;
                candidate_idx = idx;
            }
        }

        if (candidate_idx != static_cast<std::size_t>(-1)) {
            const double dt = event_time - chart_.notes[candidate_idx].t_hit;
            apply_judge(candidate_idx, grade_for_delta(dt), JudgeSource::Input, event_time);
        }
    }
}

void Engine::process_misses() {
    advance_unresolved_cursor();
    while (unresolved_cursor_ < note_states_.size()) {
        const auto& st = note_states_[unresolved_cursor_];
        if (st.judged) {
            ++unresolved_cursor_;
            continue;
        }
        if (now_sec_ > chart_.notes[unresolved_cursor_].t_hit + kBadWindowSec) {
            apply_judge(unresolved_cursor_, JudgeKind::Miss, JudgeSource::TimeoutMiss, now_sec_);
            ++unresolved_cursor_;
        } else {
            break;
        }
    }
}

void Engine::build_frame_commands(std::vector<FrameCommand>& out_cmds) const {
    out_cmds.clear();

    const double approach = std::max(0.001, cfg_.approach_sec);
    const double overrender = std::max(1.0, cfg_.overrender);
    const double expand = std::max(1.0, cfg_.expand);
    const int lane_count = std::max(1, cfg_.mods.lane_count);
    const double flow = std::max(1e-6, cfg_.note_flow_speed_multiplier);
    const double min_t_hit = now_sec_ - (kBadWindowSec / flow);
    const double max_t_hit = now_sec_ + ((approach * overrender) / flow);
    const std::size_t lo = lower_bound_hit(min_t_hit);
    const std::size_t hi = upper_bound_hit(max_t_hit);
    out_cmds.reserve(hi > lo ? (hi - lo) : 0);

    for (std::size_t i = lo; i < hi; ++i) {
        const auto& st = note_states_[i];
        const auto& note = chart_.notes[i];
        if (st.judged) {
            continue;
        }

        const double dt = (note.t_hit - now_sec_) * flow;
        if (dt < -kBadWindowSec || dt > approach * overrender) {
            continue;
        }

        const double lane_pos = (static_cast<double>(note.lane) + 0.5) / static_cast<double>(lane_count);
        const double y_raw = 1.0 - (dt / approach);
        const double x_expanded = 0.5 + (lane_pos - 0.5) / expand;
        const double y_expanded = 0.5 + (y_raw - 0.5) / expand;
        const float x = static_cast<float>(x_expanded);
        const float y = static_cast<float>(y_expanded);

        out_cmds.emplace_back(FrameCommand{
            FrameCommand::Type::DrawNote,
            note.id,
            note.lane,
            note.kind,
            std::clamp(x, 0.0f, 1.0f),
            std::clamp(y, 0.0f, 1.0f),
            static_cast<float>(std::clamp(note.alpha01, 0.0, 1.0)),
            note.t_hit,
            note.hold_end,
        });
    }
}

Engine::StepResult Engine::step(double dt_sec, const std::vector<InputEvent>& input_events) {
    if (dt_sec < 0.0) {
        dt_sec = 0.0;
    }

    const double speed_mul = std::max(1e-6, cfg_.note_speed);
    now_sec_ += dt_sec * speed_mul;

    step_judge_events_.clear();
    process_autoplay();
    process_inputs(input_events);
    process_misses();

    StepResult result;
    result.time_sec = now_sec_;
    result.stats = stats_;
    build_frame_commands(result.frame_commands);
    result.judge_events = std::move(step_judge_events_);
    return result;
}

const std::string& Engine::last_error() const { return last_error_; }

}  // namespace phic
