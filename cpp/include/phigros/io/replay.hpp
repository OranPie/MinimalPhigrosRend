#pragma once
#include "phigros/engine/judge.hpp"
#include "phigros/core/types.hpp"
#include <miniz.h>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <functional>

namespace phigros::io {

// --- Binary replay format ---
// Header  : magic[4] + version[2] + chart_hash[4] + unix_ts[8] + n_events[4]  = 22 bytes
// Events  : [t:f32, note_idx:u32, grade:u8]  ×  N                              = 9N bytes
// Footer  : crc32[4]
//
// The whole payload (header+events) is deflate-compressed.

static constexpr char REPLAY_MAGIC[4] = {'P','H','R','P'};
static constexpr uint16_t REPLAY_VERSION = 1;

enum class ReplayGrade : uint8_t {
    Perfect     = 0,
    Good        = 1,
    Bad         = 2,
    Miss        = 3,
    HoldStart   = 4,
    HoldRelease = 5,
};

struct ReplayEvent {
    float    t;         // chart time
    uint32_t note_idx;  // index into chart.notes
    uint8_t  grade;     // ReplayGrade
};

inline uint8_t grade_to_u8(const std::string& g) {
    if (g.rfind("hold_start:", 0) == 0)      return (uint8_t)ReplayGrade::HoldStart;
    if (g == "hold_release")                  return (uint8_t)ReplayGrade::HoldRelease;
    if (g == "PERFECT")                       return (uint8_t)ReplayGrade::Perfect;
    if (g == "GOOD")                          return (uint8_t)ReplayGrade::Good;
    if (g == "BAD")                           return (uint8_t)ReplayGrade::Bad;
    return (uint8_t)ReplayGrade::Miss;
}
inline std::string u8_to_grade(uint8_t g) {
    switch ((ReplayGrade)g) {
        case ReplayGrade::Perfect:     return "PERFECT";
        case ReplayGrade::Good:        return "GOOD";
        case ReplayGrade::Bad:         return "BAD";
        case ReplayGrade::HoldStart:   return "PERFECT"; // approximate hold grade
        case ReplayGrade::HoldRelease: return "hold_release";
        default:                       return "MISS";
    }
}

// ---------- Writer ----------
struct ReplayWriter {
    std::vector<ReplayEvent> events;

    void record(float t, uint32_t note_idx, const std::string& grade) {
        events.push_back({t, note_idx, grade_to_u8(grade)});
    }

    bool save(const std::string& path, uint32_t chart_hash) const {
        // Build raw payload
        std::vector<uint8_t> raw;
        raw.reserve(22 + events.size() * 9);

        // Header
        raw.insert(raw.end(), REPLAY_MAGIC, REPLAY_MAGIC + 4);
        uint16_t ver = REPLAY_VERSION;
        raw.insert(raw.end(), (uint8_t*)&ver, (uint8_t*)&ver + 2);
        raw.insert(raw.end(), (uint8_t*)&chart_hash, (uint8_t*)&chart_hash + 4);
        uint64_t ts = (uint64_t)std::time(nullptr);
        raw.insert(raw.end(), (uint8_t*)&ts, (uint8_t*)&ts + 8);
        uint32_t n = (uint32_t)events.size();
        raw.insert(raw.end(), (uint8_t*)&n, (uint8_t*)&n + 4);

        // Events
        for (const auto& ev : events) {
            raw.insert(raw.end(), (uint8_t*)&ev.t, (uint8_t*)&ev.t + 4);
            raw.insert(raw.end(), (uint8_t*)&ev.note_idx, (uint8_t*)&ev.note_idx + 4);
            raw.push_back(ev.grade);
        }

        // CRC32 of uncompressed payload
        uint32_t crc = (uint32_t)mz_crc32(0, raw.data(), raw.size());

        // Compress
        mz_ulong out_len = mz_compressBound((mz_ulong)raw.size());
        std::vector<uint8_t> compressed(out_len);
        int res = mz_compress2(compressed.data(), &out_len,
                               raw.data(), (mz_ulong)raw.size(),
                               MZ_DEFAULT_COMPRESSION);
        if (res != MZ_OK) return false;
        compressed.resize(out_len);

        // Write: 4-byte uncompressed size + compressed data + crc32
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;
        uint32_t raw_size = (uint32_t)raw.size();
        f.write((char*)&raw_size, 4);
        f.write((char*)compressed.data(), (std::streamsize)compressed.size());
        f.write((char*)&crc, 4);
        return f.good();
    }
};

// ---------- Player ----------
struct ReplayPlayer {
    std::vector<ReplayEvent> events;
    int cursor = 0;

