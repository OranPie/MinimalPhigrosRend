#pragma once
// Video encoder: FFmpeg subprocess pipeline for rendering chart to video file.
// Captures raw RGB24 frames and pipes them to FFmpeg for encoding.
// Optional audio muxing with BGM + hitsounds.

#include "phigros/core/logger.hpp"
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <cstring>
#include <array>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <algorithm>
#include <cctype>
#include <fstream>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace phigros::io {

// --- Encoding Presets ---

struct EncodingPreset {
    std::string name;
    std::string ffmpeg_preset; // ultrafast, medium, slow, veryslow
    int crf;
    std::string codec;         // libx264, libx265, libvpx-vp9
    std::string pix_fmt;       // yuv420p, yuv444p
    std::string extra_args;    // e.g. "-tune zerolatency"
};

inline EncodingPreset get_preset(const std::string& name) {
    if (name == "fast")
        return {"fast", "ultrafast", 28, "libx264", "yuv420p", "-tune zerolatency"};
    if (name == "balanced" || name.empty())
        return {"balanced", "medium", 23, "libx264", "yuv420p", ""};
    if (name == "quality")
        return {"quality", "slow", 18, "libx264", "yuv420p", ""};
    if (name == "archive")
        return {"archive", "veryslow", 15, "libx264", "yuv444p", "-tune film"};
    return {"balanced", "medium", 23, "libx264", "yuv420p", ""};
}

inline std::string to_lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

inline bool is_hw_codec_name(const std::string& codec) {
    return codec.find("_nvenc") != std::string::npos ||
           codec.find("_qsv") != std::string::npos ||
           codec.find("_vaapi") != std::string::npos ||
           codec.find("_amf") != std::string::npos ||
           codec.find("_videotoolbox") != std::string::npos;
}

inline std::string hw_type_to_codec(const std::string& hw_type) {
    const std::string hw = to_lower_ascii(hw_type);
    if (hw == "nvenc") return "h264_nvenc";
    if (hw == "qsv") return "h264_qsv";
    if (hw == "vaapi") return "h264_vaapi";
    if (hw == "amf") return "h264_amf";
    if (hw == "videotoolbox") return "h264_videotoolbox";
    return "";
}

// --- Recording Statistics ---

struct RecordStats {
    int frames_written = 0;
    int64_t bytes_written = 0;
    double start_wall_time = 0.0;
    double total_write_ms = 0.0;
    double max_write_ms = 0.0;
    int slow_writes = 0; // writes >= 50ms

    double elapsed_sec() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(
            now.time_since_epoch()).count() - start_wall_time;
    }

    double fps_wall() const {
        double e = elapsed_sec();
        return (e > 0) ? frames_written / e : 0.0;
    }

    double avg_write_ms() const {
        return (frames_written > 0) ? total_write_ms / frames_written : 0.0;
    }

    double output_mbps() const {
        double e = elapsed_sec();
        return (e > 0) ? (bytes_written / (1024.0 * 1024.0)) / e : 0.0;
    }
};

// --- Frame Capture Buffer ---

struct FrameBuffer {
    std::vector<uint8_t> data;
    int w = 0, h = 0;
    int channels = 4; // 4=RGBA, 3=RGB24

    void resize(int width, int height, int ch = 4) {
        w = width; h = height; channels = ch;
        data.resize(static_cast<size_t>(w) * h * channels);
    }

    size_t byte_size() const { return data.size(); }

    // Point directly at RGBA readback buffer (zero-copy)
    void wrap_rgba(uint8_t* rgba, int width, int height) {
        w = width; h = height; channels = 4;
        // We don't own this data, but we can ref it for write_frame_ptr
    }
};

// --- Audio Mixer ---

struct AudioMixer {
    // Extract BGM to raw PCM WAV using FFmpeg
    static bool extract_audio_wav(const std::string& input,
                                  const std::string& output_wav) {
        std::string cmd = "ffmpeg -hide_banner -loglevel error -y "
                          "-i \"" + input + "\" "
                          "-acodec pcm_s16le -ar 44100 -ac 2 "
                          "\"" + output_wav + "\" 2>&1";
        int ret = std::system(cmd.c_str());
        return ret == 0;
    }

