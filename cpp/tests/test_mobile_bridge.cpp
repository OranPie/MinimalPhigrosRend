#include "phigros/api/mobile_bridge.h"

#include <cmath>
#include <cstring>
#include <iostream>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { ++g_fail; std::cerr << "FAIL: " << (msg) << "\n"; } \
    else { ++g_pass; } \
} while (0)

int main() {
    phigros_mobile_config cfg{};
    cfg.window_width = 1280;
    cfg.window_height = 720;
    cfg.chart_speed = 1.0;
    cfg.note_scale = 2.5;

    phigros_mobile_handle* handle = phigros_mobile_create(&cfg);
    CHECK(handle != nullptr, "bridge handle created");
    if (!handle) return 1;

    CHECK(phigros_mobile_load_chart(handle, "charts/Aleph0.LeaF/IN.json", "") == 0,
          "chart load succeeds");

    phigros_mobile_state state{};
    CHECK(phigros_mobile_get_state(handle, &state) == 0, "state read succeeds");
    CHECK(state.chart_loaded == 1, "chart is marked as loaded");
    CHECK(state.line_count > 0, "line count is available");
    CHECK(state.playable_notes > 0, "playable note count is available");
    CHECK(std::fabs(state.chart_offset) < 1e-9, "chart offset is correct");

    const double seek_time = state.chart_offset + 5.0;
    CHECK(phigros_mobile_set_time(handle, seek_time) == 0, "seek succeeds");
    CHECK(phigros_mobile_get_state(handle, &state) == 0, "state read after seek");
    CHECK(state.chart_time >= seek_time - 1e-9, "chart time advanced");

    CHECK(phigros_mobile_on_touch(handle, 1, PHIGROS_MOBILE_TOUCH_BEGAN, 320.0f, 240.0f, 100) == 0,
          "touch begin succeeds");
    CHECK(phigros_mobile_get_state(handle, &state) == 0, "state read after touch");
    CHECK(state.active_touch_count == 1, "touch is tracked");

    CHECK(phigros_mobile_on_touch(handle, 1, PHIGROS_MOBILE_TOUCH_ENDED, 320.0f, 240.0f, 120) == 0,
          "touch end succeeds");
    CHECK(phigros_mobile_get_state(handle, &state) == 0, "state read after touch end");
    CHECK(state.active_touch_count == 0, "touch is cleared");

    CHECK(phigros_mobile_restart(handle) == 0, "restart succeeds");
    CHECK(phigros_mobile_get_state(handle, &state) == 0, "state read after restart");
    CHECK(std::fabs(state.chart_time - state.chart_offset) < 1e-9, "restart resets chart time");

    CHECK(phigros_mobile_load_chart(handle, "charts/does-not-exist.json", "") != 0,
          "invalid path fails");
    char error[256];
    CHECK(phigros_mobile_copy_last_error(handle, error, sizeof(error)) > 0,
          "error string is available");
    CHECK(std::strstr(error, "not found") != nullptr, "error mentions missing file");

    phigros_mobile_destroy(handle);
    std::cout << "mobile bridge: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
