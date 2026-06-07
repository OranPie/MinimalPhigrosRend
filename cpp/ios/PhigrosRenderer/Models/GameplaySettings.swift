import Foundation

enum GameplaySettings {
    private static let defaults = UserDefaults.standard

    static var chartSpeed: Double {
        defaults.object(forKey: "setting_chart_speed") as? Double ?? 1.0
    }

    static var noteScale: Double {
        defaults.object(forKey: "setting_note_scale") as? Double ?? 2.5
    }

    static var audioOffsetMs: Double {
        defaults.object(forKey: "setting_audio_offset") as? Double ?? 0.0
    }

    static var hudOpacity: Double {
        defaults.object(forKey: "setting_hud_opacity") as? Double ?? 0.85
    }

    static var musicVolume: Double {
        defaults.object(forKey: "setting_music_volume") as? Double ?? 0.9
    }

    static var backgroundDim: Double {
        defaults.object(forKey: "setting_background_dim") as? Double ?? 0.45
    }

    static var backgroundBlur: Double {
        defaults.object(forKey: "setting_background_blur") as? Double ?? 0.35
    }

    static var showBackground: Bool {
        defaults.object(forKey: "setting_show_background") as? Bool ?? true
    }

    static var showHUD: Bool {
        defaults.object(forKey: "setting_show_hud") as? Bool ?? true
    }
}
