#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace phic {

class VideoRecorder {
public:
    VideoRecorder(std::string output_path, int width, int height, double fps, std::string audio_path = "");
    ~VideoRecorder();

    bool open();
    bool write_frame_rgb24(const uint8_t* data, std::size_t byte_count);
    void close();

    const std::string& last_error() const;

private:
    std::string output_path_;
    int width_ = 0;
    int height_ = 0;
    double fps_ = 60.0;
    std::string audio_path_;

    void* pipe_ = nullptr;
    std::string last_error_;
};

}  // namespace phic