    static bool load_wav_s16(const std::string& path, std::vector<int16_t>& pcm_out) {
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 2, 44100);
        ma_decoder dec;
        if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) return false;
        ma_uint64 length_frames = 0;
        if (ma_data_source_get_length_in_pcm_frames(&dec, &length_frames) != MA_SUCCESS) {
            ma_decoder_uninit(&dec);
            return false;
        }
        pcm_out.resize(static_cast<size_t>(length_frames) * 2);
        ma_uint64 frames_read = 0;
        bool ok = ma_decoder_read_pcm_frames(&dec, pcm_out.data(), length_frames, &frames_read) == MA_SUCCESS;
        pcm_out.resize(static_cast<size_t>(frames_read) * 2);
        ma_decoder_uninit(&dec);
        return ok;
    }

    static bool decode_ogg_memory_s16(const std::vector<uint8_t>& data, std::vector<int16_t>& pcm_out) {
        if (data.empty()) return false;
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 2, 44100);
        ma_decoder dec;
        if (ma_decoder_init_memory(data.data(), data.size(), &cfg, &dec) != MA_SUCCESS) return false;
        ma_uint64 length_frames = 0;
        if (ma_data_source_get_length_in_pcm_frames(&dec, &length_frames) != MA_SUCCESS) {
            ma_decoder_uninit(&dec);
            return false;
        }
        pcm_out.resize(static_cast<size_t>(length_frames) * 2);
        ma_uint64 frames_read = 0;
        bool ok = ma_decoder_read_pcm_frames(&dec, pcm_out.data(), length_frames, &frames_read) == MA_SUCCESS;
        pcm_out.resize(static_cast<size_t>(frames_read) * 2);
        ma_decoder_uninit(&dec);
        return ok;
    }

    static bool write_wav_s16(const std::string& path, const std::vector<int16_t>& pcm) {
        std::ofstream f(path, std::ios::binary);
        if (!f.is_open()) return false;
        const uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
        const uint32_t riff_size = 36u + data_bytes;
        const uint16_t audio_format = 1;
        const uint16_t channels = 2;
        const uint32_t sample_rate = 44100;
        const uint16_t bits_per_sample = 16;
        const uint16_t block_align = channels * bits_per_sample / 8;
        const uint32_t byte_rate = sample_rate * block_align;
        f.write("RIFF", 4);
        f.write(reinterpret_cast<const char*>(&riff_size), 4);
        f.write("WAVE", 4);
        f.write("fmt ", 4);
        const uint32_t fmt_size = 16;
        f.write(reinterpret_cast<const char*>(&fmt_size), 4);
        f.write(reinterpret_cast<const char*>(&audio_format), 2);
        f.write(reinterpret_cast<const char*>(&channels), 2);
        f.write(reinterpret_cast<const char*>(&sample_rate), 4);
        f.write(reinterpret_cast<const char*>(&byte_rate), 4);
        f.write(reinterpret_cast<const char*>(&block_align), 2);
        f.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
        f.write("data", 4);
        f.write(reinterpret_cast<const char*>(&data_bytes), 4);
        f.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(data_bytes));
        return f.good();
    }

    // Mix hitsound into PCM buffer at specific time offset
    // pcm: interleaved int16 stereo 44100Hz buffer
    static void mix_hitsound(std::vector<int16_t>& pcm_buffer,
                             const std::vector<int16_t>& hitsound,
                             double time_sec, float volume = 0.5f) {
        int64_t sample_offset = static_cast<int64_t>(time_sec * 44100.0) * 2; // stereo
        if (sample_offset < 0) return;
        size_t start = static_cast<size_t>(sample_offset);
        for (size_t i = 0; i < hitsound.size() && start + i < pcm_buffer.size(); ++i) {
            int32_t mixed = pcm_buffer[start + i] +
                            static_cast<int32_t>(hitsound[i] * volume);
            // Clip to int16 range
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;
            pcm_buffer[start + i] = static_cast<int16_t>(mixed);
        }
    }
};