    bool enabled() const { return !events.empty(); }

    bool load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;

        // Read uncompressed size
        uint32_t raw_size = 0;
        f.read((char*)&raw_size, 4);
        if (!f || raw_size > 64 * 1024 * 1024) return false;

        // Read compressed body + CRC
        std::vector<uint8_t> compressed(
            (std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());
        if (compressed.size() < 4) return false;

        uint32_t stored_crc;
        std::memcpy(&stored_crc, compressed.data() + compressed.size() - 4, 4);
        compressed.resize(compressed.size() - 4);

        // Decompress
        std::vector<uint8_t> raw(raw_size);
        mz_ulong dest_len = raw_size;
        if (mz_uncompress(raw.data(), &dest_len,
                          compressed.data(), (mz_ulong)compressed.size()) != MZ_OK)
            return false;

        // Verify CRC
        uint32_t actual_crc = (uint32_t)mz_crc32(0, raw.data(), dest_len);
        if (actual_crc != stored_crc) return false;

        // Parse header
        if (dest_len < 22) return false;
        size_t off = 0;
        if (std::memcmp(raw.data(), REPLAY_MAGIC, 4) != 0) return false;
        off += 4;
        uint16_t ver; std::memcpy(&ver, raw.data() + off, 2); off += 2;
        off += 4;  // chart_hash
        off += 8;  // unix_ts
        uint32_t n; std::memcpy(&n, raw.data() + off, 4); off += 4;

        // Parse events
        events.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            if (off + 9 > dest_len) { events.resize(i); break; }
            std::memcpy(&events[i].t,        raw.data() + off, 4); off += 4;
            std::memcpy(&events[i].note_idx, raw.data() + off, 4); off += 4;
            events[i].grade = raw.data()[off++];
        }
        cursor = 0;
        return true;
    }

    // Apply all pending events up to time t.
    // on_judgment callback: (note_idx, t, grade_str) — for effect spawning.
    void tick(double t,
              const std::vector<Note>& notes,
              std::vector<NoteState>& states,
              engine::Judge& judge,
              const std::function<void(int, float, const std::string&)>& on_judgment = {})
    {
        while (cursor < (int)events.size() && events[cursor].t <= (float)t) {
            const auto& ev = events[cursor++];
            if (ev.note_idx >= states.size()) continue;
            auto& ns = states[ev.note_idx];
            if (ns.judged && ns.hold_finalized) continue;

            auto g = (ReplayGrade)ev.grade;
            if (g == ReplayGrade::HoldStart) {
                if (!ns.hit) judge.start_hold(ns, ev.t);
            } else if (g == ReplayGrade::HoldRelease) {
                if (ns.holding) {
                    ns.holding = false;
                    ns.released_early = true;
                    ns.release_t = ev.t;
                }
            } else {
                if (!ns.judged) {
                    judge.try_hit(ns, ev.t);
                }
            }
            if (on_judgment) on_judgment((int)ev.note_idx, ev.t, u8_to_grade(ev.grade));
        }
    }
};

// Compute a simple CRC32-based chart hash from the path string
inline uint32_t chart_path_hash(const std::string& path) {
    return (uint32_t)mz_crc32(0,
        (const uint8_t*)path.c_str(), path.size());
}

} // namespace phigros::io
