#pragma once
#include "phigros/core/logger.hpp"
#include <string>
#include <cstdio>

// Forward-declare miniaudio types (defined in audio_impl.cpp)
#include "miniaudio.h"

namespace phigros::io {

struct AudioSystem {
    ma_engine engine{};
    ma_sound  bgm{};
    bool engine_ok = false;
    bool bgm_loaded = false;
    double offset_sec = 0.0;

    bool init() {
        ma_engine_config cfg = ma_engine_config_init();
        if (ma_engine_init(&cfg, &engine) != MA_SUCCESS) {
            PHLOG_ERROR(Audio, "Failed to init miniaudio engine");
            return false;
        }
        engine_ok = true;
        PHLOG_DEBUG(Audio, "miniaudio engine initialised");
        return true;
    }

    bool load_bgm(const std::string& path, double offset = 0.0) {
        if (!engine_ok) return false;
        offset_sec = offset;
        if (ma_sound_init_from_file(&engine, path.c_str(), 0, nullptr, nullptr, &bgm) != MA_SUCCESS) {
            PHLOG_ERROR(Audio, "Failed to load BGM: " << path);
            return false;
        }
        bgm_loaded = true;
        PHLOG_DEBUG(Audio, "BGM loaded: " << path << " offset=" << offset << "s");
        return true;
    }

    void play() {
        if (bgm_loaded) ma_sound_start(&bgm);
    }

    void stop() {
        if (bgm_loaded) ma_sound_stop(&bgm);
    }

    void seek(double t_sec) {
        if (!bgm_loaded) return;
        ma_uint32 sample_rate = ma_engine_get_sample_rate(&engine);
        ma_uint64 frame = static_cast<ma_uint64>((t_sec + offset_sec) * sample_rate);
        ma_sound_seek_to_pcm_frame(&bgm, frame);
    }

    double get_playback_time() const {
        if (!bgm_loaded) return 0.0;
        float cursor = 0.0f;
        ma_sound_get_cursor_in_seconds(const_cast<ma_sound*>(&bgm), &cursor);
        return static_cast<double>(cursor) - offset_sec;
    }

    bool is_playing() const {
        if (!bgm_loaded) return false;
        return ma_sound_is_playing(const_cast<ma_sound*>(&bgm));
    }

    bool is_at_end() const {
        if (!bgm_loaded) return true;
        return ma_sound_at_end(const_cast<ma_sound*>(&bgm));
    }

    void destroy() {
        if (bgm_loaded) { ma_sound_uninit(&bgm); bgm_loaded = false; }
        if (engine_ok) { ma_engine_uninit(&engine); engine_ok = false; }
    }
};

} // namespace phigros::io