// --- FFmpeg Subprocess Video Encoder ---

class VideoEncoder {
public:
    ~VideoEncoder() { close(); }

    static bool codec_available(const std::string& codec) {
        if (codec.empty()) return false;
        PHLOG_TRACE(Record, "Probing codec availability: " << codec);
        std::string cmd = "ffmpeg -hide_banner -loglevel error -encoders 2>/dev/null";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) return false;
        std::array<char, 4096> buf{};
        std::string out;
        while (std::fgets(buf.data(), static_cast<int>(buf.size()), p))
            out += buf.data();
        int ret = pclose(p);
        if (ret != 0 && out.empty()) return false;
        const std::string needle0 = " " + codec;
        const std::string needle1 = "\t" + codec;
        const std::string needle2 = codec + " ";
        const bool ok = out.find(needle0) != std::string::npos ||
               out.find(needle1) != std::string::npos ||
               out.find(needle2) != std::string::npos;
        PHLOG_TRACE(Record, "Codec availability " << codec << "=" << ok);
        return ok;
    }

    static bool codec_usable(const std::string& codec) {
        if (codec.empty()) return false;
        PHLOG_TRACE(Record, "Probing codec usability: " << codec);
        // Some hardware encoders (notably NVENC) reject tiny probe frames (e.g. 16x16).
        // Use a conservative probe size that works across SW/HW encoders.
        constexpr int probe_w = 128;
        constexpr int probe_h = 128;
        std::string cmd = "ffmpeg -hide_banner -loglevel error "
                          "-f lavfi -i color=c=black:s=" + std::to_string(probe_w) + "x" + std::to_string(probe_h) + ":r=1 "
                          "-frames:v 1 -c:v " + codec +
                          " -f null - >/dev/null 2>&1";
        int ret = std::system(cmd.c_str());
        const bool ok = ret == 0;
        PHLOG_TRACE(Record, "Codec usability " << codec << "=" << ok
            << " ret=" << ret);
        return ok;
    }

    bool open(const std::string& output_path, int input_w, int input_h,
              int output_w, int output_h,
              double fps, const EncodingPreset& preset,
              const std::string& input_pix_fmt = "rgba") {
        if (pipe_) return false;
        output_path_ = output_path;
        w_ = input_w; h_ = input_h;
        out_w_ = output_w; out_h_ = output_h;
        fps_ = fps;
        preset_ = preset;
        input_pix_fmt_ = input_pix_fmt;
        int bpp = (input_pix_fmt == "rgba") ? 4 : 3;
        frame_size_ = static_cast<size_t>(w_) * h_ * bpp;

        cmd_last_ = build_ffmpeg_cmd(output_path, false);
        PHLOG_DEBUG(Record, "VideoEncoder command: " << cmd_last_);
        pipe_ = popen(cmd_last_.c_str(), "w");
        if (!pipe_) {
            PHLOG_ERROR(Record, "VideoEncoder: failed to start FFmpeg");
            return false;
        }
        std::setvbuf(pipe_, nullptr, _IOFBF, 1 << 20);

        {
            std::lock_guard<std::mutex> lock(stats_mu_);
            stats_ = RecordStats{};
            stats_.start_wall_time = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        return true;
    }

    // Write one RGB24 frame
    bool write_frame(const FrameBuffer& frame) {
        return write_frame_ptr(frame.data.data(), frame.byte_size());
    }

    // Write raw pixel data directly (zero-copy from readback buffer)
    bool write_frame_ptr(const uint8_t* data, size_t len) {
        if (!pipe_ || len != frame_size_) return false;
        auto t0 = std::chrono::steady_clock::now();
        size_t written = fwrite(data, 1, frame_size_, pipe_);
        auto t1 = std::chrono::steady_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        {
            std::lock_guard<std::mutex> lock(stats_mu_);
            stats_.total_write_ms += ms;
            if (ms > stats_.max_write_ms) stats_.max_write_ms = ms;
            if (ms >= 50.0) ++stats_.slow_writes;
            if (written == frame_size_) {
                stats_.frames_written++;
                stats_.bytes_written += static_cast<int64_t>(frame_size_);
            }
        }
        if (written != frame_size_) {
            PHLOG_ERROR(Record, "VideoEncoder: write failed (wrote " << written
                << "/" << frame_size_ << ")");
            return false;
        }
        return true;
    }

    int close() {
        if (!pipe_) return 0;
        int ret = pclose(pipe_);
        pipe_ = nullptr;
        PHLOG_DEBUG(Record, "VideoEncoder pipe closed ret=" << ret);
        return ret;
    }

    bool is_open() const { return pipe_ != nullptr; }
    RecordStats stats_snapshot() const {
        std::lock_guard<std::mutex> lock(stats_mu_);
        return stats_;
    }
    int width() const { return w_; }
    int height() const { return h_; }
    const std::string& command_line() const { return cmd_last_; }

    // Mux video with audio as a post-processing step
    static bool mux_audio(const std::string& video_path,
                          const std::string& audio_path,
                          const std::string& output_path) {
        PHLOG_INFO(Record, "Mux audio: video=" << video_path
            << " audio=" << audio_path
            << " output=" << output_path);
        std::string cmd = "ffmpeg -hide_banner -loglevel error -y "
                          "-i \"" + video_path + "\" "
                          "-i \"" + audio_path + "\" "
                          "-c:v copy -c:a aac -b:a 192k "
                          "-shortest \"" + output_path + "\" 2>&1";
        int ret = std::system(cmd.c_str());
        PHLOG_DEBUG(Record, "Mux audio ret=" << ret);
        return ret == 0;
    }

private:
    static std::string map_nvenc_preset(const std::string& p) {
        if (p == "ultrafast") return "p1";
        if (p == "slow") return "p6";
        if (p == "veryslow") return "p7";
        return "p4"; // medium / default
    }

    std::string build_ffmpeg_cmd(const std::string& output, bool with_audio) const {
        (void)with_audio;
        char fps_str[32];
        std::snprintf(fps_str, sizeof(fps_str), "%.4f", fps_);

        std::string cmd = "ffmpeg -hide_banner -loglevel error -nostats -y "
                          "-f rawvideo -pix_fmt " + input_pix_fmt_ + " "
                          "-s " + std::to_string(w_) + "x" + std::to_string(h_) + " "
                          "-r " + std::string(fps_str) + " "
                          "-i pipe:0 ";

        std::string vf;
        if (out_w_ > 0 && out_h_ > 0 && (out_w_ != w_ || out_h_ != h_)) {
            vf = "scale=" + std::to_string(out_w_) + ":" + std::to_string(out_h_) +
                 ":flags=bilinear";
        }

        if (preset_.codec.find("_vaapi") != std::string::npos) {
            if (!vf.empty()) vf += ",";
            vf += "format=nv12,hwupload";
        }
        if (!vf.empty()) cmd += "-vf " + vf + " ";

        cmd += "-c:v " + preset_.codec + " ";
        if (preset_.codec.find("_nvenc") != std::string::npos) {
            cmd += "-preset " + map_nvenc_preset(preset_.ffmpeg_preset) + " "
                   "-cq " + std::to_string(preset_.crf) + " "
                   "-b:v 0 "
                   "-pix_fmt " + preset_.pix_fmt + " ";
        } else if (preset_.codec.find("_qsv") != std::string::npos) {
            cmd += "-global_quality " + std::to_string(preset_.crf) + " "
                   "-look_ahead 0 "
                   "-pix_fmt nv12 ";
        } else if (preset_.codec.find("_vaapi") != std::string::npos) {
            cmd += "-qp " + std::to_string(preset_.crf) + " "
                   "-pix_fmt vaapi ";
        } else if (preset_.codec.find("_videotoolbox") != std::string::npos) {
            int qv = std::clamp(63 - preset_.crf, 1, 63);
            cmd += "-q:v " + std::to_string(qv) + " "
                   "-pix_fmt yuv420p ";
        } else if (preset_.codec.find("_amf") != std::string::npos) {
            cmd += "-quality quality "
                   "-rc cqp "
                   "-qp_i " + std::to_string(preset_.crf) + " "
                   "-qp_p " + std::to_string(preset_.crf) + " "
                   "-pix_fmt " + preset_.pix_fmt + " ";
        } else {
            cmd += "-preset " + preset_.ffmpeg_preset + " ";
            if (preset_.codec == "libvpx-vp9")
                cmd += "-crf " + std::to_string(preset_.crf) + " -b:v 0 ";
            else
                cmd += "-crf " + std::to_string(preset_.crf) + " ";
            cmd += "-pix_fmt " + preset_.pix_fmt + " ";
        }
        cmd += "-threads 0 ";

        if (!preset_.extra_args.empty() && !is_hw_codec_name(preset_.codec))
            cmd += preset_.extra_args + " ";

        cmd += "\"" + output + "\"";
        return cmd;
    }

    FILE* pipe_ = nullptr;
    std::string output_path_;
    std::string input_pix_fmt_ = "rgba";
    int w_ = 0, h_ = 0;        // input (readback) resolution
    int out_w_ = 0, out_h_ = 0; // output (video) resolution (0 = same as input)
    double fps_ = 60.0;
    size_t frame_size_ = 0;
    EncodingPreset preset_;
    std::string cmd_last_;
    mutable std::mutex stats_mu_;
    RecordStats stats_;
};

