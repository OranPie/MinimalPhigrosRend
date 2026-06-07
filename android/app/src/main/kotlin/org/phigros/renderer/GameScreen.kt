package org.phigros.renderer

import android.view.ViewGroup
import androidx.compose.animation.*
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView

// ── Root gameplay screen ──────────────────────────────────────────────────────

@Composable
fun GameScreen(
    chart:           ChartEntry,
    surfaceView:     GameSurfaceView,
    hudState:        State<HudSnapshot>,
    onQuit:          () -> Unit,
    onRestart:       () -> Unit,
    onPauseChanged:  (Boolean) -> Unit = {}
) {
    val hud        by hudState
    var paused     by remember { mutableStateOf(false) }
    var showResult by remember { mutableStateOf(false) }

    // Detect chart end
    LaunchedEffect(hud.chartEnded) {
        if (hud.chartEnded) showResult = true
    }
    // Propagate pause state to caller for audio sync
    LaunchedEffect(paused) { onPauseChanged(paused) }

    Box(Modifier.fillMaxSize()) {

        // GL surface (bottom layer)
        AndroidView(
            factory = {
                surfaceView.apply {
                    layoutParams = ViewGroup.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT
                    )
                }
            },
            modifier = Modifier.fillMaxSize()
        )

        // HUD overlay
        if (!paused && !showResult) {
            GameHud(
                hud    = hud,
                onPause = {
                    paused = true
                    NativeBridge.setPaused(true)
                }
            )
        }

        // Pause menu
        AnimatedVisibility(
            visible = paused,
            enter = fadeIn(), exit = fadeOut()
        ) {
            PauseOverlay(
                onResume = {
                    paused = false
                    NativeBridge.setPaused(false)
                },
                onRestart = {
                    paused = false
                    showResult = false
                    onRestart()
                },
                onQuit = onQuit
            )
        }

        // Result screen
        AnimatedVisibility(
            visible = showResult,
            enter = fadeIn() + scaleIn(initialScale = 0.9f),
            exit  = fadeOut()
        ) {
            ResultOverlay(
                hud       = hud,
                chartName = chart.displayName,
                onRetry   = {
                    showResult = false
                    onRestart()
                },
                onQuit = onQuit
            )
        }
    }
}

// ── HUD ───────────────────────────────────────────────────────────────────────

@Composable
fun GameHud(hud: HudSnapshot, onPause: () -> Unit) {
    Box(Modifier.fillMaxSize()) {
        // Progress bar — top edge
        LinearProgressIndicator(
            progress   = { hud.progress },
            modifier   = Modifier.fillMaxWidth().height(3.dp).align(Alignment.TopCenter),
            color      = Color.White,
            trackColor = Color.White.copy(alpha = 0.2f)
        )

        // Top row: pause | accuracy | score
        Row(
            Modifier.fillMaxWidth().padding(12.dp).align(Alignment.TopCenter),
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = onPause) {
                Text("⏸", fontSize = 20.sp, color = Color.White)
            }
            Spacer(Modifier.width(8.dp))
            Text(
                hud.accText,
                color = Color.White,
                fontSize = 14.sp,
                fontWeight = FontWeight.SemiBold,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
            )
            Spacer(Modifier.weight(1f))
            Text(
                hud.scoreText,
                color = Color.White,
                fontSize = 26.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
            )
        }

        // Centre combo
        if (hud.showCombo) {
            Column(
                Modifier.align(Alignment.Center),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    "${hud.combo}",
                    color = Color.White,
                    fontSize = 64.sp,
                    fontWeight = FontWeight.Black
                )
                Text(
                    "COMBO",
                    color = Color.White.copy(alpha = 0.8f),
                    fontSize = 14.sp,
                    fontWeight = FontWeight.SemiBold,
                    letterSpacing = 4.sp
                )
            }
        }
    }
}

// ── Pause overlay ─────────────────────────────────────────────────────────────

@Composable
fun PauseOverlay(onResume: () -> Unit, onRestart: () -> Unit, onQuit: () -> Unit) {
    Box(
        Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.6f)),
        contentAlignment = Alignment.Center
    ) {
        Surface(
            shape  = RoundedCornerShape(24.dp),
            color  = MaterialTheme.colorScheme.surface.copy(alpha = 0.92f),
            tonalElevation = 8.dp,
            modifier = Modifier.widthIn(max = 300.dp)
        ) {
            Column(
                Modifier.padding(32.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Text("Paused", fontSize = 28.sp, fontWeight = FontWeight.Bold)
                HorizontalDivider()
                PauseBtn("▶  Resume",  onClick = onResume)
                PauseBtn("↺  Restart", onClick = onRestart)
                PauseBtn("✕  Quit",    onClick = onQuit, destructive = true)
            }
        }
    }
}

@Composable
private fun PauseBtn(label: String, onClick: () -> Unit, destructive: Boolean = false) {
    Button(
        onClick  = onClick,
        modifier = Modifier.fillMaxWidth(),
        colors   = if (destructive)
            ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)
        else
            ButtonDefaults.buttonColors()
    ) { Text(label, fontSize = 16.sp) }
}

// ── Result overlay ────────────────────────────────────────────────────────────

@Composable
fun ResultOverlay(
    hud: HudSnapshot, chartName: String,
    onRetry: () -> Unit, onQuit: () -> Unit
) {
    val gradeColor = when (hud.grade) {
        "AP" -> Color(0xFFFFD700)
        "FC" -> Color(0xFF00CCFF)
        "V"  -> Color(0xFF44EE44)
        "F"  -> Color(0xFFFF8800)
        else -> Color.White
    }

    Box(
        Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.92f)),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(24.dp),
            modifier = Modifier.padding(40.dp)
        ) {
            Text(chartName,
                color = Color.White.copy(alpha = 0.8f),
                fontSize = 20.sp, fontWeight = FontWeight.SemiBold)

            Text(hud.grade,
                color = gradeColor,
                fontSize = 96.sp, fontWeight = FontWeight.Black)

            // Stats card
            Surface(
                shape = RoundedCornerShape(16.dp),
                color = Color.White.copy(alpha = 0.08f),
                modifier = Modifier.widthIn(max = 320.dp)
            ) {
                Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    ResultRow("Score",     hud.scoreText)
                    ResultRow("Accuracy",  hud.accText)
                    ResultRow("Max Combo", "${hud.maxCombo}")
                }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(24.dp)) {
                OutlinedButton(onClick = onQuit, modifier = Modifier.defaultMinSize(minWidth = 120.dp)) {
                    Text("Menu", color = Color.White)
                }
                Button(onClick = onRetry, modifier = Modifier.defaultMinSize(minWidth = 120.dp)) {
                    Text("Retry")
                }
            }
        }
    }
}

@Composable
private fun ResultRow(label: String, value: String) {
    Row {
        Text(label, color = Color.White.copy(alpha = 0.6f))
        Spacer(Modifier.weight(1f))
        Text(value, color = Color.White, fontWeight = FontWeight.Bold)
    }
}
