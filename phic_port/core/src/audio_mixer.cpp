#include "phic/core/audio_mixer.hpp"

// stb_vorbis for OGG decoding
#include "stb_vorbis.c"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace phic {

namespace {

// ============================================================
// Decode OGG/Vorbis file → interleaved float samples
// Returns false on failure. out_rate set to actual sample rate.
// ============================================================

bool decode_ogg(const std::string& path, std::vector<float>& out_samples,
                int& out_rate, int& out_channels) {
    int error = 0;
    stb_vorbis* v = stb_vorbis_open_filename(path.c_str(), &error, nullptr);
    if (!v) return false;

    stb_vorbis_info info = stb_vorbis_get_info(v);
    out_rate = info.sample_rate;
    out_channels = info.channels;

    const int ch = info.channels;
    out_samples.clear();

    constexpr int kBlock = 4096;
    std::vector<float> tmp(static_cast<std::size_t>(kBlock) * ch);
    int got = 0;
    while ((got = stb_vorbis_get_samples_float_interleaved(v, ch, tmp.data(), kBlock * ch)) > 0) {
        out_samples.insert(out_samples.end(), tmp.begin(), tmp.begin() + got * ch);
    }
    stb_vorbis_close(v);
    return !out_samples.empty();
}

// ============================================================
// Resample: simple linear interpolation
// in_rate → out_rate, stereo (or mono→mono)
// ============================================================

std::vector<float> resample(const std::vector<float>& src, int in_channels,
                             int in_rate, int out_rate) {
    if (in_rate == out_rate) return src;
    const std::size_t in_frames = src.size() / static_cast<std::size_t>(in_channels);
    const std::size_t out_frames = static_cast<std::size_t>(
        std::ceil(static_cast<double>(in_frames) * out_rate / in_rate));
    std::vector<float> dst(out_frames * static_cast<std::size_t>(in_channels), 0.0f);

    for (std::size_t of = 0; of < out_frames; ++of) {
        const double pos = static_cast<double>(of) * in_rate / out_rate;
        const std::size_t i0 = static_cast<std::size_t>(pos);
        const std::size_t i1 = std::min(i0 + 1, in_frames - 1);
        const float t = static_cast<float>(pos - i0);
        for (int c = 0; c < in_channels; ++c) {
            dst[of * in_channels + c] =
                src[i0 * in_channels + c] * (1.0f - t) +
                src[i1 * in_channels + c] * t;
        }
    }
    return dst;
}

// Convert mono → stereo
std::vector<float> mono_to_stereo(const std::vector<float>& src) {
    std::vector<float> dst(src.size() * 2);
    for (std::size_t i = 0; i < src.size(); ++i) {
        dst[i * 2 + 0] = src[i];
        dst[i * 2 + 1] = src[i];
    }
    return dst;
}

// ============================================================
// Write 16-bit stereo WAV
// ============================================================

bool write_wav(const std::string& path, const std::vector<float>& samples_stereo,
               int sample_rate) {
    const uint32_t num_frames = static_cast<uint32_t>(samples_stereo.size() / 2);
    const uint32_t byte_rate = static_cast<uint32_t>(sample_rate) * 2 * 2;
    const uint32_t data_size = num_frames * 4;  // 2 ch * 2 bytes

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    // RIFF header
    auto write4 = [&](const char* s) { f.write(s, 4); };
    auto writeu32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeu16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    write4("RIFF");
    writeu32(36 + data_size);
    write4("WAVE");
    write4("fmt ");
    writeu32(16);
    writeu16(1);  // PCM
    writeu16(2);  // stereo
    writeu32(static_cast<uint32_t>(sample_rate));
    writeu32(byte_rate);
    writeu16(4);  // block align
    writeu16(16); // bits per sample
    write4("data");
    writeu32(data_size);

    // Convert float [-1,1] → int16
    for (float s : samples_stereo) {
        int16_t v = static_cast<int16_t>(
            std::clamp(s * 32767.0f, -32768.0f, 32767.0f));
        f.write(reinterpret_cast<const char*>(&v), 2);
    }
    return f.good();
}

}  // namespace

// ============================================================
// Public API
// ============================================================

