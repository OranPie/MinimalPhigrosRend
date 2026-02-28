#pragma once

#include "phic/core/types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace phic {

class Engine {
public:
    explicit Engine(RenderConfig cfg = {});

    void set_config(const RenderConfig& cfg);
    const RenderConfig& config() const;

    void load_chart(ChartData chart);
    const ChartData& chart() const;

    void reset();
    void seek(double time_sec);

    struct StepResult {
        std::vector<FrameCommand> frame_commands;
        std::vector<JudgeEvent> judge_events;
        EngineStats stats;
        double time_sec = 0.0;
    };

    StepResult step(double dt_sec, const std::vector<InputEvent>& input_events);

    const std::string& last_error() const;

private:
    struct NoteState {
        bool judged = false;
        bool hit = false;
        bool miss = false;
    };

    JudgeKind grade_for_delta(double dt) const;
    void advance_unresolved_cursor();
    std::size_t lower_bound_hit(double t) const;
    std::size_t upper_bound_hit(double t) const;
    void apply_judge(std::size_t note_idx, JudgeKind kind, JudgeSource source, double event_time);
    void process_autoplay();
    void process_inputs(const std::vector<InputEvent>& input_events);
    void process_misses();
    void build_frame_commands(std::vector<FrameCommand>& out_cmds) const;

    RenderConfig cfg_{};
    ChartData chart_{};
    std::vector<NoteState> note_states_{};
    std::vector<std::vector<std::size_t>> lane_note_indices_{};
    EngineStats stats_{};
    std::vector<JudgeEvent> step_judge_events_{};
    std::size_t unresolved_cursor_ = 0;
    double now_sec_ = 0.0;
    std::string last_error_{};
};

}  // namespace phic
