#include "phic/core/video_recorder.hpp"

#include <cstdio>
#include <sstream>

namespace phic {

VideoRecorder::VideoRecorder(std::string output_path, int width, int height, double fps, std::string audio_path)
    : output_path_(std::move(output_path)), width_(width), height_(height), fps_(fps), audio_path_(std::move(audio_path)) {}

VideoRecorder::~VideoRecorder() { close(); }

bool VideoRecorder::open() {
    close();

    if (width_ <= 0 || height_ <= 0 || fps_ <= 0.0 || output_path_.empty()) {
        last_error_ = "invalid recorder arguments";
        return false;
    }

#if defined(__unix__) || defined(__APPLE__)
    std::ostringstream cmd;
    cmd << "ffmpeg -hide_banner -loglevel error -y"
        << " -f rawvideo -pix_fmt rgb24"
        << " -s " << width_ << "x" << height_
        << " -r " << fps_
        << " -i -";
    if (!audio_path_.empty()) {
        cmd << " -i \"" << audio_path_ << "\" -c:a aac -b:a 192k";
    }
    cmd << " -c:v libx264 -preset medium -crf 23 -pix_fmt yuv420p";
    if (!audio_path_.empty()) cmd << " -shortest";
    cmd << " \"" << output_path_ << "\"";

    pipe_ = static_cast<void*>(popen(cmd.str().c_str(), "w"));
    if (pipe_ == nullptr) {
        last_error_ = "failed to open ffmpeg pipe";
        return false;
    }
    last_error_.clear();
    return true;
#else
    last_error_ = "VideoRecorder currently supports POSIX popen targets only";
    return false;
#endif
}

bool VideoRecorder::write_frame_rgb24(const uint8_t* data, std::size_t byte_count) {
#if defined(__unix__) || defined(__APPLE__)
    if (pipe_ == nullptr || data == nullptr) {
        last_error_ = "recorder not open";
        return false;
    }
    const std::size_t expected = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 3;
    if (byte_count != expected) {
        last_error_ = "unexpected frame byte_count";
        return false;
    }
    const std::size_t written = std::fwrite(data, 1, byte_count, static_cast<FILE*>(pipe_));
    if (written != byte_count) {
        last_error_ = "short write to ffmpeg";
        return false;
    }
    return true;
#else
    (void)data;
    (void)byte_count;
    last_error_ = "VideoRecorder currently supports POSIX popen targets only";
    return false;
#endif
}

void VideoRecorder::close() {
#if defined(__unix__) || defined(__APPLE__)
    if (pipe_ != nullptr) {
        pclose(static_cast<FILE*>(pipe_));
        pipe_ = nullptr;
    }
#else
    pipe_ = nullptr;
#endif
}

const std::string& VideoRecorder::last_error() const { return last_error_; }

}  // namespace phic
