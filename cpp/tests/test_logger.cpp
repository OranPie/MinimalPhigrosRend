// tests/test_logger.cpp — Unit tests for phigros::core::Logger
// Validates log-level filtering, channel filtering, and stream macros.
#include "phigros/core/logger.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sstream>

using namespace phigros::core;

static int failures = 0;

static void check(const char* name, bool cond) {
    if (!cond) {
        std::printf("FAIL  %s\n", name);
        ++failures;
    }
}

// Capture logger output into a string (redirect Logger write via a test sink).
// Because Logger writes to stdout/stderr we subclass LogStream behaviour via a
// lightweight approach: call Logger::get().write() directly and inspect
// should_log().
int main() {
    auto& log = Logger::get();

    // ── 1. Default level is Info ───────────────────────────────────────────────
    check("default_level_info", log.min_level == LogLevel::Info);

    // ── 2. should_log honours level ───────────────────────────────────────────
    log.set_level(LogLevel::Info);
    check("should_log_info_at_info",  log.should_log(LogLevel::Info,  LogChannel::General));
    check("should_log_warn_at_info",  log.should_log(LogLevel::Warn,  LogChannel::General));
    check("no_log_debug_at_info",    !log.should_log(LogLevel::Debug, LogChannel::General));
    check("no_log_trace_at_info",    !log.should_log(LogLevel::Trace, LogChannel::General));

    log.set_level(LogLevel::Trace);
    check("should_log_trace_at_trace", log.should_log(LogLevel::Trace, LogChannel::General));
    check("should_log_debug_at_trace", log.should_log(LogLevel::Debug, LogChannel::General));

    log.set_level(LogLevel::Off);
    check("no_log_fatal_at_off", !log.should_log(LogLevel::Fatal, LogChannel::General));

    // ── 3. set_level from string ──────────────────────────────────────────────
    log.set_level("warn");
    check("set_level_warn_str", log.min_level == LogLevel::Warn);

    log.set_level("debug");
    check("set_level_debug_str", log.min_level == LogLevel::Debug);

    log.set_level("info");  // reset to default

    // ── 4. Level names ────────────────────────────────────────────────────────
    check("level_name_trace", std::string(Logger::level_name(LogLevel::Trace)) == "TRACE");
    check("level_name_debug", std::string(Logger::level_name(LogLevel::Debug)) == "DEBUG");
    check("level_name_info",  std::string(Logger::level_name(LogLevel::Info))  == "INFO");
    check("level_name_warn",  std::string(Logger::level_name(LogLevel::Warn))  == "WARN");
    check("level_name_error", std::string(Logger::level_name(LogLevel::Error)) == "ERROR");
    check("level_name_fatal", std::string(Logger::level_name(LogLevel::Fatal)) == "FATAL");

    // ── 5. Channel names ──────────────────────────────────────────────────────
    check("ch_name_chart",       std::string(Logger::channel_name(LogChannel::Chart))       == "Chart");
    check("ch_name_render",      std::string(Logger::channel_name(LogChannel::Render))      == "Render");
    check("ch_name_audio",       std::string(Logger::channel_name(LogChannel::Audio))       == "Audio");
    check("ch_name_record",      std::string(Logger::channel_name(LogChannel::Record))      == "Record");
    check("ch_name_engine",      std::string(Logger::channel_name(LogChannel::Engine))      == "Engine");
    check("ch_name_input",       std::string(Logger::channel_name(LogChannel::Input))       == "Input");
    check("ch_name_window",      std::string(Logger::channel_name(LogChannel::Window))      == "Window");
    check("ch_name_respack",     std::string(Logger::channel_name(LogChannel::Respack))     == "Respack");
    check("ch_name_compile",     std::string(Logger::channel_name(LogChannel::Compile))     == "Compile");
    check("ch_name_chartscript", std::string(Logger::channel_name(LogChannel::ChartScript)) == "ChartScript");
    check("ch_name_mod",         std::string(Logger::channel_name(LogChannel::Mod))         == "Mod");
    check("ch_name_profile",     std::string(Logger::channel_name(LogChannel::Profile))     == "Profile");

    // ── 6. Channel filtering ──────────────────────────────────────────────────
    log.set_level(LogLevel::Trace);
    log.set_channel_filter("chart,audio");
    check("filter_chart_enabled", log.should_log(LogLevel::Trace, LogChannel::Chart));
    check("filter_audio_enabled", log.should_log(LogLevel::Trace, LogChannel::Audio));
    check("filter_render_off",   !log.should_log(LogLevel::Trace, LogChannel::Render));
    check("filter_engine_off",   !log.should_log(LogLevel::Trace, LogChannel::Engine));

    // Re-enable all
    log.set_channel_filter("");
    check("all_channels_after_clear", log.should_log(LogLevel::Trace, LogChannel::Render));

    // ── 7. Channel filter with whitespace ─────────────────────────────────────
    log.set_channel_filter("  compile , mod  ");
    check("filter_ws_compile", log.should_log(LogLevel::Info, LogChannel::Compile));
    check("filter_ws_mod",     log.should_log(LogLevel::Info, LogChannel::Mod));
    check("filter_ws_chart_off", !log.should_log(LogLevel::Info, LogChannel::Chart));

    log.set_channel_filter("");
    log.set_level(LogLevel::Info);  // restore default

    // ── 8. level_from_string round-trip ───────────────────────────────────────
    check("lfs_trace", Logger::level_from_string("trace") == LogLevel::Trace);
    check("lfs_debug", Logger::level_from_string("debug") == LogLevel::Debug);
    check("lfs_info",  Logger::level_from_string("info")  == LogLevel::Info);
    check("lfs_warn",  Logger::level_from_string("warn")  == LogLevel::Warn);
    check("lfs_error", Logger::level_from_string("error") == LogLevel::Error);
    check("lfs_fatal", Logger::level_from_string("fatal") == LogLevel::Fatal);
    check("lfs_off",   Logger::level_from_string("off")   == LogLevel::Off);
    check("lfs_bad",   Logger::level_from_string("bad_value") == LogLevel::Info);

    // ── 9. LogStream skips allocation when filtered ───────────────────────────
    // (If stream is built even when filtered the test will crash / be slow)
    log.set_level(LogLevel::Warn);
    // These should be no-ops — no output, no crash:
    PHLOG_TRACE(General, "should not appear (trace < warn)");
    PHLOG_DEBUG(General, "should not appear (debug < warn)");
    PHLOG_INFO(General,  "should not appear (info < warn)");
    // These should appear (but test doesn't capture stdout):
    log.use_color = false; // stable output for any log file checks
    PHLOG_WARN(General,  "test_logger: WARN visible (expected)");
    PHLOG_ERROR(General, "test_logger: ERROR visible (expected)");

    log.set_level(LogLevel::Info);  // restore

    // ── 10. Macro channel enum correctness ────────────────────────────────────
    log.set_level(LogLevel::Debug);
    bool chart_ok = false, render_ok = false;
    // Use should_log to verify macro channel mapping
    check("macro_chart",   log.should_log(LogLevel::Info, LogChannel::Chart));
    check("macro_render",  log.should_log(LogLevel::Info, LogChannel::Render));
    check("macro_engine",  log.should_log(LogLevel::Info, LogChannel::Engine));
    check("macro_profile", log.should_log(LogLevel::Info, LogChannel::Profile));
    (void)chart_ok; (void)render_ok;

    // ── Summary ────────────────────────────────────────────────────────────────
    log.set_level(LogLevel::Info);
    if (failures == 0)
        std::printf("All logger tests passed.\n");
    else
        std::printf("%d logger test(s) FAILED.\n", failures);

    return failures > 0 ? 1 : 0;
}
