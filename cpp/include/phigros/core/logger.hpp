#pragma once
// phigros/core/logger.hpp — Verbose logging system for MinimalPhigrosRend.
//
// Features:
//   • Six log levels: Trace < Debug < Info < Warn < Error < Fatal
//   • Per-channel (subsystem) filtering: Chart, Render, Audio, Record, …
//   • Stream-style macros: PHLOG_INFO(Chart, "Loaded " << n << " notes")
//   • ANSI colour output (auto-detected via isatty, overridable)
//   • Optional wall-clock timestamps
//   • Optional secondary log-file output (in addition to stderr/stdout)
//   • Thread-safe — single mutex around the final write
//
// Usage:
//   PHLOG_INFO(Chart, "Loading: " << path);
//   PHLOG_WARN(Audio, "Not found — will run silent");
//   PHLOG_DEBUG(Engine, "t=" << t << " notes_window=" << idx_next);
//   PHLOG_TRACE(Input, "touch press at (" << x << "," << y << ")");
//
// Configuration (call once after parsing CLI args):
//   auto& log = phigros::core::Logger::get();
//   log.set_level(phigros::core::LogLevel::Debug);
//   log.set_channel_filter("chart,render,audio");  // empty = all channels
//   log.open_file("/tmp/phigros.log");
//   log.use_color = false;
//   log.show_time = true;

#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sstream>
#include <string>

// ── Platform terminal / colour detection ─────────────────────────────────────
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
#  define PHIGROS_LOG_COLOR_DEFAULT false
#else
#  include <unistd.h>
// Colour is enabled only when stderr (where WARN/ERROR go) is a real terminal.
// stdout-only logging also benefits, but stderr is the conservative choice.
#  define PHIGROS_LOG_COLOR_DEFAULT (isatty(STDERR_FILENO) != 0)
#endif

namespace phigros::core {

// ── Log level ─────────────────────────────────────────────────────────────────
enum class LogLevel : int {
    Trace = 0,  ///< Very detailed per-frame / per-event data
    Debug = 1,  ///< Diagnostic info useful during development
    Info  = 2,  ///< Normal operational messages (default minimum)
    Warn  = 3,  ///< Non-fatal issues, fallback behaviour
    Error = 4,  ///< Recoverable errors
    Fatal = 5,  ///< Unrecoverable errors
    Off   = 6,  ///< Suppress all output
};

// ── Log channel (subsystem) ───────────────────────────────────────────────────
enum class LogChannel : int {
    General     = 0,
    Chart       = 1,
    Render      = 2,
    Audio       = 3,
    Record      = 4,
    Engine      = 5,
    Input       = 6,
    Window      = 7,
    Respack     = 8,
    Compile     = 9,
    ChartScript = 10,
    Mod         = 11,
    Profile     = 12,
    _Count,
};
static constexpr int kChannelCount = static_cast<int>(LogChannel::_Count);

// ── Logger ────────────────────────────────────────────────────────────────────
class Logger {
public:
    LogLevel min_level        = LogLevel::Info;
    bool     use_color        = PHIGROS_LOG_COLOR_DEFAULT;
    bool     show_time        = false;
    bool     show_channel     = true;

    static Logger& get() {
        static Logger inst;
        return inst;
    }

    // ── Configuration helpers ─────────────────────────────────────────────────
    void set_level(LogLevel lv) noexcept { min_level = lv; }

    void set_level(const std::string& s) noexcept {
        if      (s == "trace") min_level = LogLevel::Trace;
        else if (s == "debug") min_level = LogLevel::Debug;
        else if (s == "info")  min_level = LogLevel::Info;
        else if (s == "warn")  min_level = LogLevel::Warn;
        else if (s == "error") min_level = LogLevel::Error;
        else if (s == "fatal") min_level = LogLevel::Fatal;
        else if (s == "off")   min_level = LogLevel::Off;
        // unrecognised → keep current level
    }

    /// Enable only the listed channels (comma-separated, case-insensitive).
    /// Pass an empty string to re-enable all channels.
    void set_channel_filter(const std::string& filter) noexcept {
        if (filter.empty()) {
            for (auto& e : ch_enabled_) e = true;
            return;
        }
        for (auto& e : ch_enabled_) e = false;
        _each_token(filter, ',', [this](const std::string& tok) {
            int idx = _channel_idx(tok);
            if (idx >= 0) ch_enabled_[idx] = true;
        });
    }

