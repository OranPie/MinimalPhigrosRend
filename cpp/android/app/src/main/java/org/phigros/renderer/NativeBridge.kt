package org.phigros.renderer

object NativeBridge {
    init {
        System.loadLibrary("phigros_mobile_bridge_jni")
    }

    external fun initialize(width: Int, height: Int)
    external fun loadChart(path: String): Boolean
    external fun setTime(timeSeconds: Double)
    external fun restart()
    external fun getState(): BridgeState
    external fun getLastError(): String
}
