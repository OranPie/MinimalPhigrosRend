package org.phigros.renderer

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import java.io.File
import java.io.FileOutputStream

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val metrics = resources.displayMetrics
        NativeBridge.initialize(metrics.widthPixels, metrics.heightPixels)

        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    MobileShellScreen()
                }
            }
        }
    }
}

@androidx.compose.runtime.Composable
private fun MobileShellScreen() {
    val context = LocalContext.current
    var bridgeState by remember { mutableStateOf(NativeBridge.getState()) }
    var selectedChart by remember { mutableStateOf("") }
    var errorText by remember { mutableStateOf("") }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        val copied = copyUriToCache(context.cacheDir, context.contentResolver, uri)
        if (copied == null) {
            errorText = "Failed to copy selected chart"
            return@rememberLauncherForActivityResult
        }
        selectedChart = copied.absolutePath
        if (NativeBridge.loadChart(copied.absolutePath)) {
            bridgeState = NativeBridge.getState()
            errorText = ""
        } else {
            errorText = NativeBridge.getLastError()
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        Text(stringResource(R.string.mobile_shell_title), style = MaterialTheme.typography.headlineMedium)
        Text(stringResource(R.string.mobile_shell_subtitle), style = MaterialTheme.typography.bodyMedium)

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(onClick = { picker.launch(arrayOf("application/json", "*/*")) }) {
                Text("Import Chart")
            }
            Button(
                onClick = {
                    NativeBridge.restart()
                    bridgeState = NativeBridge.getState()
                },
                enabled = bridgeState.chartLoaded != 0
            ) {
                Text("Restart")
            }
            Button(
                onClick = {
                    if (selectedChart.isNotEmpty()) {
                        val intent = Intent(context, PhigrosActivity::class.java)
                        intent.putExtra(PhigrosActivity.EXTRA_CHART_PATH, selectedChart)
                        intent.putExtra(PhigrosActivity.EXTRA_PLAY_MODE, PhigrosActivity.PLAY_MODE_MANUAL)
                        context.startActivity(intent)
                    }
                },
                enabled = selectedChart.isNotEmpty()
            ) {
                Text("Play Chart")
            }
        }

        Text(if (selectedChart.isEmpty()) "No chart selected" else selectedChart)
        Text("Lines: ${bridgeState.lineCount}")
        Text("Playable notes: ${bridgeState.playableNotes}")
        Text("Visible notes: ${bridgeState.visibleNotes}")
        Text("Judged notes: ${bridgeState.judgedNotes}")
        Text("Max combo: ${bridgeState.maxCombo}")
        Text("Touches: ${bridgeState.activeTouchCount}")
        Text("Tap \"Play Chart\" to open manual touch play.")

        Slider(
            value = bridgeState.chartTime.toFloat(),
            onValueChange = {
                NativeBridge.setTime(it.toDouble())
                bridgeState = NativeBridge.getState()
            },
            valueRange = bridgeState.chartOffset.toFloat()..
                maxOf((bridgeState.chartOffset + bridgeState.chartDuration).toFloat(),
                      bridgeState.chartOffset.toFloat() + 0.001f),
            enabled = bridgeState.chartLoaded != 0
        )
        Text(String.format("%.2fs / %.2fs", bridgeState.chartTime, bridgeState.chartDuration))

        if (errorText.isNotEmpty()) {
            Text(errorText, color = MaterialTheme.colorScheme.error)
        }

        Spacer(modifier = Modifier.height(1.dp))
    }
}

private fun copyUriToCache(
    cacheDir: File,
    resolver: android.content.ContentResolver,
    uri: Uri
): File? {
    return try {
        val name = "phigros_${System.currentTimeMillis()}.chart"
        val outFile = File(cacheDir, name)
        resolver.openInputStream(uri)?.use { input ->
            FileOutputStream(outFile).use { output ->
                input.copyTo(output)
            }
        }
        outFile
    } catch (_: Exception) {
        null
    }
}