// --- Recording Session (orchestrates capture + encode + mux) ---

struct RecordConfig {
    std::string output;                    // output.mp4
    std::string preset_name = "balanced";  // fast|balanced|quality|archive
    std::string codec;                     // override: libx265, libvpx-vp9
    std::string hw_type;                   // nvenc|qsv|vaapi|amf|videotoolbox
    double fps = 60.0;
    int width = 0, height = 0;             // output size (0 = same as capture)
    int capture_width = 0, capture_height = 0; // render/readback size
    int queue_depth = 6;                   // <=1 disables async queue
    double start_time = -1.0;             // chart time to start recording
    double end_time = 0.0;                // 0 = full chart
    std::string audio_path;               // BGM for muxing
    bool no_hitsound = false;
    std::vector<uint8_t> hitsound_ogg[5];
};

struct HitsoundEvent {
    int kind = 0;
    double time_sec = 0.0;
};

class RecordingSession {
public:
    ~RecordingSession() {
        if (started_) {
            PHLOG_WARN(Record, "RecordingSession destroyed while active, forcing finish");
            finish();
        }
    }

    bool start(const RecordConfig& rc, int window_w, int window_h) {
        if (started_) {
            PHLOG_WARN(Record, "RecordingSession start ignored: already started");
            return false;
        }
        cfg_ = rc;
        {
            std::lock_guard<std::mutex> lock(queue_mu_);
            queue_.clear();
            queue_peak_ = 0;
            stop_worker_ = false;
            worker_failed_ = false;
        }

        capture_w_ = (rc.capture_width > 0) ? rc.capture_width : window_w;
        capture_h_ = (rc.capture_height > 0) ? rc.capture_height : window_h;
        if (capture_w_ != window_w || capture_h_ != window_h) {
            PHLOG_WARN(Record, "Capture resolution (" << capture_w_ << "x" << capture_h_
                << ") does not match render size (" << window_w << "x" << window_h
                << "); using render size for readback");
            capture_w_ = window_w;
            capture_h_ = window_h;
        }
        int out_w = (rc.width > 0) ? rc.width : capture_w_;
        int out_h = (rc.height > 0) ? rc.height : capture_h_;

        auto preset = get_preset(rc.preset_name);
        const std::string fallback_codec = preset.codec;
        if (!rc.codec.empty()) {
            preset.codec = rc.codec;
        } else if (!rc.hw_type.empty()) {
            std::string mapped = hw_type_to_codec(rc.hw_type);
            if (mapped.empty()) {
                PHLOG_WARN(Record, "Unknown --record-hw value '" << rc.hw_type
                    << "', using software codec '" << preset.codec << "'");
            } else {
                preset.codec = mapped;
            }
        }
        if (!VideoEncoder::codec_available(preset.codec)) {
            PHLOG_WARN(Record, "Codec '" << preset.codec
                << "' is unavailable; falling back to '" << fallback_codec << "'");
            preset.codec = fallback_codec;
        }
        if (is_hw_codec_name(preset.codec) && !VideoEncoder::codec_usable(preset.codec)) {
            PHLOG_WARN(Record, "Hardware codec '" << preset.codec
                << "' is not usable on this system; falling back to '"
                << fallback_codec << "'");
            preset.codec = fallback_codec;
        }
        if (!VideoEncoder::codec_available(preset.codec)) {
            PHLOG_ERROR(Record, "Codec '" << preset.codec
                << "' is unavailable in FFmpeg. Install the encoder or choose another codec.");
            return false;
        }
        if (!VideoEncoder::codec_usable(preset.codec)) {
            PHLOG_ERROR(Record, "Codec '" << preset.codec
                << "' failed ffmpeg probe. Choose a different codec.");
            return false;
        }

        std::string target = rc.output;
        video_tmp_.clear();
        if (!rc.audio_path.empty()) {
            video_tmp_ = rc.output + ".video_tmp.mp4";
            target = video_tmp_;
        }

        // Pipe RGBA directly — FFmpeg handles scaling if output != capture.
        if (!encoder_.open(target, capture_w_, capture_h_, out_w, out_h, rc.fps, preset, "rgba")) {
            PHLOG_ERROR(Record, "Failed to open encoder target=" << target);
            return false;
        }

        queue_capacity_ = static_cast<size_t>(std::max(1, rc.queue_depth));
        async_enabled_ = queue_capacity_ > 1;
        if (async_enabled_) {
            try {
                worker_ = std::thread([this]() { worker_main(); });
                PHLOG_DEBUG(Record, "Encoder worker started");
            } catch (const std::exception& e) {
                PHLOG_ERROR(Record, "Failed to start encoder worker: " << e.what());
                encoder_.close();
                return false;
            }
        }

        started_ = true;
        PHLOG_INFO(Record, "Started: capture=" << capture_w_ << "x" << capture_h_
            << " output=" << out_w << "x" << out_h
            << " @ " << rc.fps << "fps"
            << " preset=" << preset.name
            << " codec=" << preset.codec
            << " queue=" << queue_capacity_
            << (async_enabled_ ? " (async)" : " (sync)"));
        return true;
    }

