#pragma once
// Video encoder: FFmpeg subprocess pipeline for rendering chart to video file.
// Captures raw RGB24 frames and pipes them to FFmpeg for encoding.
// Optional audio muxing with BGM + hitsounds.

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

        std::string cmd = build_ffmpeg_cmd(output_path, false);
        pipe_ = popen(cmd.c_str(), "w");
        if (!pipe_) {
            std::cerr << "[VideoEncoder] Failed to start FFmpeg\n";
            return false;
        }

        stats_.start_wall_time = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        return true;
    }

    // Write one RGB24 frame
    bool write_frame(const FrameBuffer& frame) {
        if (!pipe_ || frame.byte_size() != frame_size_) return false;

        auto t0 = std::chrono::steady_clock::now();
        size_t written = fwrite(frame.data.data(), 1, frame_size_, pipe_);
        auto t1 = std::chrono::steady_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        stats_.total_write_ms += ms;
        if (ms > stats_.max_write_ms) stats_.max_write_ms = ms;
        if (ms >= 50.0) ++stats_.slow_writes;

        if (written != frame_size_) {
            std::cerr << "[VideoEncoder] Write failed (wrote " << written
                      << "/" << frame_size_ << ")\n";
            return false;
        }

        stats_.frames_written++;
        stats_.bytes_written += static_cast<int64_t>(frame_size_);
        return true;
    }

    // Write raw pixel data directly (zero-copy from readback buffer)
    bool write_frame_ptr(const uint8_t* data, size_t len) {
        if (!pipe_ || len != frame_size_) return false;
        auto t0 = std::chrono::steady_clock::now();
        size_t written = fwrite(data, 1, frame_size_, pipe_);
        auto t1 = std::chrono::steady_clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        stats_.total_write_ms += ms;
        if (ms > stats_.max_write_ms) stats_.max_write_ms = ms;
        if (ms >= 50.0) ++stats_.slow_writes;

        if (written != frame_size_) return false;
        stats_.frames_written++;
        stats_.bytes_written += static_cast<int64_t>(frame_size_);
        return true;
    }

    int close() {
        if (!pipe_) return 0;
        int ret = pclose(pipe_);
        pipe_ = nullptr;
        return ret;
    }

    bool is_open() const { return pipe_ != nullptr; }
    const RecordStats& stats() const { return stats_; }
    int width() const { return w_; }
    int height() const { return h_; }

    // Mux video with audio as a post-processing step
    static bool mux_audio(const std::string& video_path,
                          const std::string& audio_path,
                          const std::string& output_path) {
        std::string cmd = "ffmpeg -hide_banner -loglevel error -y "
                          "-i \"" + video_path + "\" "
                          "-i \"" + audio_path + "\" "
                          "-c:v copy -c:a aac -b:a 192k "
                          "-shortest \"" + output_path + "\" 2>&1";
        int ret = std::system(cmd.c_str());
        return ret == 0;
    }

private:
    std::string build_ffmpeg_cmd(const std::string& output, bool with_audio) const {
        char fps_str[32];
        std::snprintf(fps_str, sizeof(fps_str), "%.4f", fps_);

        std::string cmd = "ffmpeg -hide_banner -loglevel error -nostats -y "
                          "-f rawvideo -pix_fmt " + input_pix_fmt_ + " "
                          "-s " + std::to_string(w_) + "x" + std::to_string(h_) + " "
                          "-r " + std::string(fps_str) + " "
                          "-i pipe:0 ";

        // Scale if output resolution differs from input
        if (out_w_ > 0 && out_h_ > 0 && (out_w_ != w_ || out_h_ != h_)) {
            cmd += "-vf scale=" + std::to_string(out_w_) + ":" + std::to_string(out_h_)
                   + ":flags=bilinear ";
        }

        cmd += "-c:v " + preset_.codec + " "
               "-preset " + preset_.ffmpeg_preset + " "
               "-crf " + std::to_string(preset_.crf) + " "
               "-pix_fmt " + preset_.pix_fmt + " "
               "-threads 0 ";

        if (!preset_.extra_args.empty())
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
    RecordStats stats_;
};

// --- Recording Session (orchestrates capture + encode + mux) ---

struct RecordConfig {
    std::string output;                    // output.mp4
    std::string preset_name = "balanced";  // fast|balanced|quality|archive
    std::string codec;                     // override: libx265, libvpx-vp9
    double fps = 60.0;
    int width = 0, height = 0;            // 0 = use window resolution
    double start_time = -1.0;             // chart time to start recording
    double end_time = 0.0;                // 0 = full chart
    std::string audio_path;               // BGM for muxing
    bool no_hitsound = false;
};