    /// Open an additional plain-text log file (appends).
    bool open_file(const std::string& path) noexcept {
        std::lock_guard<std::mutex> lk(mtx_);
        if (file_) { fclose(file_); file_ = nullptr; }
        if (path.empty()) return true;
        file_ = fopen(path.c_str(), "a");
        return file_ != nullptr;
    }

    // ── Query helpers ─────────────────────────────────────────────────────────
    bool should_log(LogLevel lv, LogChannel ch) const noexcept {
        return lv >= min_level && ch_enabled_[static_cast<int>(ch)];
    }

    // ── Write (called from macros via the RAII helper below) ─────────────────
    void write(LogLevel lv, LogChannel ch, const std::string& msg) {
        // Pre-filter without the lock for hot-path speed.
        if (!should_log(lv, ch)) return;

        std::lock_guard<std::mutex> lk(mtx_);

        FILE* dest = (lv >= LogLevel::Warn) ? stderr : stdout;

        // ANSI colour escape
        const char* col = "";
        const char* rst = "";
        if (use_color) {
            switch (lv) {
            case LogLevel::Trace: col = "\033[90m";   rst = "\033[0m"; break; // dark grey
            case LogLevel::Debug: col = "\033[36m";   rst = "\033[0m"; break; // cyan
            case LogLevel::Info:  col = "";            rst = "";        break; // default
            case LogLevel::Warn:  col = "\033[33m";   rst = "\033[0m"; break; // yellow
            case LogLevel::Error: col = "\033[31m";   rst = "\033[0m"; break; // red
            case LogLevel::Fatal: col = "\033[1;31m"; rst = "\033[0m"; break; // bold red
            default: break;
            }
        }

        // Build the prefix
        char prefix[64] = {};
        int  plen       = 0;

        if (show_time) {
            time_t now = time(nullptr);
            struct tm* tm_info = localtime(&now);
            plen += static_cast<int>(strftime(prefix + plen,
                sizeof(prefix) - static_cast<size_t>(plen), "[%H:%M:%S] ", tm_info));
        }
        if (show_channel && lv != LogLevel::Info) {
            plen += snprintf(prefix + plen,
                sizeof(prefix) - static_cast<size_t>(plen),
                "[%s/%s] ", _channel_name(ch), _level_name(lv));
        } else if (show_channel) {
            plen += snprintf(prefix + plen,
                sizeof(prefix) - static_cast<size_t>(plen),
                "[%s] ", _channel_name(ch));
        }

        fprintf(dest, "%s%s%s%s\n", col, prefix, msg.c_str(), rst);
        fflush(dest);

        if (file_) {
            fprintf(file_, "%s%s\n", prefix, msg.c_str());
            fflush(file_);
        }
    }

    // ── Static helpers for naming ─────────────────────────────────────────────
    static const char* level_name(LogLevel lv) noexcept { return _level_name(lv); }
    static const char* channel_name(LogChannel ch) noexcept { return _channel_name(ch); }

    static LogLevel level_from_string(const std::string& s) noexcept {
        if (s == "trace") return LogLevel::Trace;
        if (s == "debug") return LogLevel::Debug;
        if (s == "info")  return LogLevel::Info;
        if (s == "warn")  return LogLevel::Warn;
        if (s == "error") return LogLevel::Error;
        if (s == "fatal") return LogLevel::Fatal;
        if (s == "off")   return LogLevel::Off;
        return LogLevel::Info;
    }

    // Non-copyable, non-movable singleton
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() noexcept {
        for (auto& e : ch_enabled_) e = true;
    }
    ~Logger() noexcept {
        if (file_) fclose(file_);
    }

    bool  ch_enabled_[kChannelCount] = {};
    FILE* file_ = nullptr;
    std::mutex mtx_;

    static const char* _level_name(LogLevel lv) noexcept {
        switch (lv) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default:              return "?";
        }
    }

