#pragma once
#include <cstdint>

namespace phigros::engine {

struct JudgeAction {
    int64_t id = 0;            // pointer slot id (>=0), or -(key_index+1) for keys
    bool has_position = false;  // true = touch/mouse, false = keyboard
    float x = 0, y = 0;        // screen pos (valid only if has_position)
    bool press = false;         // went down this frame
    bool release = false;       // went up this frame
    bool down = false;          // currently held
    bool flick = false;         // flick gesture (pointer only)
};

struct JudgeInputFrame {
    static constexpr int MAX_ACTIONS = 20;
    JudgeAction actions[MAX_ACTIONS];
    int count = 0;

    void add(const JudgeAction& a) {
        if (count < MAX_ACTIONS) actions[count++] = a;
    }
    void clear() { count = 0; }
};

} // namespace phigros::engine
