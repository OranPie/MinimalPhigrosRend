#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct phigros_mobile_handle phigros_mobile_handle;

typedef enum phigros_mobile_touch_phase {
    PHIGROS_MOBILE_TOUCH_BEGAN = 0,
    PHIGROS_MOBILE_TOUCH_MOVED = 1,
    PHIGROS_MOBILE_TOUCH_ENDED = 2,
    PHIGROS_MOBILE_TOUCH_CANCELLED = 3
} phigros_mobile_touch_phase;

typedef struct phigros_mobile_config {
    int window_width;
    int window_height;
    double chart_speed;
    double note_scale;
    double audio_offset_ms;
    const char* respack_path;
} phigros_mobile_config;

typedef struct phigros_mobile_state {
    int chart_loaded;
    int paused;
    int window_width;
    int window_height;
    int line_count;
    int total_notes;
    int playable_notes;
    int visible_notes;
    int judged_notes;
    int max_combo;
    int active_touch_count;
    double chart_time;
    double chart_offset;
    double chart_duration;
} phigros_mobile_state;

/* ── Frame snapshot structs (for Swift/Metal rendering) ─────────────────── */

#define PHIGROS_MAX_LINES  256
#define PHIGROS_MAX_NOTES 2048

/* Per-line state computed for one frame */
typedef struct phigros_line_data {
    int   lid;
    float x, y;          /* screen-space centre position */
    float rot;            /* rotation in radians */
    float alpha;          /* 0..1 opacity */
    float scale_x;
    float scale_y;
    uint8_t r, g, b;      /* line color */
    float incline;        /* perspective tilt degrees (RPE) */
    int   is_cover;       /* drawn over notes when true */
    int   z_order;
} phigros_line_data;

/* Per-note state computed for one frame */
typedef struct phigros_note_data {
    int   nid;
    int   kind;           /* 1=tap 2=drag 3=hold 4=flick */
    float wx,  wy;        /* head world position */
    float wx2, wy2;       /* tail world position (holds only) */
    float alpha;
    float line_rot;       /* owning line rotation (radians) for note orientation */
    float size_px;        /* size multiplier */
    float skew;           /* RPE skewControl degrees */
    uint8_t r, g, b;      /* tint color */
    int   judged;
    int   miss;
    int   holding;        /* hold note actively held */
    int   is_hold;
    int   draw_hold_head;
    int   hold_hit_failed;
    int   is_mh;          /* multi-hit (simultaneous) flag — selects *_mh texture */
} phigros_note_data;

/* HUD state */
typedef struct phigros_hud_data {
    int    combo;
    int    max_combo;
    int    score;
    int    total_notes;
    float  accuracy;      /* 0..1 */
    float  progress;      /* 0..1 */
    char   score_text[16];
    char   acc_text[16];
    int    show_combo;    /* true when combo >= 3 */
} phigros_hud_data;

/* Complete per-frame output; fill by calling phigros_mobile_get_frame() */
typedef struct phigros_frame_data {
    phigros_line_data  lines[PHIGROS_MAX_LINES];
    int                line_count;
    phigros_note_data  notes[PHIGROS_MAX_NOTES];
    int                note_count;
    phigros_hud_data   hud;
    double             chart_time;
    int                chart_ended; /* 1 when simulation past chart_end */

    /* Hit-flash effects (brief expanding rings at judged note positions) */
#define PHIGROS_MAX_EFFECTS 128
    struct {
        float x, y;           /* screen-space position */
        float t0;             /* birth time in chart seconds */
        float radius_start;
        float radius_end;
        uint8_t r, g, b;
        int    is_good;       /* 0 = perfect colour, 1 = good/bad colour */
    } effects[PHIGROS_MAX_EFFECTS];
    int effect_count;
} phigros_frame_data;

/* ── Core lifecycle ─────────────────────────────────────────────────────── */

phigros_mobile_handle* phigros_mobile_create(const phigros_mobile_config* config);
void phigros_mobile_destroy(phigros_mobile_handle* handle);

int phigros_mobile_load_chart(phigros_mobile_handle* handle,
                              const char* path,
                              const char* password);
int phigros_mobile_attach_surface(phigros_mobile_handle* handle,
                                  void* native_surface,
                                  int width,
                                  int height);
int phigros_mobile_set_time(phigros_mobile_handle* handle, double time_seconds);
int phigros_mobile_set_paused(phigros_mobile_handle* handle, int paused);
int phigros_mobile_restart(phigros_mobile_handle* handle);

/* ── Play mode ──────────────────────────────────────────────────────────── */

/* mode: "autoplay" | "manual" | "scriptplay" */
int phigros_mobile_set_play_mode(phigros_mobile_handle* handle, const char* mode);

/* Load a replay / script file for scriptplay mode (call before restart) */
int phigros_mobile_load_script(phigros_mobile_handle* handle, const char* path);

/* ── Per-frame game loop ─────────────────────────────────────────────────── */

/* Advance simulation by dt_seconds. When not paused, advances chart time,
   processes judgment (manual: queued touches; autoplay/scriptplay: virtual inputs),
   and rebuilds the cached FrameSnapshot used by phigros_mobile_get_frame.
   Call once per display-link frame from the Metal render thread. */
int phigros_mobile_tick(phigros_mobile_handle* handle, double dt_seconds);

/* Fill *out with the snapshot built by the last phigros_mobile_tick call.
   Thread-safe; always returns the most recent complete frame. */
int phigros_mobile_get_frame(phigros_mobile_handle* handle,
                             phigros_frame_data* out);

/* ── Input ──────────────────────────────────────────────────────────────── */

int phigros_mobile_on_touch(phigros_mobile_handle* handle,
                            int pointer_id,
                            phigros_mobile_touch_phase phase,
                            float x,
                            float y,
                            int64_t timestamp_ms);

/* ── State / error ──────────────────────────────────────────────────────── */

int phigros_mobile_get_state(phigros_mobile_handle* handle,
                             phigros_mobile_state* out_state);
int phigros_mobile_copy_last_error(const phigros_mobile_handle* handle,
                                   char* buffer,
                                   size_t buffer_size);

/* ── Zip extraction utility (no handle needed) ──────────────────────────── */

/* Extract .json and .phbc chart files from a zip archive into dest_dir.
   Returns the number of files extracted, or -1 on error.
   Skips path components — only the filename is written to dest_dir. */
int phigros_extract_chart_zip(const char* zip_path, const char* dest_dir);

/* Extract the full zip archive into dest_dir, preserving relative paths.
   Returns the number of extracted files, or -1 on error. */
int phigros_extract_zip_to_dir(const char* zip_path, const char* dest_dir);

#ifdef __cplusplus
}
#endif