std::string mix_audio_with_hitsounds(
    const std::string& bgm_path,
    const std::string& respack_zip,
    const std::vector<RuntimeNote>& notes,
    double bgm_volume,
    double hitsound_volume,
    int hitsound_min_interval_ms) {

    // ----- 1. Decode BGM -----
    std::vector<float> bgm_samples;
    int bgm_rate = 0, bgm_ch = 0;
    if (bgm_path.empty() || !decode_ogg(bgm_path, bgm_samples, bgm_rate, bgm_ch)) {
        std::fprintf(stderr, "[audio] could not decode BGM: %s\n", bgm_path.c_str());
        return {};
    }

    // Ensure stereo
    if (bgm_ch == 1) bgm_samples = mono_to_stereo(bgm_samples);

    // Apply BGM volume
    for (float& s : bgm_samples) s *= static_cast<float>(bgm_volume);

    const int rate = bgm_rate;
    const std::size_t total_frames = bgm_samples.size() / 2;

    if (respack_zip.empty() || hitsound_volume <= 0.0) {
        // No hit sounds — just write BGM as WAV and return
        std::string out = "/tmp/phic_bgm_" + std::to_string(std::hash<std::string>{}(bgm_path)) + ".wav";
        if (!write_wav(out, bgm_samples, rate)) return {};
        return out;
    }

    // ----- 2. Extract + decode hit sounds from respack -----
    std::string tmpdir = "/tmp/phic_hs_" + std::to_string(std::hash<std::string>{}(respack_zip));
    {
        std::string cmd = "rm -rf \"" + tmpdir + "\" && mkdir -p \"" + tmpdir +
                          "\" && unzip -oq \"" + respack_zip + "\" -d \"" + tmpdir + "\" 2>/dev/null";
        std::system(cmd.c_str());
    }

    auto load_sound = [&](const std::string& name) -> std::vector<float> {
        std::string p = tmpdir + "/" + name;
        std::vector<float> samples;
        int sr = 0, ch = 0;
        if (!decode_ogg(p, samples, sr, ch)) return {};
        if (ch == 1) samples = mono_to_stereo(samples);
        if (sr != rate) samples = resample(samples, 2, sr, rate);
        // Apply hit sound volume
        for (float& s : samples) s *= static_cast<float>(hitsound_volume);
        return samples;
    };

    std::vector<float> snd_click = load_sound("click.ogg");
    std::vector<float> snd_drag  = load_sound("drag.ogg");
    std::vector<float> snd_flick = load_sound("flick.ogg");

    { std::string cmd = "rm -rf \"" + tmpdir + "\""; std::system(cmd.c_str()); }

    // ----- 3. Mix hit sounds into BGM buffer -----
    const double min_interval = hitsound_min_interval_ms / 1000.0;
    double last_sound_time = -1e9;

    for (const auto& note : notes) {
        if (note.fake) continue;

        const double t = note.t_hit;
        if (t < 0.0) continue;
        if (t - last_sound_time < min_interval) continue;

        const std::vector<float>* snd = nullptr;
        switch (note.kind) {
            case NoteKind::Tap:   snd = &snd_click; break;
            case NoteKind::Hold:  snd = &snd_click; break;
            case NoteKind::Flick: snd = snd_flick.empty() ? &snd_click : &snd_flick; break;
            case NoteKind::Drag:  snd = snd_drag.empty()  ? &snd_click : &snd_drag;  break;
        }
        if (!snd || snd->empty()) continue;

        const std::size_t offset = static_cast<std::size_t>(t * rate) * 2;  // stereo
        const std::size_t copy = std::min(snd->size(), total_frames * 2 - offset);
        if (offset >= total_frames * 2) continue;

        for (std::size_t i = 0; i < copy; ++i) {
            bgm_samples[offset + i] = std::clamp(bgm_samples[offset + i] + (*snd)[i], -1.0f, 1.0f);
        }
        last_sound_time = t;
    }

    // ----- 4. Write WAV -----
    std::string out_path = "/tmp/phic_mixed_" + std::to_string(
        std::hash<std::string>{}(bgm_path + respack_zip)) + ".wav";
    if (!write_wav(out_path, bgm_samples, rate)) {
        std::fprintf(stderr, "[audio] failed to write mixed WAV\n");
        return {};
    }
    std::fprintf(stderr, "[audio] mixed WAV written: %s\n", out_path.c_str());
    return out_path;
}

}  // namespace phic