    // Capture RGBA framebuffer. Async mode copies into queue; worker writes to FFmpeg.
    bool capture_rgba(const uint8_t* rgba, int w, int h) {
        if (!started_) {
            PHLOG_TRACE(Record, "capture_rgba ignored: session not started");
            return false;
        }
        if (w != capture_w_ || h != capture_h_) {
            PHLOG_ERROR(Record, "Capture size mismatch: got " << w << "x" << h
                << ", expected " << capture_w_ << "x" << capture_h_);
            return false;
        }
        size_t len = static_cast<size_t>(w) * h * 4;
        if (!async_enabled_) {
            PHLOG_TRACE(Record, "capture_rgba sync write len=" << len);
            return encoder_.write_frame_ptr(rgba, len);
        }

        std::vector<uint8_t> frame(len);
        std::memcpy(frame.data(), rgba, len);
        std::unique_lock<std::mutex> lock(queue_mu_);
        queue_cv_not_full_.wait(lock, [this]() {
            return queue_.size() < queue_capacity_ || stop_worker_ || worker_failed_;
        });
        if (stop_worker_ || worker_failed_) return false;
        queue_.push_back(std::move(frame));
        if (queue_.size() > queue_peak_) queue_peak_ = queue_.size();
        lock.unlock();
        queue_cv_not_empty_.notify_one();
        return true;
    }

