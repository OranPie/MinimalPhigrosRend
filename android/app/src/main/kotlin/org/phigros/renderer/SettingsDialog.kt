package org.phigros.renderer

import android.content.Context
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.edit

private const val PREFS = "phigros_settings"

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsDialog(onDismiss: () -> Unit) {
    val context = LocalContext.current
    val prefs   = remember { context.getSharedPreferences(PREFS, Context.MODE_PRIVATE) }

    var speed  by remember { mutableFloatStateOf(prefs.getFloat("chart_speed",   1.0f)) }
    var scale  by remember { mutableFloatStateOf(prefs.getFloat("note_scale",    2.5f)) }
    var offset by remember { mutableFloatStateOf(prefs.getFloat("audio_offset",  0.0f)) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title   = { Text("Settings") },
        text    = {
            Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                SettingSlider("Chart Speed: ${"%.1f".format(speed)}×",
                    value = speed, range = 0.5f..3.0f, steps = 24,
                    onChanged = { speed = it; prefs.edit { putFloat("chart_speed", it) } })

                SettingSlider("Note Scale: ${"%.1f".format(scale)}",
                    value = scale, range = 0.5f..4.0f, steps = 34,
                    onChanged = { scale = it; prefs.edit { putFloat("note_scale", it) } })

                SettingSlider("Audio Offset: ${offset.toInt()} ms",
                    value = offset, range = -300f..300f, steps = 119,
                    onChanged = { offset = it; prefs.edit { putFloat("audio_offset", it) } })
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Done") } },
        dismissButton = {
            TextButton(onClick = {
                speed = 1.0f; scale = 2.5f; offset = 0.0f
                prefs.edit {
                    putFloat("chart_speed", 1.0f)
                    putFloat("note_scale",  2.5f)
                    putFloat("audio_offset", 0.0f)
                }
            }) { Text("Reset") }
        }
    )
}

@Composable
private fun SettingSlider(
    label: String, value: Float,
    range: ClosedFloatingPointRange<Float>,
    steps: Int,
    onChanged: (Float) -> Unit
) {
    Column {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Slider(value = value, onValueChange = onChanged,
            valueRange = range, steps = steps,
            modifier = Modifier.fillMaxWidth())
    }
}
