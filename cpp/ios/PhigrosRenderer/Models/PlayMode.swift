import Foundation

enum PlayMode: Equatable, Hashable {
    case autoplay
    case manual
    case scriptplay(scriptURL: URL?)

    var displayName: String {
        switch self {
        case .autoplay:    return "Autoplay"
        case .manual:      return "Manual"
        case .scriptplay:  return "Scriptplay"
        }
    }

    var description: String {
        switch self {
        case .autoplay:
            return "Perfect autoplay simulation — watch the chart play itself."
        case .manual:
            return "Touch to hit notes. Judgment is spatial + temporal."
        case .scriptplay:
            return "Replay from a .phr script file."
        }
    }

    var cMode: String {
        switch self {
        case .autoplay:    return "autoplay"
        case .manual:      return "manual"
        case .scriptplay:  return "scriptplay"
        }
    }

    var scriptURL: URL? {
        if case .scriptplay(let url) = self { return url }
        return nil
    }

    static var allCases: [PlayMode] { [.autoplay, .manual, .scriptplay(scriptURL: nil)] }

    // Equality ignores associated URL so segmented pickers work correctly.
    static func == (lhs: PlayMode, rhs: PlayMode) -> Bool {
        switch (lhs, rhs) {
        case (.autoplay, .autoplay), (.manual, .manual), (.scriptplay, .scriptplay):
            return true
        default:
            return false
        }
    }

    // Hash must be consistent with ==: same bucket for same mode type regardless of URL.
    func hash(into hasher: inout Hasher) {
        switch self {
        case .autoplay:   hasher.combine(0)
        case .manual:     hasher.combine(1)
        case .scriptplay: hasher.combine(2)
        }
    }
}