class RecordingSession {
public:
    bool start(const RecordConfig& rc, int window_w, int window_h) {
        cfg_ = rc;
        // Input resolution = always window size (what read_pixels gives us)
        int in_w = window_w, in_h = window_h;
        // Output resolution = user override or same as input
        int out_w = (rc.width > 0) ? rc.width : in_w;
        int out_h = (rc.height > 0) ? rc.height : in_h;

        auto preset = get_preset(rc.preset_name);
        if (!rc.codec.empty()) preset.codec = rc.codec;

        std::string target = rc.output;
        if (!rc.audio_path.empty()) {
            video_tmp_ = rc.output + ".video_tmp.mp4";
            target = video_tmp_;
        }

        // Pipe RGBA directly — FFmpeg handles scaling if out != in
        if (!encoder_.open(target, in_w, in_h, out_w, out_h, rc.fps, preset, "rgba"))
            return false;

        started_ = true;
        std::string res_info = std::to_string(in_w) + "x" + std::to_string(in_h);
        if (out_w != in_w || out_h != in_h)
            res_info += " → " + std::to_string(out_w) + "x" + std::to_string(out_h);
        std::cout << "[Record] " << res_info << " @ " << rc.fps
                  << "fps, preset=" << preset.name
                  << ", codec=" << preset.codec << std::endl;
        return true;
    }

    // Capture RGBA framebuffer directly to FFmpeg (zero-copy, no conversion)
    bool capture_rgba(const uint8_t* rgba, int w, int h) {
        if (!started_) return false;
        size_t len = static_cast<size_t>(w) * h * 4;
        return encoder_.write_frame_ptr(rgba, len);
    }

    // Finalize: close encoder, mux audio if needed
    bool finish() {
        if (!started_) return false;
        started_ = false;

        int ret = encoder_.close();
        auto& s = encoder_.stats();
        double pipe_mb = s.bytes_written / (1024.0 * 1024.0);
        std::cout << "\n[Record] Complete: " << s.frames_written << " frames"
                  << ", " << static_cast<int>(pipe_mb) << " MB piped"
                  << ", " << s.fps_wall() << " fps"
                  << ", avg write " << s.avg_write_ms() << "ms"
                  << " (max " << s.max_write_ms << "ms";
        if (s.slow_writes > 0)
            std::cout << ", " << s.slow_writes << " slow";
        std::cout << ")" << std::endl;

        if (ret != 0) {
            std::cerr << "[Record] FFmpeg exited with code " << ret << std::endl;
            return false;
        }

        // Audio mux step
        if (!cfg_.audio_path.empty() && !video_tmp_.empty()) {
            std::cout << "[Record] Muxing audio..." << std::endl;
            bool ok = VideoEncoder::mux_audio(video_tmp_, cfg_.audio_path, cfg_.output);
            // Clean up temp video
            std::filesystem::remove(video_tmp_);
            if (!ok) {
                std::cerr << "[Record] Audio mux failed\n";
                // Rename temp as final output (video-only)
                return false;
            }
            std::cout << "[Record] Output: " << cfg_.output << std::endl;
        } else {
            std::cout << "[Record] Output: " << cfg_.output << std::endl;
        }

        return true;
    }

    bool is_active() const { return started_; }
    const RecordStats& stats() const { return encoder_.stats(); }
    void log_progress(double chart_time, double chart_end) const {
        auto& s = encoder_.stats();
        double pct = (chart_end > 0) ? (chart_time / chart_end * 100.0) : 0.0;
        double elapsed = s.elapsed_sec();
        double speed = (elapsed > 0) ? chart_time / elapsed : 0.0;
        double remaining = (speed > 0.01 && chart_end > chart_time)
            ? (chart_end - chart_time) / speed : 0.0;
        int eta_min = static_cast<int>(remaining) / 60;
        int eta_sec = static_cast<int>(remaining) % 60;
        std::printf("\r[Record] %.1f%% | f%d | %.0ffps | %.1fx speed | ETA %d:%02d  ",
                    pct, s.frames_written, s.fps_wall(), speed, eta_min, eta_sec);
        std::fflush(stdout);
    }

private:
    VideoEncoder encoder_;
    RecordConfig cfg_;
    std::string video_tmp_;
    bool started_ = false;
};

} // namespace phigros::io
