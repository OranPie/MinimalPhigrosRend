import Foundation

struct ChartMediaAssets {
    let audioURL: URL?
    let backgroundURL: URL?
}

enum ChartMediaResolver {
    static func resolve(for chartURL: URL) -> ChartMediaAssets {
        let searchRoots = candidateRoots(for: chartURL)
        let metadata = loadMetadata(from: chartURL, roots: searchRoots)
        let audio = resolveRelativeAsset(metadata.songPath, roots: searchRoots)
            ?? preferredMatch(in: searchRoots, names: ["music", "song", "audio"], extensions: ["ogg", "mp3", "wav", "m4a", "aac", "flac"])
            ?? firstMatch(in: searchRoots, extensions: ["ogg", "mp3", "wav", "m4a", "aac", "flac"])
        let background = resolveRelativeAsset(metadata.backgroundPath, roots: searchRoots)
            ?? preferredMatch(in: searchRoots, names: ["illustration", "background", "bg", "cover"], extensions: ["jpg", "jpeg", "png", "webp"])
            ?? firstMatch(in: searchRoots, extensions: ["jpg", "jpeg", "png", "webp"])
        return ChartMediaAssets(audioURL: audio, backgroundURL: background)
    }

    private static func candidateRoots(for chartURL: URL) -> [URL] {
        var roots: [URL] = []
        let chartDir = chartURL.deletingLastPathComponent()
        roots.append(chartDir)

        var cursor = chartDir
        for _ in 0..<4 {
            let parent = cursor.deletingLastPathComponent()
            if parent.path == cursor.path { break }
            roots.append(parent)
            cursor = parent
        }
        return Array(NSOrderedSet(array: roots)) as? [URL] ?? roots
    }

    private static func loadMetadata(from chartURL: URL, roots: [URL]) -> (songPath: String?, backgroundPath: String?) {
        if let jsonMetadata = loadJSONMetadata(from: chartURL) {
            return jsonMetadata
        }

        for root in roots {
            for fileName in ["info.json", "info.yml", "info.yaml", "info.txt"] {
                let url = root.appendingPathComponent(fileName)
                if fileName.hasSuffix(".json"), let metadata = loadJSONMetadata(from: url) {
                    return metadata
                }
                if let metadata = loadKeyValueMetadata(from: url) {
                    return metadata
                }
            }
        }

        return (nil, nil)
    }

    private static func loadJSONMetadata(from url: URL) -> (songPath: String?, backgroundPath: String?)? {
        guard url.pathExtension.lowercased() == "json",
              let data = try? Data(contentsOf: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return nil
        }

        if let meta = json["META"] as? [String: Any] {
            return (
                meta["song"] as? String ?? meta["music"] as? String,
                meta["background"] as? String ?? meta["illustration"] as? String
            )
        }

        if let info = json["info"] as? [String: Any] {
            return (
                info["song"] as? String ?? info["music"] as? String,
                info["background"] as? String ?? info["illustration"] as? String
            )
        }

        if let charts = json["charts"] as? [[String: Any]],
           let first = charts.first {
            return (
                json["music"] as? String ?? json["song"] as? String,
                json["illustration"] as? String ?? json["background"] as? String ?? first["image"] as? String
            )
        }

        return nil
    }

    private static func loadKeyValueMetadata(from url: URL) -> (songPath: String?, backgroundPath: String?)? {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else { return nil }
        var values: [String: String] = [:]
        for rawLine in text.components(separatedBy: .newlines) {
            let line = rawLine.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false).first.map(String.init) ?? rawLine
            guard let separator = line.firstIndex(where: { $0 == ":" || $0 == "=" }) else { continue }
            let key = line[..<separator].trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
            let value = line[line.index(after: separator)...].trimmingCharacters(in: .whitespacesAndNewlines)
                .trimmingCharacters(in: CharacterSet(charactersIn: "\"'"))
            if !key.isEmpty && !value.isEmpty {
                values[key] = value
            }
        }
        if values.isEmpty { return nil }
        return (
            values["music"] ?? values["song"],
            values["illustration"] ?? values["background"] ?? values["picture"]
        )
    }

    private static func resolveRelativeAsset(_ relativePath: String?, roots: [URL]) -> URL? {
        guard let relativePath, !relativePath.isEmpty else { return nil }
        for root in roots {
            let url = root.appendingPathComponent(relativePath)
            if FileManager.default.fileExists(atPath: url.path) {
                return url
            }
        }
        return nil
    }

    private static func preferredMatch(in roots: [URL], names: Set<String>, extensions: Set<String>) -> URL? {
        for root in roots {
            guard let enumerator = FileManager.default.enumerator(
                at: root,
                includingPropertiesForKeys: [.isRegularFileKey],
                options: [.skipsHiddenFiles]
            ) else {
                continue
            }
            for case let url as URL in enumerator {
                let stem = url.deletingPathExtension().lastPathComponent.lowercased()
                if names.contains(stem), extensions.contains(url.pathExtension.lowercased()) {
                    return url
                }
            }
        }
        return nil
    }

    private static func firstMatch(in roots: [URL], extensions: Set<String>) -> URL? {
        for root in roots {
            guard let enumerator = FileManager.default.enumerator(
                at: root,
                includingPropertiesForKeys: [.isRegularFileKey],
                options: [.skipsHiddenFiles]
            ) else {
                continue
            }
            for case let url as URL in enumerator {
                if extensions.contains(url.pathExtension.lowercased()) {
                    return url
                }
            }
        }
        return nil
    }
}