    // Finalize: close encoder, mux audio if needed
    bool finish() {
        if (!started_) {
            PHLOG_TRACE(Record, "finish ignored: session not started");
            return false;
        }
        started_ = false;

        if (async_enabled_) {
            {
                std::lock_guard<std::mutex> lock(queue_mu_);
                stop_worker_ = true;
            }
            queue_cv_not_empty_.notify_all();
            queue_cv_not_full_.notify_all();
            if (worker_.joinable()) worker_.join();
            PHLOG_DEBUG(Record, "Encoder worker joined");
            async_enabled_ = false;
        }

        int ret = encoder_.close();
        auto s = encoder_.stats_snapshot();
        double pipe_mb = s.bytes_written / (1024.0 * 1024.0);
        {
            std::ostringstream oss;
            oss << "Complete: " << s.frames_written << " frames"
                << ", " << static_cast<int>(pipe_mb) << " MB piped"
                << ", " << s.fps_wall() << " fps"
                << ", avg write " << s.avg_write_ms() << "ms"
                << " (max " << s.max_write_ms << "ms";
            if (s.slow_writes > 0) oss << ", " << s.slow_writes << " slow";
            oss << ", queue_peak " << queue_peak_ << "/" << queue_capacity_ << ")";
            PHLOG_INFO(Record, oss.str());
        }

        if (worker_failed_) {
            PHLOG_ERROR(Record, "Async encoder worker failed before completion");
            return false;
        }

        if (ret != 0) {
            PHLOG_ERROR(Record, "FFmpeg exited with code " << ret);
            return false;
        }

        // Audio mux step
        if (!cfg_.audio_path.empty() && !video_tmp_.empty()) {
            PHLOG_INFO(Record, "Muxing audio…");
            std::string mux_audio_path = cfg_.audio_path;
            std::string mixed_audio_tmp;
            if (!cfg_.no_hitsound && !hitsound_events_.empty()) {
                mixed_audio_tmp = cfg_.output + ".audio_tmp.wav";
                if (build_mixed_audio_track(mixed_audio_tmp)) {
                    mux_audio_path = mixed_audio_tmp;
                } else {
                    PHLOG_WARN(Record, "Failed to build mixed hitsound track; muxing raw BGM only");
                }
            }
            bool ok = VideoEncoder::mux_audio(video_tmp_, mux_audio_path, cfg_.output);
            // Clean up temp video
            std::filesystem::remove(video_tmp_);
            if (!mixed_audio_tmp.empty()) std::filesystem::remove(mixed_audio_tmp);
            if (!ok) {
                PHLOG_ERROR(Record, "Audio mux failed");
                return false;
            }
            PHLOG_INFO(Record, "Output: " << cfg_.output);
        } else {
            PHLOG_INFO(Record, "Output: " << cfg_.output);
        }

        return true;
    }

