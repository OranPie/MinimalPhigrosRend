#pragma once
#include "phigros/core/logger.hpp"
#include <string>
#include <vector>
#include <cstdio>
#include <mutex>
#include <algorithm>

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
        PHLOG_TRACE(Audio, "HitsoundPool: loading " << ogg_data.size() << " bytes");
        ma_decoder_config dc = ma_decoder_config_init_default();
        for (int i = 0; i < POOL_SIZE; ++i) {
            if (ma_decoder_init_memory(ogg_data.data(), ogg_data.size(), &dc,
                                       &decoders[i]) != MA_SUCCESS) {
                PHLOG_TRACE(Audio, "HitsoundPool: decoder init failed at slot " << i);
                return false;
            }
            if (ma_sound_init_from_data_source(engine, &decoders[i],
                    MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                    &sounds[i]) != MA_SUCCESS) {
                ma_decoder_uninit(&decoders[i]);
                PHLOG_TRACE(Audio, "HitsoundPool: sound init failed at slot " << i);
                return false;
            }
        }
        loaded = true;
        PHLOG_TRACE(Audio, "HitsoundPool: ready with " << POOL_SIZE << " voices");
        return true;
    }

    void play() {
        if (!loaded) return;
        // Seek both the sound and its underlying decoder to the start
        ma_sound_seek_to_pcm_frame(&sounds[cursor], 0);
        ma_decoder_seek_to_pcm_frame(&decoders[cursor], 0);
        ma_sound_start(&sounds[cursor]);
        PHLOG_TRACE(Audio, "HitsoundPool: play slot=" << cursor);
        cursor = (cursor + 1) % POOL_SIZE;
    }

    void set_volume(float v) {
        for (int i = 0; i < POOL_SIZE; ++i)
            ma_sound_set_volume(&sounds[i], v);
    }

    void destroy() {
        if (!loaded) return;
        PHLOG_TRACE(Audio, "HitsoundPool: destroy");
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
    ma_decoder analysis_decoder{};
    bool engine_ok  = false;
    bool bgm_loaded = false;
    bool analysis_loaded = false;
    double offset_sec = 0.0;
    ma_uint32 analysis_sample_rate = 44100;
    ma_uint32 analysis_channels = 2;
    mutable std::mutex analysis_mu;

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
        PHLOG_INFO(Audio, "miniaudio engine initialised");
        return true;
    }

    bool load_bgm(const std::string& path, double offset = 0.0) {
        if (!engine_ok) {
            PHLOG_WARN(Audio, "load_bgm called before audio engine init");
            return false;
        }
        offset_sec = offset;
        if (ma_sound_init_from_file(&engine, path.c_str(), 0, nullptr, nullptr, &bgm) != MA_SUCCESS) {
            PHLOG_ERROR(Audio, "Failed to load BGM: " << path);
            return false;
        }
        ma_decoder_config analysis_cfg = ma_decoder_config_init(ma_format_f32, 2, 44100);
        if (ma_decoder_init_file(path.c_str(), &analysis_cfg, &analysis_decoder) == MA_SUCCESS) {
            ma_format fmt = ma_format_unknown;
            ma_uint32 channel_count = 0;
            ma_uint32 sample_rate = 0;
            if (ma_data_source_get_data_format(&analysis_decoder, &fmt, &channel_count,
                                               &sample_rate, nullptr, 0) == MA_SUCCESS) {
                analysis_channels = std::max<ma_uint32>(1, channel_count);
                analysis_sample_rate = std::max<ma_uint32>(1, sample_rate);
            }
            analysis_loaded = true;
        } else {
            PHLOG_WARN(Audio, "Failed to init analysis decoder for PCM taps: " << path);
        }
        bgm_loaded = true;
        PHLOG_INFO(Audio, "BGM loaded: " << path << " offset=" << offset << "s");
        return true;
    }

    // Load a hitsound OGG for note kind k (1–4). Call after init().
    bool load_hitsound(int k, const std::vector<uint8_t>& data) {
        if (!engine_ok || k < 1 || k > 4) {
            PHLOG_TRACE(Audio, "load_hitsound rejected kind=" << k
                << " engine_ok=" << engine_ok);
            return false;
        }
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
        if (k == 3 && hitsounds[1].loaded) {
            PHLOG_TRACE(Audio, "Hitsound fallback: hold->tap");
            hitsounds[1].play();
            return;
        } // hold→tap fallback
        PHLOG_TRACE(Audio, "Hitsound unavailable for kind=" << k);
    }

    void play() {
        if (bgm_loaded) {
            PHLOG_DEBUG(Audio, "Starting BGM playback");
            ma_sound_start(&bgm);
        }
    }

    void stop() {
        if (bgm_loaded) {
            PHLOG_DEBUG(Audio, "Stopping BGM playback");
            ma_sound_stop(&bgm);
        }
    }

    void seek(double t_sec) {
        if (!bgm_loaded) return;
        ma_uint32 sample_rate = ma_engine_get_sample_rate(&engine);
        ma_uint64 frame = static_cast<ma_uint64>((t_sec + offset_sec) * sample_rate);
        PHLOG_TRACE(Audio, "Seeking BGM to t=" << t_sec << "s frame=" << frame
            << " sample_rate=" << sample_rate);
        ma_sound_seek_to_pcm_frame(&bgm, frame);
    }

    double get_playback_time() const {
        if (!bgm_loaded) return 0.0;
        float cursor = 0.0f;
        ma_sound_get_cursor_in_seconds(const_cast<ma_sound*>(&bgm), &cursor);
        double t = static_cast<double>(cursor) - offset_sec;
        PHLOG_TRACE(Audio, "Playback time query=" << t);
        return t;
    }

    bool is_playing() const {
        if (!bgm_loaded) return false;
        return ma_sound_is_playing(const_cast<ma_sound*>(&bgm));
    }

    bool is_at_end() const {
        if (!bgm_loaded) return true;
        return ma_sound_at_end(const_cast<ma_sound*>(&bgm));
    }

    bool has_pcm_tap() const {
        return analysis_loaded;
    }

    std::vector<float> capture_recent_pcm_at_playback_time(double playback_time_sec,
                                                           size_t sample_count) const {
        std::vector<float> mono(sample_count, 0.0f);
        if (!analysis_loaded || sample_count == 0) return mono;

        std::lock_guard<std::mutex> lk(analysis_mu);
        const double source_t = std::max(0.0, playback_time_sec + offset_sec);
        const ma_uint64 center_frame = static_cast<ma_uint64>(
            source_t * static_cast<double>(analysis_sample_rate));
        const ma_uint64 frame_count = static_cast<ma_uint64>(sample_count);
        const ma_uint64 start_frame = (center_frame > frame_count) ? (center_frame - frame_count) : 0;
        if (ma_decoder_seek_to_pcm_frame(const_cast<ma_decoder*>(&analysis_decoder), start_frame) != MA_SUCCESS)
            return mono;

        std::vector<float> interleaved(sample_count * analysis_channels, 0.0f);
        ma_uint64 frames_read = 0;
        if (ma_decoder_read_pcm_frames(const_cast<ma_decoder*>(&analysis_decoder), interleaved.data(),
                                       frame_count, &frames_read) != MA_SUCCESS)
            return mono;

        for (size_t i = 0; i < static_cast<size_t>(frames_read); ++i) {
            float acc = 0.0f;
            for (ma_uint32 ch = 0; ch < analysis_channels; ++ch)
                acc += interleaved[i * analysis_channels + ch];
            mono[i] = acc / static_cast<float>(analysis_channels);
        }
        return mono;
    }

    std::vector<float> capture_recent_pcm(size_t sample_count) const {
        return capture_recent_pcm_at_playback_time(get_playback_time(), sample_count);
    }

    void destroy() {
        PHLOG_DEBUG(Audio, "AudioSystem destroy: bgm_loaded=" << bgm_loaded
            << " engine_ok=" << engine_ok);
        if (bgm_loaded) { ma_sound_uninit(&bgm); bgm_loaded = false; }
        if (analysis_loaded) {
            ma_decoder_uninit(&analysis_decoder);
            analysis_loaded = false;
        }
        for (int k = 1; k <= 4; ++k) hitsounds[k].destroy();
        if (engine_ok) { ma_engine_uninit(&engine); engine_ok = false; }
    }
};

} // namespace phigros::io