    static const char* _channel_name(LogChannel ch) noexcept {
        switch (ch) {
        case LogChannel::General:     return "General";
        case LogChannel::Chart:       return "Chart";
        case LogChannel::Render:      return "Render";
        case LogChannel::Audio:       return "Audio";
        case LogChannel::Record:      return "Record";
        case LogChannel::Engine:      return "Engine";
        case LogChannel::Input:       return "Input";
        case LogChannel::Window:      return "Window";
        case LogChannel::Respack:     return "Respack";
        case LogChannel::Compile:     return "Compile";
        case LogChannel::ChartScript: return "ChartScript";
        case LogChannel::Mod:         return "Mod";
        case LogChannel::Profile:     return "Profile";
        default:                      return "Unknown";
        }
    }

    static int _channel_idx(const std::string& s) noexcept {
        // Case-insensitive compare — use tolower for correct ASCII handling
        std::string lo = s;
        for (auto& c : lo) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lo == "general")     return static_cast<int>(LogChannel::General);
        if (lo == "chart")       return static_cast<int>(LogChannel::Chart);
        if (lo == "render")      return static_cast<int>(LogChannel::Render);
        if (lo == "audio")       return static_cast<int>(LogChannel::Audio);
        if (lo == "record")      return static_cast<int>(LogChannel::Record);
        if (lo == "engine")      return static_cast<int>(LogChannel::Engine);
        if (lo == "input")       return static_cast<int>(LogChannel::Input);
        if (lo == "window")      return static_cast<int>(LogChannel::Window);
        if (lo == "respack")     return static_cast<int>(LogChannel::Respack);
        if (lo == "compile")     return static_cast<int>(LogChannel::Compile);
        if (lo == "chartscript") return static_cast<int>(LogChannel::ChartScript);
        if (lo == "mod")         return static_cast<int>(LogChannel::Mod);
        if (lo == "profile")     return static_cast<int>(LogChannel::Profile);
        return -1;
    }

    template<typename Fn>
    static void _each_token(const std::string& s, char delim, Fn fn) {
        size_t start = 0;
        while (start < s.size()) {
            auto end = s.find(delim, start);
            std::string tok = s.substr(start, end == std::string::npos
                                               ? std::string::npos : end - start);
            // trim whitespace
            while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t')) tok.erase(tok.begin());
            while (!tok.empty() && (tok.back()  == ' ' || tok.back()  == '\t')) tok.pop_back();
            if (!tok.empty()) fn(tok);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
};

// ── RAII stream helper ────────────────────────────────────────────────────────
// Used by the macros below.  Building the message string only happens if the
// log entry passes the level+channel pre-filter, keeping hot-path cost minimal.
class LogStream {
public:
    LogStream(LogLevel lv, LogChannel ch) noexcept
        : lv_(lv), ch_(ch), active_(Logger::get().should_log(lv, ch)) {}

    ~LogStream() {
        if (active_) Logger::get().write(lv_, ch_, oss_.str());
    }

    template<typename T>
    LogStream& operator<<(const T& v) {
        if (active_) oss_ << v;
        return *this;
    }

    explicit operator bool() const noexcept { return active_; }

private:
    LogLevel         lv_;
    LogChannel       ch_;
    bool             active_;
    std::ostringstream oss_;
};

} // namespace phigros::core

// ── Public macros ─────────────────────────────────────────────────────────────
//
// Usage examples:
//   PHLOG_INFO(Chart, "Loaded " << n << " notes in " << ms << " ms");
//   PHLOG_WARN(Audio, "BGM not found at " << path << " — running silent");
//   PHLOG_DEBUG(Engine, "t=" << t << " idx_next=" << idx_next);
//   PHLOG_TRACE(Input, "finger " << id << " at (" << x << "," << y << ")");

#define PHLOG(ch_, lv_, expr_) \
    do { \
        ::phigros::core::LogStream _phls_( \
            ::phigros::core::LogLevel::lv_, \
            ::phigros::core::LogChannel::ch_); \
        if (_phls_) _phls_ << expr_; \
    } while (0)

#define PHLOG_TRACE(ch_, expr_) PHLOG(ch_, Trace, expr_)
#define PHLOG_DEBUG(ch_, expr_) PHLOG(ch_, Debug, expr_)
#define PHLOG_INFO(ch_, expr_)  PHLOG(ch_, Info,  expr_)
#define PHLOG_WARN(ch_, expr_)  PHLOG(ch_, Warn,  expr_)
#define PHLOG_ERROR(ch_, expr_) PHLOG(ch_, Error, expr_)
#define PHLOG_FATAL(ch_, expr_) PHLOG(ch_, Fatal, expr_)
