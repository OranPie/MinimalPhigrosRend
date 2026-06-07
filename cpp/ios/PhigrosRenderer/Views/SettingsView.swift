import SwiftUI

/// Persists settings to UserDefaults; GameBridge reads them at next init.
struct SettingsView: View {
    @Environment(\.dismiss) var dismiss

    @AppStorage("setting_chart_speed")     private var chartSpeed:     Double = 1.0
    @AppStorage("setting_note_scale")      private var noteScale:      Double = 2.5
    @AppStorage("setting_audio_offset")    private var audioOffset:    Double = 0.0
    @AppStorage("setting_hud_opacity")     private var hudOpacity:     Double = 0.85
    @AppStorage("setting_music_volume")    private var musicVolume:    Double = 0.9
    @AppStorage("setting_background_dim")  private var backgroundDim:  Double = 0.45
    @AppStorage("setting_background_blur") private var backgroundBlur: Double = 0.35
    @AppStorage("setting_show_background") private var showBackground: Bool = true
    @AppStorage("setting_show_hud")        private var showHUD:        Bool = true

    var body: some View {
        NavigationStack {
            Form {
                Section("Playback") {
                    LabeledContent("Chart Speed: \(chartSpeed, specifier: "%.1f")×") {
                        Slider(value: $chartSpeed, in: 0.5...3.0, step: 0.1)
                    }
                    LabeledContent("Note Scale: \(noteScale, specifier: "%.1f")") {
                        Slider(value: $noteScale, in: 0.5...4.0, step: 0.1)
                    }
                }
                Section("Audio") {
                    LabeledContent("Offset: \(Int(audioOffset)) ms") {
                        Slider(value: $audioOffset, in: -300...300, step: 5)
                    }
                    LabeledContent("Music Volume: \(musicVolume, specifier: "%.0f%%")") {
                        Slider(value: $musicVolume, in: 0.0...1.0, step: 0.05)
                    }
                }
                Section("Display") {
                    Toggle("Show Background", isOn: $showBackground)
                    Toggle("Show HUD", isOn: $showHUD)
                    LabeledContent("HUD Opacity: \(hudOpacity, specifier: "%.0f%%")") {
                        Slider(value: $hudOpacity, in: 0.0...1.0, step: 0.05)
                    }
                    LabeledContent("Background Dim: \(backgroundDim, specifier: "%.0f%%")") {
                        Slider(value: $backgroundDim, in: 0.0...0.9, step: 0.05)
                    }
                    LabeledContent("Background Blur: \(backgroundBlur, specifier: "%.0f%%")") {
                        Slider(value: $backgroundBlur, in: 0.0...1.0, step: 0.05)
                    }
                }
                Section {
                    Button("Reset Defaults", role: .destructive) {
                        chartSpeed  = 1.0
                        noteScale   = 2.5
                        audioOffset = 0.0
                        hudOpacity  = 0.85
                        musicVolume = 0.9
                        backgroundDim = 0.45
                        backgroundBlur = 0.35
                        showBackground = true
                        showHUD = true
                    }
                }
            }
            .navigationTitle("Settings")
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}
