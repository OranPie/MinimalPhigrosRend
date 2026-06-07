package org.phigros.renderer

data class BridgeState(
    val chartLoaded: Int,
    val lineCount: Int,
    val totalNotes: Int,
    val playableNotes: Int,
    val visibleNotes: Int,
    val activeTouchCount: Int,
    val maxCombo: Int,
    val judgedNotes: Int,
    val chartTime: Double,
    val chartOffset: Double,
    val chartDuration: Double,
    val paused: Boolean
)
