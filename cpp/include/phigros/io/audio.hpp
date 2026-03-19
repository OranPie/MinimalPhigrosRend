#pragma once
#include "phigros/core/logger.hpp"
#include <string>
#include <vector>
#include <cstdio>

// Forward-declare miniaudio types (defined in audio_impl.cpp)
#include "miniaudio.h"

namespace phigros::io {

// Polyphonic hitsound pool: POOL_SIZE concurrent voices per sound type.
// Each slot owns an independent ma_decoder (initialised from the same in-memory
// OGG bytes) plus a ma_sound bound to that decoder.
struct HitsoundPool {
    static constexpr int POOL_SIZE = 6;

    std::vector<uint8_t> ogg_data;
    ma_decoder decoders[POOL_SIZE]{};
    ma_sound   sounds[POOL_SIZE]{};
    int  cursor = 0;
    bool loaded = false;

    bool load(ma_engine* engine, const std::vector<uint8_t>& data) {
        if (data.empty()) return false;
        ogg_data = data;
        ma_decoder_config dc = ma_decoder_config_init_default();
        for (int i = 0; i < POOL_SIZE; ++i) {
            if (ma_decoder_init_memory(ogg_data.data(), ogg_data.size(), &dc,
                                       &decoders[i]) != MA_SUCCESS)
                return false;
            if (ma_sound_init_from_data_source(engine, &decoders[i],
                    MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                    &sounds[i]) != MA_SUCCESS) {
                ma_decoder_uninit(&decoders[i]);
                return false;
            }
        }
        loaded = true;
        return true;
    }

    void play() {
        if (!loaded) return;
        // Seek both the sound and its underlying decoder to the start
        ma_sound_seek_to_pcm_frame(&sounds[cursor], 0);
        ma_decoder_seek_to_pcm_frame(&decoders[cursor], 0);
        ma_sound_start(&sounds[cursor]);
        cursor = (cursor + 1) % POOL_SIZE;
    }

    void set_volume(float v) {
        for (int i = 0; i < POOL_SIZE; ++i)
            ma_sound_set_volume(&sounds[i], v);
    }

    void destroy() {
        if (!loaded) return;
        for (int i = 0; i < POOL_SIZE; ++i) {
            ma_sound_uninit(&sounds[i]);
            ma_decoder_uninit(&decoders[i]);
        }
        loaded = false;
    }
};

struct AudioSystem {
    ma_engine engine{};
    ma_sound  bgm{};
    bool engine_ok  = false;
    bool bgm_loaded = false;
    double offset_sec = 0.0;

    // Hitsound pools indexed by note kind (1=tap, 2=drag, 3=hold, 4=flick).
    // Index 0 unused; holds fall back to tap sound if no dedicated pool loaded.
    HitsoundPool hitsounds[5]{};
    float hitsound_volume = 1.0f;

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

    // Load a hitsound OGG for note kind k (1–4). Call after init().
    bool load_hitsound(int k, const std::vector<uint8_t>& data) {
        if (!engine_ok || k < 1 || k > 4) return false;
        if (!hitsounds[k].load(&engine, data)) {
            PHLOG_WARN(Audio, "Failed to load hitsound for kind=" << k);
            return false;
        }
        hitsounds[k].set_volume(hitsound_volume);
        PHLOG_DEBUG(Audio, "Hitsound loaded: kind=" << k);
        return true;
    }

    // Play the hitsound matching note kind k (1=tap, 2=drag, 3=hold, 4=flick).
    // Holds fall back to tap if no hold-specific sound was loaded.
    void play_hitsound(int k) {
        if (k < 1 || k > 4) return;
        if (hitsounds[k].loaded)       { hitsounds[k].play(); return; }
        if (k == 3 && hitsounds[1].loaded) { hitsounds[1].play(); return; } // hold→tap fallback
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
        for (int k = 1; k <= 4; ++k) hitsounds[k].destroy();
        if (engine_ok) { ma_engine_uninit(&engine); engine_ok = false; }
    }
};

} // namespace phigros::io
