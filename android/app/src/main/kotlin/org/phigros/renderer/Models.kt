package org.phigros.renderer

import android.net.Uri
import android.os.Parcelable
import kotlinx.parcelize.Parcelize

enum class PlayMode(val cMode: String, val label: String, val description: String) {
    AUTOPLAY(
        "autoplay",
        "Autoplay",
        "Perfect autoplay simulation — watch the chart play itself."
    ),
    MANUAL(
        "manual",
        "Manual",
        "Touch to hit notes. Judgment is spatial + temporal."
    ),
    SCRIPTPLAY(
        "scriptplay",
        "Scriptplay",
        "Replay from a .phr script file."
    )
}

@Parcelize
data class ChartEntry(
    val id: String,            // stable: filename e.g. "mysong.json"
    val displayName: String,
    val path: String           // absolute path on device
) : Parcelable

/**
 * Pre-allocated frame arrays reused every tick to avoid GC pressure.
 * Access only from the game-loop thread.
 */
class FrameArrays {
    val lineData  = FloatArray(NativeBridge.MAX_LINES  * NativeBridge.LINE_STRIDE)
    val lineCount = IntArray(1)
    val noteData  = FloatArray(NativeBridge.MAX_NOTES  * NativeBridge.NOTE_STRIDE)
    val noteCount = IntArray(1)
    val hudData   = FloatArray(NativeBridge.HUD_SIZE)
    val effectData  = FloatArray(NativeBridge.MAX_EFFECTS * NativeBridge.EFFECT_STRIDE)
    val effectCount = IntArray(1)

    // Convenience accessors for HUD fields
    val combo:       Int   get() = hudData[0].toInt()
    val maxCombo:    Int   get() = hudData[1].toInt()
    val score:       Int   get() = hudData[2].toInt()
    val accuracy:    Float get() = hudData[3]
    val progress:    Float get() = hudData[4]
    val showCombo:   Boolean get() = hudData[5] != 0f
    val chartTime:   Double  get() = hudData[6].toDouble()
    val chartEnded:  Boolean get() = hudData[7] != 0f
    val totalNotes:  Int   get() = hudData[8].toInt()

    // Note flag bits (match JNI encoding in mobile_bridge_jni.cpp)
    companion object {
        const val FLAG_IS_HOLD       = 1
        const val FLAG_JUDGED        = 2
        const val FLAG_MISS          = 4
        const val FLAG_HOLDING       = 8
        const val FLAG_DRAW_HOLD_HEAD = 16
        const val FLAG_HOLD_FAILED   = 32
        const val FLAG_MH            = 64
    }
}

/** Immutable snapshot posted to UI thread each frame. */
data class HudSnapshot(
    val combo:      Int     = 0,
    val maxCombo:   Int     = 0,
    val score:      Int     = 0,
    val accuracy:   Float   = 1f,
    val progress:   Float   = 0f,
    val showCombo:  Boolean = false,
    val chartEnded: Boolean = false,
    val chartTime:  Double  = 0.0
) {
    val scoreText: String get() = "%07d".format(score)
    val accText:   String get() = "%.2f%%".format(accuracy * 100f)

    val grade: String get() = when {
        accuracy >= 1f                          -> "AP"
        maxCombo == combo && accuracy >= 0.96f  -> "FC"
        accuracy >= 0.96f                       -> "V"
        accuracy >= 0.70f                       -> "F"
        else                                    -> "P"
    }
}
