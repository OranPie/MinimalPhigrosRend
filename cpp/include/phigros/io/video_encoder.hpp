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
    std::vector<uint8_t> data; // RGB24 pixels
    int w = 0, h = 0;

    void resize(int width, int height) {
        w = width; h = height;
        data.resize(static_cast<size_t>(w) * h * 3);
    }

    size_t byte_size() const { return data.size(); }

    // Convert RGBA32 → RGB24 in-place (from a readback buffer)
    void from_rgba(const uint8_t* rgba, int width, int height) {
        resize(width, height);
        size_t pixels = static_cast<size_t>(width) * height;
        for (size_t i = 0; i < pixels; ++i) {
            data[i * 3 + 0] = rgba[i * 4 + 0];
            data[i * 3 + 1] = rgba[i * 4 + 1];
            data[i * 3 + 2] = rgba[i * 4 + 2];
        }
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

    bool open(const std::string& output_path, int width, int height,
              double fps, const EncodingPreset& preset) {
        if (pipe_) return false;
        output_path_ = output_path;
        w_ = width; h_ = height;
        fps_ = fps;
        preset_ = preset;
        frame_size_ = static_cast<size_t>(w_) * h_ * 3;

        // Build FFmpeg command
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

    // Write raw RGB24 data directly
    bool write_frame_raw(const uint8_t* rgb24, size_t len) {
        if (!pipe_ || len != frame_size_) return false;
        auto t0 = std::chrono::steady_clock::now();
        size_t written = fwrite(rgb24, 1, frame_size_, pipe_);
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
                          "-f rawvideo -pix_fmt rgb24 "
                          "-s " + std::to_string(w_) + "x" + std::to_string(h_) + " "
                          "-r " + std::string(fps_str) + " "
                          "-i pipe:0 ";

        cmd += "-c:v " + preset_.codec + " "
               "-preset " + preset_.ffmpeg_preset + " "
               "-crf " + std::to_string(preset_.crf) + " "
               "-pix_fmt " + preset_.pix_fmt + " ";

        if (!preset_.extra_args.empty())
            cmd += preset_.extra_args + " ";

        cmd += "\"" + output + "\"";
        return cmd;
    }

    FILE* pipe_ = nullptr;
    std::string output_path_;
    int w_ = 0, h_ = 0;
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
        int w = (rc.width > 0) ? rc.width : window_w;
        int h = (rc.height > 0) ? rc.height : window_h;

        auto preset = get_preset(rc.preset_name);
        if (!rc.codec.empty()) preset.codec = rc.codec;

        // If audio muxing needed, encode video to temp file first
        if (!rc.audio_path.empty()) {
            video_tmp_ = rc.output + ".video_tmp.mp4";
            if (!encoder_.open(video_tmp_, w, h, rc.fps, preset)) return false;
        } else {
            if (!encoder_.open(rc.output, w, h, rc.fps, preset)) return false;
        }

        frame_buf_.resize(w, h);
        rgba_buf_.resize(static_cast<size_t>(w) * h * 4);
        started_ = true;
        std::cout << "[Record] " << w << "x" << h << " @ " << rc.fps
                  << "fps, preset=" << preset.name
                  << ", codec=" << preset.codec << std::endl;
        return true;
    }

    // Capture current framebuffer (call after end_frame but before present, or use readback)
    // pixels: RGBA32 data from SDL_RenderReadPixels
    bool capture_rgba(const uint8_t* rgba, int w, int h) {
        if (!started_) return false;
        frame_buf_.from_rgba(rgba, w, h);
        return encoder_.write_frame(frame_buf_);
    }

    // Finalize: close encoder, mux audio if needed
    bool finish() {
        if (!started_) return false;
        started_ = false;

        int ret = encoder_.close();
        auto& s = encoder_.stats();
        std::cout << "\n[Record] Complete: " << s.frames_written << " frames"
                  << ", " << (s.bytes_written / (1024*1024)) << " MB input"
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
        std::printf("\r[Record] %.1f%% | frame %d | %.1f fps | %.1f MB",
                    pct, s.frames_written, s.fps_wall(),
                    s.bytes_written / (1024.0 * 1024.0));
        std::fflush(stdout);
    }

private:
    VideoEncoder encoder_;
    FrameBuffer frame_buf_;
    std::vector<uint8_t> rgba_buf_;
    RecordConfig cfg_;
    std::string video_tmp_;
    bool started_ = false;
};

} // namespace phigros::io
