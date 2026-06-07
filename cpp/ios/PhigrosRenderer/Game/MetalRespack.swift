import Foundation
import Metal
import MetalKit

struct MetalRespackConfig {
    var holdHeadH = 20
    var holdTailH = 20
    var holdHeadHMH = 20
    var holdTailHMH = 20
    var holdKeepHead = false
}

struct MetalRespack {
    struct NamedTexture {
        let texture: MTLTexture
        let width: Int
        let height: Int
    }

    let config: MetalRespackConfig
    let click: NamedTexture?
    let drag: NamedTexture?
    let flick: NamedTexture?
    let hold: NamedTexture?
    let clickMH: NamedTexture?
    let dragMH: NamedTexture?
    let flickMH: NamedTexture?
    let holdMH: NamedTexture?
    let hitFx: NamedTexture?

    func noteTexture(kind: Int, multiHit: Bool) -> NamedTexture? {
        if multiHit {
            switch kind {
            case 1: return clickMH ?? click
            case 2: return dragMH ?? drag
            case 3: return holdMH ?? hold
            case 4: return flickMH ?? flick
            default: return clickMH ?? click
            }
        }

        switch kind {
        case 1: return click
        case 2: return drag
        case 3: return hold
        case 4: return flick
        default: return click
        }
    }

    func holdTexture(multiHit: Bool) -> NamedTexture? {
        multiHit ? (holdMH ?? hold) : hold
    }

    func useHoldAtlasMH(multiHit: Bool) -> Bool {
        multiHit && holdMH != nil
    }
}

enum MetalRespackLoader {
    static func load(device: MTLDevice, zipPath: String?) -> MetalRespack? {
        guard let zipPath, !zipPath.isEmpty else { return nil }
        guard let extractedDir = extractRespack(zipPath: zipPath) else { return nil }

        let textureLoader = MTKTextureLoader(device: device)
        let config = loadConfig(from: extractedDir)

        func loadTexture(_ name: String) -> MetalRespack.NamedTexture? {
            let url = extractedDir.appendingPathComponent(name)
            guard FileManager.default.fileExists(atPath: url.path) else { return nil }
            let options: [MTKTextureLoader.Option: Any] = [
                .SRGB: false,
                .generateMipmaps: false
            ]
            guard let texture = try? textureLoader.newTexture(URL: url, options: options) else {
                return nil
            }
            return MetalRespack.NamedTexture(texture: texture,
                                             width: texture.width,
                                             height: texture.height)
        }

        return MetalRespack(
            config: config,
            click: loadTexture("click.png"),
            drag: loadTexture("drag.png"),
            flick: loadTexture("flick.png"),
            hold: loadTexture("hold.png"),
            clickMH: loadTexture("click_mh.png"),
            dragMH: loadTexture("drag_mh.png"),
            flickMH: loadTexture("flick_mh.png"),
            holdMH: loadTexture("hold_mh.png"),
            hitFx: loadTexture("hit_fx.png")
        )
    }

    private static func extractRespack(zipPath: String) -> URL? {
        let fm = FileManager.default
        let caches = fm.urls(for: .cachesDirectory, in: .userDomainMask).first
        let baseDir = (caches ?? URL(fileURLWithPath: NSTemporaryDirectory()))
            .appendingPathComponent("phigros_respack_cache", isDirectory: true)
        let zipURL = URL(fileURLWithPath: zipPath)
        let targetDir = baseDir.appendingPathComponent(zipURL.deletingPathExtension().lastPathComponent, isDirectory: true)

        try? fm.removeItem(at: targetDir)
        try? fm.createDirectory(at: targetDir, withIntermediateDirectories: true)
        let result = phigros_extract_zip_to_dir(zipPath, targetDir.path)
        return result >= 0 ? targetDir : nil
    }

    private static func loadConfig(from dir: URL) -> MetalRespackConfig {
        let candidates = ["info.yml", "info.yaml"]
        for file in candidates {
            let url = dir.appendingPathComponent(file)
            guard let text = try? String(contentsOf: url, encoding: .utf8) else { continue }
            return parseInfoYML(text)
        }
        return MetalRespackConfig()
    }

    private static func parseInfoYML(_ text: String) -> MetalRespackConfig {
        var cfg = MetalRespackConfig()
        var sawHoldAtlasMH = false

        for rawLine in text.components(separatedBy: .newlines) {
            let line = rawLine.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false).first.map(String.init) ?? rawLine
            guard let colon = line.firstIndex(of: ":") else { continue }
            let key = line[..<colon].trimmingCharacters(in: .whitespacesAndNewlines)
            var value = line[line.index(after: colon)...].trimmingCharacters(in: .whitespacesAndNewlines)
            value = value.trimmingCharacters(in: CharacterSet(charactersIn: "\"'"))

            func parsePair(_ string: String) -> (Int, Int)? {
                let cleaned = string.trimmingCharacters(in: CharacterSet(charactersIn: "[]"))
                let parts = cleaned.split(separator: ",").map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
                guard parts.count == 2,
                      let first = Int(parts[0]),
                      let second = Int(parts[1]) else {
                    return nil
                }
                return (first, second)
            }

            switch key {
            case "holdAtlas":
                if let (tail, head) = parsePair(value) {
                    cfg.holdTailH = tail
                    cfg.holdHeadH = head
                }
            case "holdAtlasMH":
                if let (tail, head) = parsePair(value) {
                    cfg.holdTailHMH = tail
                    cfg.holdHeadHMH = head
                    sawHoldAtlasMH = true
                }
            case "holdKeepHead":
                cfg.holdKeepHead = (value == "true")
            default:
                continue
            }
        }

        if !sawHoldAtlasMH {
            cfg.holdHeadHMH = cfg.holdHeadH
            cfg.holdTailHMH = cfg.holdTailH
        }
        return cfg
    }
}
