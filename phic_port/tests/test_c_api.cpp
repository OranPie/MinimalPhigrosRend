#include "phic_c_api.h"

#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

int main() {
    assert(phic_abi_version() >= 5);

    phic_render_config_t cfg{};
    cfg.width = 1280;
    cfg.height = 720;
    cfg.approach_sec = 3.0;
    cfg.note_speed = 1.0;
    cfg.autoplay = 1;
    cfg.mod_lane_count = 8;

    phic_engine_t* engine = phic_engine_create(&cfg);
    assert(engine != nullptr);

    const std::string chart_json = R"JSON({
        "name": "api-v2-test",
        "judgeLineList": [
            {
                "bpm": 120.0,
                "notesAbove": [
                    {"time": 64.0, "lane": 1, "type": 1},
                    {"time": 128.0, "lane": 2, "type": 3, "holdTime": 32.0}
                ]
            }
        ]
    })JSON";

    const int rc_load = phic_engine_load_chart(
        engine,
        reinterpret_cast<const unsigned char*>(chart_json.data()),
        chart_json.size(),
        "official"
    );
    assert(rc_load == PHIC_OK);

    std::vector<phic_frame_command_v2_t> commands(64);
    std::vector<phic_judge_event_v2_t> events(64);
    phic_engine_stats_t stats{};
    std::size_t command_count = 0;
    std::size_t event_count = 0;

    int rc = phic_engine_step_v2(
        engine,
        0.0,
        nullptr,
        0,
        commands.data(),
        commands.size(),
        &command_count,
        &stats,
        events.data(),
        events.size(),
        &event_count
    );
    assert(rc == PHIC_OK);
    assert(command_count > 0);
    assert(commands[0].t_hit_sec > 0.99 && commands[0].t_hit_sec < 1.01);
    assert(commands[0].hold_end_sec >= commands[0].t_hit_sec);

    rc = phic_engine_step_v2(
        engine,
        3.0,
        nullptr,
        0,
        commands.data(),
        commands.size(),
        &command_count,
        &stats,
        events.data(),
        events.size(),
        &event_count
    );
    assert(rc == PHIC_OK);
    assert(event_count >= 2);

    bool saw_hold = false;
    for (std::size_t i = 0; i < event_count; ++i) {
        if (events[i].note_kind == PHIC_NOTE_HOLD) {
            saw_hold = true;
            break;
        }
    }
    assert(saw_hold);

    phic_engine_destroy(engine);
    return 0;
}
