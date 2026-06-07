package org.phigros.renderer

import android.content.Context
import android.graphics.BitmapFactory
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.opengl.GLUtils
import android.view.MotionEvent
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.util.zip.ZipInputStream
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.math.*

/**
 * GLSurfaceView that owns the OpenGL ES 2.0 render loop.
 * Each frame: tick C++ → fill frame arrays + effects → render lines + notes + effects.
 */
class GameSurfaceView(context: Context) : GLSurfaceView(context) {

    /** Callback posted to GameActivity each GL frame. */
    var onHudUpdate: ((HudSnapshot) -> Unit)? = null

    /** Optional path to respack.zip on device storage (supplement to assets). */
    var respackZipPath: String? = null

    private val renderer = PhigrosGLRenderer()
    private var lastNanos = 0L

    init {
        setEGLContextClientVersion(2)
        setRenderer(renderer)
        renderMode = RENDERMODE_CONTINUOUSLY
        keepScreenOn = true
    }

    // ── Touch forwarding ──────────────────────────────────────────────────────
    override fun onTouchEvent(event: MotionEvent): Boolean {
        val ts     = event.eventTime
        val action = event.actionMasked
        val idx    = event.actionIndex
        val phase  = when (action) {
            MotionEvent.ACTION_DOWN,
            MotionEvent.ACTION_POINTER_DOWN -> TouchPhase.BEGAN
            MotionEvent.ACTION_MOVE         -> TouchPhase.MOVED
            MotionEvent.ACTION_UP,
            MotionEvent.ACTION_POINTER_UP   -> TouchPhase.ENDED
            else                            -> TouchPhase.CANCELLED
        }
        if (action == MotionEvent.ACTION_MOVE) {
            for (i in 0 until event.pointerCount) {
                NativeBridge.onTouch(event.getPointerId(i), TouchPhase.MOVED,
                    event.getX(i), event.getY(i), ts)
            }
        } else {
            NativeBridge.onTouch(event.getPointerId(idx), phase,
                event.getX(idx), event.getY(idx), ts)
        }
        return true
    }

    // ── Tick called from renderer (GL thread) ─────────────────────────────────
    fun tick() {
        val now = System.nanoTime()
        val dt  = if (lastNanos == 0L) 1.0 / 60.0
                  else (now - lastNanos).coerceAtMost(100_000_000L) / 1_000_000_000.0
        lastNanos = now
        NativeBridge.tick(dt)
        val f = renderer.frame
        NativeBridge.fillFrameArrays(
            f.lineData, f.lineCount,
            f.noteData, f.noteCount,
            f.hudData
        )
        NativeBridge.fillEffects(f.effectData, f.effectCount)
        onHudUpdate?.invoke(HudSnapshot(
            combo      = f.combo,
            maxCombo   = f.maxCombo,
            score      = f.score,
            accuracy   = f.accuracy,
            progress   = f.progress,
            showCombo  = f.showCombo,
            chartEnded = f.chartEnded,
            chartTime  = f.chartTime
        ))
    }

    // ── Inner GL renderer ─────────────────────────────────────────────────────
    inner class PhigrosGLRenderer : Renderer {

        val frame = FrameArrays()

        private var sw = 1f
        private var sh = 1f
        private var prog = 0

        // Cached uniform / attribute locations (0 = uninit, -1 = not found)
        private var locAPos   = -1; private var locAUV    = -1
        private var locUSW    = -1; private var locUSH    = -1
        private var locUCx    = -1; private var locUCy    = -1
        private var locUCosA  = -1; private var locUSinA  = -1
        private var locUSx    = -1; private var locUSy    = -1
        private var locUColor = -1; private var locUMode  = -1
        private var locUTex   = -1; private var locURingInner = -1

        // Respack textures: kind(1..4) → texId, +10 offset for MH variants
        private val noteTextures = mutableMapOf<Int, Int>()
        private var hitFxTex = 0

        // Quad: 4 verts × (x y u v) — unit square centred at origin
        private val quadBuf: FloatBuffer = ByteBuffer
            .allocateDirect(4 * 4 * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer().apply {
                put(floatArrayOf(
                    -0.5f,-0.5f, 0f,1f,
                     0.5f,-0.5f, 1f,1f,
                    -0.5f, 0.5f, 0f,0f,
                     0.5f, 0.5f, 1f,0f))
                rewind()
            }

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            GLES20.glClearColor(0.04f, 0.04f, 0.08f, 1f)
            GLES20.glEnable(GLES20.GL_BLEND)
            GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA)
            prog = buildShaderProgram()
            cacheLocations()
            loadRespackTextures()
        }

