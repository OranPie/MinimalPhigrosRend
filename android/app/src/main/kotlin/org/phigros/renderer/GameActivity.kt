package org.phigros.renderer

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.*
import androidx.media3.common.MediaItem
import androidx.media3.exoplayer.ExoPlayer
import java.io.File
import kotlin.math.abs

/**
 * Fullscreen gameplay activity.
 * Extras: EXTRA_CHART (ChartEntry), EXTRA_PLAY_MODE (PlayMode.name), EXTRA_SCRIPT_PATH (String?)
 */
class GameActivity : ComponentActivity() {

    companion object {
        const val EXTRA_CHART       = "chart"
        const val EXTRA_PLAY_MODE   = "play_mode"
        const val EXTRA_SCRIPT_PATH = "script_path"
    }

    private lateinit var surfaceView: GameSurfaceView
    private var exoPlayer: ExoPlayer? = null
    private var audioOffsetMs: Float = 0f
    @Volatile private var isPaused = false

    private val hudState = mutableStateOf(HudSnapshot())
    private val handler  = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // True fullscreen — hide system bars
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_FULLSCREEN         or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION    or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY   or
            View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN  or
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        )

        val chart      = intent.getParcelableExtra<ChartEntry>(EXTRA_CHART)!!
        val playMode   = PlayMode.valueOf(intent.getStringExtra(EXTRA_PLAY_MODE) ?: "AUTOPLAY")
        val scriptPath = intent.getStringExtra(EXTRA_SCRIPT_PATH)

        // Read audio offset from settings
        audioOffsetMs = getSharedPreferences("phigros_settings", MODE_PRIVATE)
            .getFloat("audio_offset", 0f)

        // Init C++ bridge
        val dm = resources.displayMetrics
        NativeBridge.initialize(dm.widthPixels, dm.heightPixels)
        NativeBridge.setPlayMode(playMode.cMode)
        if (playMode == PlayMode.SCRIPTPLAY && scriptPath != null) {
            NativeBridge.loadScript(scriptPath)
        }
        NativeBridge.loadChart(chart.path)
        NativeBridge.setPaused(false)

        // Set up respack path
        surfaceView = GameSurfaceView(this).apply {
            respackZipPath = resolveRespack(chart.path)
        }

        // Set up audio
        resolveAudio(chart.path)?.let { audioFile ->
            exoPlayer = ExoPlayer.Builder(this).build().also { player ->
                player.setMediaItem(MediaItem.fromUri(android.net.Uri.fromFile(audioFile)))
                player.prepare()
            }
        }

        // Wire HUD updates → state + audio sync
        surfaceView.onHudUpdate = { snap ->
            hudState.value = snap
            val player = exoPlayer ?: return@onHudUpdate
            handler.post { syncAudio(snap.chartTime, snap.chartEnded) }
        }

        setContent {
            GameScreen(
                chart      = chart,
                surfaceView = surfaceView,
                hudState   = hudState,
                onQuit     = { finish() },
                onRestart  = {
                    isPaused = false
                    exoPlayer?.seekTo(0)
                    exoPlayer?.play()
                    NativeBridge.restart()
                    NativeBridge.setPaused(false)
                },
                onPauseChanged = { paused ->
                    isPaused = paused
                    if (paused) exoPlayer?.pause()
                    else exoPlayer?.play()
                }
            )
        }
    }

    private fun syncAudio(chartTime: Double, chartEnded: Boolean) {
        val player = exoPlayer ?: return
        if (chartEnded) { player.pause(); return }
        if (isPaused) { if (player.isPlaying) player.pause(); return }

        val targetMs = ((chartTime - audioOffsetMs / 1000.0) * 1000.0).toLong().coerceAtLeast(0L)
        if (!player.isPlaying) {
            player.seekTo(targetMs)
            player.play()
            return
        }
        val drift = targetMs - player.currentPosition
        if (abs(drift) > 80L) player.seekTo(targetMs)
    }

    /** Locate respack.zip: chart parent dir → app files dir → null (assets handled by GLSurfaceView) */
    private fun resolveRespack(chartPath: String): String? {
        val parentFile = File(chartPath).parentFile
        listOfNotNull(
            parentFile?.let { File(it, "respack.zip") },
            File(filesDir, "respack.zip")
        ).forEach { if (it.exists()) return it.absolutePath }
        return null
    }

    /** Locate audio: ogg → mp3 → wav in chart's parent directory. */
    private fun resolveAudio(chartPath: String): File? {
        val dir = File(chartPath).parentFile ?: return null
        for (ext in listOf("ogg", "mp3", "wav")) {
            dir.listFiles { f -> f.extension.equals(ext, ignoreCase = true) }
                ?.firstOrNull()?.let { return it }
        }
        return null
    }

    override fun onPause()  {
        super.onPause()
        surfaceView.onPause()
        NativeBridge.setPaused(true)
        exoPlayer?.pause()
    }

    override fun onResume() {
        super.onResume()
        surfaceView.onResume()
        if (!isPaused) {
            NativeBridge.setPaused(false)
            exoPlayer?.play()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        exoPlayer?.release()
        exoPlayer = null
    }

    @Deprecated("Use onBackPressedDispatcher")
    override fun onBackPressed() {
        // Intercept back — handled by pause menu
    }
}
