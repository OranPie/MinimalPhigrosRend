import Foundation
import Combine

/// Thread-safe wrapper around phigros_mobile_handle.
/// All C-bridge calls go through this class; SwiftUI reads @Published properties.
@MainActor
final class GameBridge: ObservableObject {
    // MARK: — Published HUD / state
    @Published var chartLoaded  = false
    @Published var isPaused     = true
    @Published var chartEnded   = false
    @Published var chartOffset  = 0.0
    @Published var score        = 0
    @Published var combo        = 0
    @Published var maxCombo     = 0
    @Published var accuracy: Float = 1.0
    @Published var progress: Float = 0.0
    @Published var scoreText    = "0000000"
    @Published var accText      = "100.00%"
    @Published var showCombo    = false
    @Published var chartTime    = 0.0
    @Published var chartDuration = 0.0
    @Published var lastError    = ""

    // Handle never changes after init — nonisolated let allows Metal-thread access
    nonisolated private let handle: OpaquePointer?

    init(width: Int, height: Int, respackPath: String? = nil) {
        var cfg = phigros_mobile_config(
            window_width:    Int32(width),
            window_height:   Int32(height),
            chart_speed:     GameplaySettings.chartSpeed,
            note_scale:      GameplaySettings.noteScale,
            audio_offset_ms: GameplaySettings.audioOffsetMs,
            respack_path:    nil
        )
        // Use a local var so the nonisolated let handle is assigned exactly once.
        var h: OpaquePointer?
        if let rp = respackPath {
            rp.withCString { ptr in
                cfg.respack_path = ptr
                h = phigros_mobile_create(&cfg)
            }
        } else {
            h = phigros_mobile_create(&cfg)
        }
        handle = h
    }

    deinit {
        if let h = handle { phigros_mobile_destroy(h) }
    }

    // MARK: — Chart loading
    func loadChart(url: URL, password: String = "") {
        guard let h = handle else { return }
        url.startAccessingSecurityScopedResource()
        defer { url.stopAccessingSecurityScopedResource() }
        let result = url.path.withCString { path in
            password.withCString { pwd in
                phigros_mobile_load_chart(h, path, pwd)
            }
        }
        if result == 0 {
            chartLoaded = true
            refreshState()
            lastError = ""
        } else {
            lastError = bridgeError()
        }
    }

    // MARK: — Play mode
    func setPlayMode(_ mode: PlayMode) {
        guard let h = handle else { return }
        mode.cMode.withCString { phigros_mobile_set_play_mode(h, $0) }
        if case .scriptplay(let url) = mode, let scriptURL = url {
            scriptURL.path.withCString { phigros_mobile_load_script(h, $0) }
        }
    }

    // MARK: — Playback control
    func setPaused(_ value: Bool) {
        guard let h = handle else { return }
        phigros_mobile_set_paused(h, value ? 1 : 0)
        isPaused = value
    }

    func restart() {
        guard let h = handle else { return }
        phigros_mobile_restart(h)
        refreshState()
        chartEnded = false
        isPaused   = false
    }

    func attachSurface(_ layer: UnsafeMutableRawPointer, width: Int, height: Int) {
        guard let h = handle else { return }
        phigros_mobile_attach_surface(h, layer, Int32(width), Int32(height))
    }

    // MARK: — Touch forwarding
    /// Called directly from the touch thread — no actor dispatch needed because
    /// phigros_mobile_on_touch() is mutex-protected in C++.
    func sendTouchDirect(id: Int32, phase: phigros_mobile_touch_phase, x: Float, y: Float) {
        guard let h = handle else { return }
        phigros_mobile_on_touch(h, id, phase, x, y,
            Int64(Date().timeIntervalSince1970 * 1000))
    }

    /// Retained for MainActor callers; use sendTouchDirect from non-isolated contexts.
    func sendTouch(id: Int32, phase: phigros_mobile_touch_phase, x: Float, y: Float) {
        sendTouchDirect(id: id, phase: phase, x: x, y: y)
    }

    // MARK: — Per-frame tick + frame fill (called from Metal thread; NOT @MainActor)
    nonisolated func tick(dt: Double,
                          frameData: UnsafeMutablePointer<phigros_frame_data>) -> Bool {
        guard let h = handle else { return false }
        phigros_mobile_tick(h, dt)
        return phigros_mobile_get_frame(h, frameData) == 0
    }

    // Publish HUD values back to the main thread from a filled frame_data.
    func updateHUD(from frame: phigros_frame_data) {
        let h = frame.hud
        score       = Int(h.score)
        combo       = Int(h.combo)
        maxCombo    = Int(h.max_combo)
        accuracy    = h.accuracy
        progress    = h.progress
        showCombo   = h.show_combo != 0
        chartTime   = frame.chart_time
        chartEnded  = frame.chart_ended != 0

        scoreText = withUnsafeBytes(of: h.score_text) { raw in
            let bytes = raw.prefix(while: { $0 != 0 })
            return String(bytes: bytes, encoding: .ascii) ?? "0000000"
        }
        accText = withUnsafeBytes(of: h.acc_text) { raw in
            let bytes = raw.prefix(while: { $0 != 0 })
            return String(bytes: bytes, encoding: .ascii) ?? "100.00%"
        }
    }

    // MARK: — Error helper
    private func bridgeError() -> String {
        guard let h = handle else { return "No handle" }
        var buf = [CChar](repeating: 0, count: 512)
        phigros_mobile_copy_last_error(h, &buf, 512)
        return String(cString: buf)
    }

    private func refreshState() {
        guard let h = handle else { return }
        var state = phigros_mobile_state()
        guard phigros_mobile_get_state(h, &state) == 0 else { return }
        chartDuration = state.chart_duration
        chartOffset = state.chart_offset
    }
}