        override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
            GLES20.glViewport(0, 0, width, height)
            sw = width.toFloat(); sh = height.toFloat()
        }

        override fun onDrawFrame(gl: GL10?) {
            tick()
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
            GLES20.glUseProgram(prog)

            GLES20.glUniform1f(locUSW, sw)
            GLES20.glUniform1f(locUSH, sh)
            GLES20.glUniform1i(locUTex, 0)  // texture unit 0

            GLES20.glEnableVertexAttribArray(locAPos)
            GLES20.glEnableVertexAttribArray(locAUV)

            val stride = 4 * 4 // 4 floats × 4 bytes
            quadBuf.rewind()
            GLES20.glVertexAttribPointer(locAPos, 2, GLES20.GL_FLOAT, false, stride, quadBuf)
            val uvBuf = quadBuf.duplicate().apply { position(2) }
            GLES20.glVertexAttribPointer(locAUV, 2, GLES20.GL_FLOAT, false, stride, uvBuf)

            // Draw lines
            val lc = frame.lineCount[0].coerceAtMost(NativeBridge.MAX_LINES)
            for (i in 0 until lc) {
                val b     = i * NativeBridge.LINE_STRIDE
                val alpha = frame.lineData[b + 3].coerceIn(0f, 1f)
                if (alpha < 0.01f) continue
                setTransform(frame.lineData[b + 0], frame.lineData[b + 1],
                    cos(frame.lineData[b + 2].toDouble()).toFloat(),
                    sin(frame.lineData[b + 2].toDouble()).toFloat(),
                    sw * 0.65f, 4f)
                setColor(frame.lineData[b + 4], frame.lineData[b + 5], frame.lineData[b + 6], alpha)
                setMode(0)
                GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
            }

            // Draw notes
            val nc = frame.noteCount[0].coerceAtMost(NativeBridge.MAX_NOTES)
            for (i in 0 until nc) {
                drawNote(i)
            }

            // Draw hit-flash effects
            val ec = frame.effectCount[0].coerceAtMost(NativeBridge.MAX_EFFECTS)
            val chartTime = frame.chartTime.toFloat()
            for (i in 0 until ec) {
                drawEffect(i, chartTime)
            }

            GLES20.glDisableVertexAttribArray(locAPos)
            GLES20.glDisableVertexAttribArray(locAUV)
        }

        // ── Note drawing ──────────────────────────────────────────────────────
        private fun drawNote(i: Int) {
            val b     = i * NativeBridge.NOTE_STRIDE
            val alpha = frame.noteData[b + 4].coerceIn(0f, 1f)
            if (alpha < 0.01f) return
            val flags  = frame.noteData[b + 11].toInt()
            val isHold = (flags and FrameArrays.FLAG_IS_HOLD)       != 0
            val miss   = (flags and FrameArrays.FLAG_MISS)           != 0
            val dhh    = (flags and FrameArrays.FLAG_DRAW_HOLD_HEAD) != 0
            val failed = (flags and FrameArrays.FLAG_HOLD_FAILED)    != 0
            val isMH   = (flags and FrameArrays.FLAG_MH)             != 0
            val kind   = frame.noteData[b + 10].toInt()
            val wx     = frame.noteData[b + 0]; val wy  = frame.noteData[b + 1]
            val wx2    = frame.noteData[b + 2]; val wy2 = frame.noteData[b + 3]
            val rot    = frame.noteData[b + 5]
            val siz    = frame.noteData[b + 6]
            val r      = frame.noteData[b + 7]; val g = frame.noteData[b + 8]; val bl = frame.noteData[b + 9]
            val a      = if (miss) alpha * 0.4f else alpha
            val noteW  = 0.06f * sw * siz

            val texKey = if (isMH) kind + 10 else kind
            val texId  = noteTextures[texKey] ?: noteTextures[kind] ?: 0

            if (isHold) {
                val dx  = wx2 - wx; val dy = wy2 - wy
                val len = sqrt((dx * dx + dy * dy).toDouble()).toFloat()
                if (len > 1f) {
                    val ta  = if (miss || failed) a * 0.4f else a
                    val ang = atan2(dy.toDouble(), dx.toDouble()).toFloat() - (Math.PI / 2).toFloat()
                    drawRect(
                        (wx + wx2) / 2f, (wy + wy2) / 2f,
                        noteW * 0.55f, len, ang, r, g, bl, ta, texId = 0)
                }
                if (dhh) drawRect(wx, wy, noteW, noteW * 0.3f, rot, r, g, bl, a, texId = texId)
            } else {
                val noteH = if (texId != 0) noteW * 0.25f else if (kind == 2) noteW * 0.18f else noteW * 0.3f
                drawRect(wx, wy, noteW, noteH, rot, r, g, bl, a, texId = texId)
            }
        }

        // ── Effect drawing ────────────────────────────────────────────────────
        private fun drawEffect(i: Int, chartTime: Float) {
            val b       = i * NativeBridge.EFFECT_STRIDE
            val x       = frame.effectData[b + 0]
            val y       = frame.effectData[b + 1]
            val t0      = frame.effectData[b + 2]
            val rStart  = frame.effectData[b + 3]
            val rEnd    = frame.effectData[b + 4]
            val r       = frame.effectData[b + 5]
            val g       = frame.effectData[b + 6]
            val bl      = frame.effectData[b + 7]
            val progress = (chartTime - t0) / 0.18f
            if (progress < 0f || progress > 1f) return
            val alpha  = 1f - progress
            val radius = rStart + (rEnd - rStart) * progress

            if (hitFxTex != 0) {
                drawRect(x, y, radius * 2f, radius * 2f, 0f, r, g, bl, alpha, texId = hitFxTex)
            } else {
                // Ring: draw as colored disc with ring shader
                setTransform(x, y, 1f, 0f, radius * 2f, radius * 2f)
                setColor(r, g, bl, alpha)
                setMode(2)
                GLES20.glUniform1f(locURingInner, 0.55f)
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
                GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
            }
        }

        // ── Helpers ───────────────────────────────────────────────────────────
        private fun drawRect(cx: Float, cy: Float, w: Float, h: Float, rotation: Float,
                             r: Float, g: Float, b: Float, a: Float, texId: Int = 0) {
            setTransform(cx, cy, cos(rotation.toDouble()).toFloat(), sin(rotation.toDouble()).toFloat(), w, h)
            setColor(r, g, b, a)
            if (texId != 0) {
                setMode(1)
                GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texId)
            } else {
                setMode(0)
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
            }
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        }

        private fun setTransform(cx: Float, cy: Float, cosA: Float, sinA: Float, sx: Float, sy: Float) {
            GLES20.glUniform1f(locUCx, cx); GLES20.glUniform1f(locUCy, cy)
            GLES20.glUniform1f(locUCosA, cosA); GLES20.glUniform1f(locUSinA, sinA)
            GLES20.glUniform1f(locUSx, sx); GLES20.glUniform1f(locUSy, sy)
        }
        private fun setColor(r: Float, g: Float, b: Float, a: Float) {
            GLES20.glUniform4f(locUColor, r, g, b, a)
        }
        private fun setMode(mode: Int) {
            GLES20.glUniform1i(locUMode, mode)
            if (mode != 2) GLES20.glUniform1f(locURingInner, 0f)
        }

        // ── Respack texture loading (called on GL thread in onSurfaceCreated) ─
        private fun loadRespackTextures() {
            // Try assets first, then filesystem path
            val stream = runCatching {
                context.assets.open("respack.zip")
            }.getOrNull() ?: respackZipPath?.let {
                runCatching { java.io.File(it).inputStream() }.getOrNull()
            } ?: return

            val images = mapOf(
                "click.png"    to 1,  "drag.png"     to 2,
                "flick.png"    to 4,  "hold.png"     to 3,
                "click_mh.png" to 11, "drag_mh.png"  to 12,
                "flick_mh.png" to 14, "hold_mh.png"  to 13,
                "hit_fx.png"   to -1
            )

            stream.use { raw ->
                ZipInputStream(raw).use { zip ->
                    var entry = zip.nextEntry
                    while (entry != null) {
                        val name = entry.name.substringAfterLast('/')
                        val key  = images[name]
                        if (key != null) {
                            val bitmap = BitmapFactory.decodeStream(zip)
                            if (bitmap != null) {
                                val ids = IntArray(1)
                                GLES20.glGenTextures(1, ids, 0)
                                val id = ids[0]
                                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, id)
                                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
                                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
                                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
                                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
                                GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, bitmap, 0)
                                bitmap.recycle()
                                if (key == -1) hitFxTex = id else noteTextures[key] = id
                            }
                        }
                        zip.closeEntry()
                        entry = zip.nextEntry
                    }
                }
            }
        }

        // ── Shader / program setup ─────────────────────────────────────────────
        private fun cacheLocations() {
            locAPos       = GLES20.glGetAttribLocation(prog,  "aPos")
            locAUV        = GLES20.glGetAttribLocation(prog,  "aUV")
            locUSW        = GLES20.glGetUniformLocation(prog, "uSW")
            locUSH        = GLES20.glGetUniformLocation(prog, "uSH")
            locUCx        = GLES20.glGetUniformLocation(prog, "uCx")
            locUCy        = GLES20.glGetUniformLocation(prog, "uCy")
            locUCosA      = GLES20.glGetUniformLocation(prog, "uCosA")
            locUSinA      = GLES20.glGetUniformLocation(prog, "uSinA")
            locUSx        = GLES20.glGetUniformLocation(prog, "uSx")
            locUSy        = GLES20.glGetUniformLocation(prog, "uSy")
            locUColor     = GLES20.glGetUniformLocation(prog, "uColor")
            locUMode      = GLES20.glGetUniformLocation(prog, "uMode")
            locUTex       = GLES20.glGetUniformLocation(prog, "uTex")
            locURingInner = GLES20.glGetUniformLocation(prog, "uRingInner")
        }

        private fun buildShaderProgram(): Int {
            val vs = compileShader(GLES20.GL_VERTEX_SHADER, VERT_SRC)
            val fs = compileShader(GLES20.GL_FRAGMENT_SHADER, FRAG_SRC)
            return GLES20.glCreateProgram().also {
                GLES20.glAttachShader(it, vs)
                GLES20.glAttachShader(it, fs)
                GLES20.glLinkProgram(it)
            }
        }

        private fun compileShader(type: Int, src: String): Int =
            GLES20.glCreateShader(type).also {
                GLES20.glShaderSource(it, src)
                GLES20.glCompileShader(it)
            }
    }

    companion object {
        private val VERT_SRC = """
            attribute vec2 aPos;
            attribute vec2 aUV;
            uniform float uSW, uSH;
            uniform float uCx, uCy;
            uniform float uCosA, uSinA;
            uniform float uSx, uSy;
            varying vec2 vUV;
            void main() {
                vec2 local = vec2(aPos.x * uSx, aPos.y * uSy);
                vec2 rot   = vec2(uCosA * local.x - uSinA * local.y,
                                  uSinA * local.x + uCosA * local.y);
                vec2 world = rot + vec2(uCx, uCy);
                gl_Position = vec4(
                    world.x / uSW * 2.0 - 1.0,
                    1.0 - world.y / uSH * 2.0,
                    0.0, 1.0);
                vUV = aUV;
            }
        """.trimIndent()

        private val FRAG_SRC = """
            precision mediump float;
            varying vec2 vUV;
            uniform vec4 uColor;
            uniform sampler2D uTex;
            uniform int uMode;          /* 0=color 1=textured 2=ring */
            uniform float uRingInner;   /* inner-radius fraction for ring mode */
            void main() {
                if (uMode == 2) {
                    vec2 c = vUV * 2.0 - 1.0;
                    float d = length(c);
                    if (d > 1.0 || d < uRingInner) discard;
                    gl_FragColor = uColor;
                } else if (uMode == 1) {
                    gl_FragColor = texture2D(uTex, vUV) * uColor;
                } else {
                    gl_FragColor = uColor;
                }
            }
        """.trimIndent()
    }
}