    bool is_active() const { return started_; }
    RecordStats stats_snapshot() const { return encoder_.stats_snapshot(); }
    void record_hitsound(int kind, double time_sec) {
        if (cfg_.no_hitsound || kind < 1 || kind > 4) return;
        hitsound_events_.push_back({kind, time_sec});
    }
    void log_progress(double chart_time, double chart_end) const {
        auto s = encoder_.stats_snapshot();
        double pct = (chart_end > 0) ? (chart_time / chart_end * 100.0) : 0.0;
        pct = std::clamp(pct, 0.0, 100.0);
        double elapsed = s.elapsed_sec();
        double speed = (elapsed > 0) ? chart_time / elapsed : 0.0;
        double remaining = (speed > 0.01 && chart_end > chart_time)
            ? (chart_end - chart_time) / speed : 0.0;
        int eta_min = static_cast<int>(remaining) / 60;
        int eta_sec = static_cast<int>(remaining) % 60;
        size_t qsz = queue_size_snapshot();
        // Use raw printf for the inline progress line (overwritten in-place with \r)
        std::printf("\r[Record] %.1f%% | f%d | %.0ffps | q%zu/%zu | %.1fx speed | ETA %d:%02d  ",
                    pct, s.frames_written, s.fps_wall(), qsz, queue_capacity_,
                    speed, eta_min, eta_sec);
        std::fflush(stdout);
        // Also emit to logger at TRACE level (without \r, for log files)
        PHLOG_TRACE(Record, "Progress " << static_cast<int>(pct) << "%"
            << " frames=" << s.frames_written
            << " fps=" << static_cast<int>(s.fps_wall())
            << " queue=" << qsz << "/" << queue_capacity_
            << " speed=" << speed << "x"
            << " eta=" << eta_min << "m" << eta_sec << "s");
    }

private:
    bool build_mixed_audio_track(const std::string& output_wav) {
        const std::string bgm_tmp = output_wav + ".bgm.wav";
        if (!AudioMixer::extract_audio_wav(cfg_.audio_path, bgm_tmp)) return false;
        std::vector<int16_t> mixed_pcm;
        if (!AudioMixer::load_wav_s16(bgm_tmp, mixed_pcm)) {
            std::filesystem::remove(bgm_tmp);
            return false;
        }
        std::filesystem::remove(bgm_tmp);

        std::vector<int16_t> hitsound_pcm[5];
        for (int k = 1; k <= 4; ++k) {
            if (!cfg_.hitsound_ogg[k].empty())
                AudioMixer::decode_ogg_memory_s16(cfg_.hitsound_ogg[k], hitsound_pcm[k]);
        }

        for (const auto& ev : hitsound_events_) {
            int kind = ev.kind;
            if (kind == 3 && hitsound_pcm[3].empty()) kind = 1;
            if (kind < 1 || kind > 4 || hitsound_pcm[kind].empty()) continue;
            AudioMixer::mix_hitsound(mixed_pcm, hitsound_pcm[kind], ev.time_sec, 0.65f);
        }
        return AudioMixer::write_wav_s16(output_wav, mixed_pcm);
    }

