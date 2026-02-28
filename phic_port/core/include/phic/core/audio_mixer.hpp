#pragma once

#include "phic/core/types.hpp"

#include <string>
#include <vector>

namespace phic {

// Pre-mixes BGM OGG + respack hit sounds into a single WAV file.
// Returns the path to the temporary WAV file, or empty string on failure.
// Caller should delete the returned temp file when done.
std::string mix_audio_with_hitsounds(
    const std::string& bgm_path,       // source BGM (.ogg / .mp3 / .wav)
    const std::string& respack_zip,    // respack .zip (has click.ogg, drag.ogg, flick.ogg)
    const std::vector<RuntimeNote>& notes,  // chart notes — t_hit + kind used for placement
    double bgm_volume = 0.9,
    double hitsound_volume = 0.5,
    int hitsound_min_interval_ms = 30);

}  // namespace phic
