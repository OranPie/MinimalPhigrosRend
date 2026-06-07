package org.phigros.renderer

/** Mirrors phigros_mobile_touch_phase in mobile_bridge.h */
object TouchPhase {
    const val BEGAN     = 0
    const val MOVED     = 1
    const val ENDED     = 2
    const val CANCELLED = 3
}

/**
 * JNI wrapper around libphigros_jni.so.
 * All methods are thin forwarders — the C++ side holds the single global handle.
 */
object NativeBridge {

    init { System.loadLibrary("phigros_jni") }

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    external fun initialize(width: Int, height: Int)
    external fun loadChart(path: String): Boolean
    external fun restart()
    external fun getLastError(): String
    external fun extractChartZip(zipPath: String, destDir: String): Int

    // ── Play mode (call before loadChart / restart) ───────────────────────────
    /** mode: "autoplay" | "manual" | "scriptplay" */
    external fun setPlayMode(mode: String)
    external fun loadScript(path: String)

    // ── Playback control ──────────────────────────────────────────────────────
    external fun setPaused(paused: Boolean)
    external fun setTime(timeSeconds: Double)

    // ── Touch input (manual mode) ─────────────────────────────────────────────
    external fun onTouch(pointerId: Int, phase: Int, x: Float, y: Float, timestampMs: Long)

    // ── Per-frame game loop ───────────────────────────────────────────────────
    external fun tick(dtSeconds: Double)

    /**
     * Fill pre-allocated arrays with the most-recent frame snapshot.
     *
     * @param lineData   FloatArray[MAX_LINES * 7]  — x y rot alpha r g b (rgb normalised 0-1)
     * @param lineCount  IntArray[1]                — out: actual line count
     * @param noteData   FloatArray[MAX_NOTES * 12] — wx wy wx2 wy2 alpha lineRot sizePx r g b kind flags
     * @param noteCount  IntArray[1]                — out: actual note count
     * @param hudData    FloatArray[9]              — combo maxCombo score accuracy progress showCombo chartTime chartEnded totalNotes
     */
    external fun fillFrameArrays(
        lineData:  FloatArray, lineCount: IntArray,
        noteData:  FloatArray, noteCount: IntArray,
        hudData:   FloatArray
    )

    /**
     * Fill effect arrays from the most-recent frame snapshot.
     * Each effect is [x, y, t0, radius_start, radius_end, r, g, b, is_good] (EFFECT_STRIDE floats).
     *
     * @param effectData  FloatArray[MAX_EFFECTS * EFFECT_STRIDE] — out: effect data
     * @param effectCount IntArray[1]                             — out: actual effect count
     */
    external fun fillEffects(effectData: FloatArray, effectCount: IntArray)

    // ── Compile-time constants (must match mobile_bridge.h) ───────────────────
    const val MAX_LINES = 256
    const val MAX_NOTES = 2048
    const val LINE_STRIDE = 7
    const val NOTE_STRIDE = 12
    const val HUD_SIZE    = 9
    const val MAX_EFFECTS  = 128
    /** x y t0 radius_start radius_end r g b is_good */
    const val EFFECT_STRIDE = 9
}