    void worker_main() {
        for (;;) {
            std::vector<uint8_t> frame;
            {
                std::unique_lock<std::mutex> lock(queue_mu_);
                queue_cv_not_empty_.wait(lock, [this]() {
                    return stop_worker_ || !queue_.empty();
                });
                if (queue_.empty()) {
                    if (stop_worker_) break;
                    continue;
                }
                frame = std::move(queue_.front());
                queue_.pop_front();
            }
            queue_cv_not_full_.notify_one();

            if (!encoder_.write_frame_ptr(frame.data(), frame.size())) {
                PHLOG_ERROR(Record, "Encoder worker write failed for queued frame of "
                    << frame.size() << " bytes");
                std::lock_guard<std::mutex> lock(queue_mu_);
                worker_failed_ = true;
                stop_worker_ = true;
                queue_.clear();
                queue_cv_not_full_.notify_all();
                queue_cv_not_empty_.notify_all();
                return;
            }
            PHLOG_TRACE(Record, "Encoder worker wrote frame, queue_remaining=" << queue_size_snapshot());
        }
    }

    size_t queue_size_snapshot() const {
        std::lock_guard<std::mutex> lock(queue_mu_);
        return queue_.size();
    }

    VideoEncoder encoder_;
    RecordConfig cfg_;
    std::string video_tmp_;
    bool started_ = false;
    int capture_w_ = 0;
    int capture_h_ = 0;
    std::vector<HitsoundEvent> hitsound_events_;

    mutable std::mutex queue_mu_;
    std::condition_variable queue_cv_not_empty_;
    std::condition_variable queue_cv_not_full_;
    std::deque<std::vector<uint8_t>> queue_;
    std::thread worker_;
    size_t queue_capacity_ = 1;
    size_t queue_peak_ = 0;
    bool async_enabled_ = false;
    bool stop_worker_ = false;
    bool worker_failed_ = false;
};

} // namespace phigros::io
