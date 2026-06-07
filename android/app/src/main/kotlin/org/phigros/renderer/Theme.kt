package org.phigros.renderer

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val DarkColors = darkColorScheme(
    primary         = Color(0xFF82AAFF),
    onPrimary       = Color(0xFF003680),
    primaryContainer = Color(0xFF004BA7),
    secondary       = Color(0xFF55CCFF),
    background      = Color(0xFF0D1117),
    surface         = Color(0xFF161B22),
    onBackground    = Color(0xFFE6EDF3),
    onSurface       = Color(0xFFE6EDF3),
    error           = Color(0xFFFF6B6B)
)

@Composable
fun PhigrosTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkColors,
        content     = content
    )
}
