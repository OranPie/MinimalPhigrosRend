#include "phigros/api/mobile_bridge.h"

#ifdef __ANDROID__

#include <jni.h>
#include <string>
#include <cmath>

namespace {

phigros_mobile_handle* g_bridge = nullptr;

std::string to_std_string(JNIEnv* env, jstring value) {
    if (!value) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) return {};
    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

jstring last_error(JNIEnv* env) {
    if (!g_bridge) return env->NewStringUTF("Bridge not initialized");
    char buffer[512];
    const int copied = phigros_mobile_copy_last_error(g_bridge, buffer, sizeof(buffer));
    if (copied <= 0) return env->NewStringUTF("");
    return env->NewStringUTF(buffer);
}

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_org_phigros_renderer_NativeBridge_initialize(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jint width,
    jint height) {
    if (g_bridge) phigros_mobile_destroy(g_bridge);
    phigros_mobile_config config{};
    config.window_width = width;
    config.window_height = height;
    config.chart_speed = 1.0;
    config.note_scale = 2.5;
    g_bridge = phigros_mobile_create(&config);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_phigros_renderer_NativeBridge_loadChart(
    JNIEnv* env,
    jobject /* thiz */,
    jstring path) {
    if (!g_bridge) return JNI_FALSE;
    const std::string native_path = to_std_string(env, path);
    return phigros_mobile_load_chart(g_bridge, native_path.c_str(), "") == 0 ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_phigros_renderer_NativeBridge_setTime(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jdouble time_seconds) {
    if (g_bridge) phigros_mobile_set_time(g_bridge, time_seconds);
}

extern "C" JNIEXPORT void JNICALL
Java_org_phigros_renderer_NativeBridge_restart(
    JNIEnv* /* env */,
    jobject /* thiz */) {
    if (g_bridge) phigros_mobile_restart(g_bridge);
}

extern "C" JNIEXPORT void JNICALL
Java_org_phigros_renderer_NativeBridge_setPlayMode(
    JNIEnv* env,
    jobject /* thiz */,
    jstring mode) {
    if (!g_bridge) return;
    const std::string m = to_std_string(env, mode);
    phigros_mobile_set_play_mode(g_bridge, m.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_org_phigros_renderer_NativeBridge_loadScript(
    JNIEnv* env,
    jobject /* thiz */,
    jstring path) {
    if (!g_bridge) return;
    const std::string p = to_std_string(env, path);
    phigros_mobile_load_script(g_bridge, p.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_org_phigros_renderer_NativeBridge_setPaused(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jboolean paused) {
    if (g_bridge) phigros_mobile_set_paused(g_bridge, paused ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL
Java_org_phigros_renderer_NativeBridge_onTouch(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jint pointer_id,
    jint phase,
    jfloat x,
    jfloat y,
    jlong timestamp_ms) {
    if (!g_bridge) return;
    phigros_mobile_on_touch(g_bridge, pointer_id,
        static_cast<phigros_mobile_touch_phase>(phase),
        x, y, static_cast<int64_t>(timestamp_ms));
}

extern "C" JNIEXPORT void JNICALL
Java_org_phigros_renderer_NativeBridge_tick(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jdouble dt_seconds) {
    if (g_bridge) phigros_mobile_tick(g_bridge, dt_seconds);
}

// Fill pre-allocated Kotlin arrays from the most-recent frame snapshot.
//
// lineData  : FloatArray[MAX_LINES * 7]  — per line: x y rot alpha r g b (r/g/b normalised 0-1)
// lineCount : IntArray[1]                — filled with actual line count
// noteData  : FloatArray[MAX_NOTES * 12] — per note: wx wy wx2 wy2 alpha lineRot sizePx r g b kindF flagsF
//                                          flags (int cast to float): bit0=isHold bit1=judged bit2=miss
//                                                                      bit3=holding bit4=drawHoldHead bit5=holdFailed
// noteCount : IntArray[1]                — filled with actual note count
// hudData   : FloatArray[9]              — combo maxCombo score accuracy progress showCombo chartTime chartEnded totalNotes
extern "C" JNIEXPORT void JNICALL
Java_org_phigros_renderer_NativeBridge_fillFrameArrays(
    JNIEnv* env,
    jobject /* thiz */,
    jfloatArray lineData,
    jintArray   lineCount,
    jfloatArray noteData,
    jintArray   noteCount,
    jfloatArray hudData) {

    if (!g_bridge) return;

    phigros_frame_data frame{};
    if (phigros_mobile_get_frame(g_bridge, &frame) != 0) return;

    // Lines
    {
        jfloat* ld = env->GetFloatArrayElements(lineData, nullptr);
        jint*   lc = env->GetIntArrayElements(lineCount, nullptr);
        if (ld && lc) {
            const int cnt = frame.line_count < PHIGROS_MAX_LINES ? frame.line_count : PHIGROS_MAX_LINES;
            lc[0] = cnt;
            for (int i = 0; i < cnt; ++i) {
                const phigros_line_data& l = frame.lines[i];
                const int base = i * 7;
                ld[base + 0] = l.x;
                ld[base + 1] = l.y;
                ld[base + 2] = l.rot;
                ld[base + 3] = l.alpha;
                ld[base + 4] = l.r / 255.0f;
                ld[base + 5] = l.g / 255.0f;
                ld[base + 6] = l.b / 255.0f;
            }
        }
        if (ld) env->ReleaseFloatArrayElements(lineData, ld, 0);
        if (lc) env->ReleaseIntArrayElements(lineCount, lc, 0);
    }

    // Notes
    {
        jfloat* nd = env->GetFloatArrayElements(noteData, nullptr);
        jint*   nc = env->GetIntArrayElements(noteCount, nullptr);
        if (nd && nc) {
            const int cnt = frame.note_count < PHIGROS_MAX_NOTES ? frame.note_count : PHIGROS_MAX_NOTES;
            nc[0] = cnt;
            for (int i = 0; i < cnt; ++i) {
                const phigros_note_data& n = frame.notes[i];
                const int base = i * 12;
                int flags = (n.is_hold       ? 1  : 0)
                          | (n.judged        ? 2  : 0)
                          | (n.miss          ? 4  : 0)
                          | (n.holding       ? 8  : 0)
                          | (n.draw_hold_head? 16 : 0)
                          | (n.hold_hit_failed?32 : 0)
                          | (n.is_mh        ? 64 : 0);
                nd[base +  0] = n.wx;
                nd[base +  1] = n.wy;
                nd[base +  2] = n.wx2;
                nd[base +  3] = n.wy2;
                nd[base +  4] = n.alpha;
                nd[base +  5] = n.line_rot;
                nd[base +  6] = n.size_px;
                nd[base +  7] = n.r / 255.0f;
                nd[base +  8] = n.g / 255.0f;
                nd[base +  9] = n.b / 255.0f;
                nd[base + 10] = static_cast<float>(n.kind);
                nd[base + 11] = static_cast<float>(flags);
            }
        }
        if (nd) env->ReleaseFloatArrayElements(noteData, nd, 0);
        if (nc) env->ReleaseIntArrayElements(noteCount, nc, 0);
    }

    // HUD [9]
    {
        jfloat* hd = env->GetFloatArrayElements(hudData, nullptr);
        if (hd) {
            const phigros_hud_data& h = frame.hud;
            hd[0] = static_cast<float>(h.combo);
            hd[1] = static_cast<float>(h.max_combo);
            hd[2] = static_cast<float>(h.score);
            hd[3] = h.accuracy;
            hd[4] = h.progress;
            hd[5] = h.show_combo ? 1.0f : 0.0f;
            hd[6] = static_cast<float>(frame.chart_time);
            hd[7] = frame.chart_ended ? 1.0f : 0.0f;
            hd[8] = static_cast<float>(h.total_notes);
        }
        if (hd) env->ReleaseFloatArrayElements(hudData, hd, 0);
    }
}

extern "C" JNIEXPORT jobject JNICALL
Java_org_phigros_renderer_NativeBridge_getState(
    JNIEnv* env,
    jobject /* thiz */) {
    jclass cls = env->FindClass("org/phigros/renderer/BridgeState");
    if (!cls) return nullptr;
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(IIIIIIIIDDDZ)V");
    if (!ctor) return nullptr;

    phigros_mobile_state state{};
    if (!g_bridge || phigros_mobile_get_state(g_bridge, &state) != 0) {
        return env->NewObject(cls, ctor, 0, 0, 0, 0, 0, 0, 0, 0, 0.0, 0.0, 0.0, JNI_TRUE);
    }

    return env->NewObject(cls, ctor,
                          state.chart_loaded,
                          state.line_count,
                          state.total_notes,
                          state.playable_notes,
                          state.visible_notes,
                          state.active_touch_count,
                          state.max_combo,
                          state.judged_notes,
                          state.chart_time,
                          state.chart_offset,
                          state.chart_duration,
                          state.paused ? JNI_TRUE : JNI_FALSE);
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_phigros_renderer_NativeBridge_getLastError(
    JNIEnv* env,
    jobject /* thiz */) {
    return last_error(env);
}

// Fill pre-allocated effect array from the most-recent frame snapshot.
// effectData : FloatArray[MAX_EFFECTS * 9] — per effect: x y t0 radiusStart radiusEnd r g b isGood
// effectCount: IntArray[1]                 — filled with actual effect count
extern "C" JNIEXPORT void JNICALL
Java_org_phigros_renderer_NativeBridge_fillEffects(
    JNIEnv* env,
    jobject /* thiz */,
    jfloatArray effectData,
    jintArray   effectCount) {
    if (!g_bridge) return;

    phigros_frame_data frame{};
    if (phigros_mobile_get_frame(g_bridge, &frame) != 0) return;

    jfloat* ed = env->GetFloatArrayElements(effectData, nullptr);
    jint*   ec = env->GetIntArrayElements(effectCount, nullptr);
    if (ed && ec) {
        const int cnt = frame.effect_count < PHIGROS_MAX_EFFECTS ? frame.effect_count : PHIGROS_MAX_EFFECTS;
        ec[0] = cnt;
        for (int i = 0; i < cnt; ++i) {
            const int base = i * 9;
            ed[base + 0] = frame.effects[i].x;
            ed[base + 1] = frame.effects[i].y;
            ed[base + 2] = frame.effects[i].t0;
            ed[base + 3] = frame.effects[i].radius_start;
            ed[base + 4] = frame.effects[i].radius_end;
            ed[base + 5] = frame.effects[i].r / 255.0f;
            ed[base + 6] = frame.effects[i].g / 255.0f;
            ed[base + 7] = frame.effects[i].b / 255.0f;
            ed[base + 8] = static_cast<float>(frame.effects[i].is_good);
        }
    }
    if (ed) env->ReleaseFloatArrayElements(effectData, ed, 0);
    if (ec) env->ReleaseIntArrayElements(effectCount, ec, 0);
}

#endif

extern "C" JNIEXPORT jint JNICALL
Java_org_phigros_renderer_NativeBridge_extractChartZip(
    JNIEnv* env,
    jobject /* thiz */,
    jstring j_zip_path,
    jstring j_dest_dir) {
    const char* zip_path = env->GetStringUTFChars(j_zip_path, nullptr);
    const char* dest_dir = env->GetStringUTFChars(j_dest_dir, nullptr);
    jint result = phigros_extract_chart_zip(zip_path, dest_dir);
    env->ReleaseStringUTFChars(j_zip_path, zip_path);
    env->ReleaseStringUTFChars(j_dest_dir, dest_dir);
    return result;
}

#endif
