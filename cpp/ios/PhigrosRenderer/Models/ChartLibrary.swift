import Foundation
import UniformTypeIdentifiers

// Stable ID based on filename so NavigationStack selection survives re-scans.
struct ChartEntry: Identifiable, Hashable {
    let id: String          // filename e.g. "mysong.json"
    let displayName: String
    let url: URL
    var difficulty: String = ""

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (lhs: ChartEntry, rhs: ChartEntry) -> Bool { lhs.id == rhs.id }
}

@MainActor
final class ChartLibrary: ObservableObject {
    @Published var charts: [ChartEntry] = []
    @Published var lastError: String = ""

    // Documents/ root for Inbox detection
    private let docsURL: URL = {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    }()

    // Documents/Charts/ — permanent home for all chart files
    private lazy var chartsURL: URL = {
        let url = docsURL.appendingPathComponent("Charts", isDirectory: true)
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }()

    // MARK: - Public API

    func scan() {
        drainInbox()
        var found: [ChartEntry] = []
        let fm = FileManager.default
        guard let items = fm.enumerator(
            at: chartsURL,
            includingPropertiesForKeys: [.isRegularFileKey],
            options: [.skipsHiddenFiles]
        ) else { return }

        for case let url as URL in items {
            let ext = url.pathExtension.lowercased()
            guard ext == "json" || ext == "phbc" else { continue }
            let fname = url.lastPathComponent
            let relativeParent = url.deletingLastPathComponent().path
                .replacingOccurrences(of: chartsURL.path, with: "")
                .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            let displayName = relativeParent.isEmpty
                ? url.deletingPathExtension().lastPathComponent
                : "\(relativeParent) / \(url.deletingPathExtension().lastPathComponent)"
            found.append(ChartEntry(
                id: url.path.replacingOccurrences(of: chartsURL.path, with: ""),
                displayName: displayName,
                url: url
            ))
        }
        charts = found.sorted { $0.displayName.localizedCompare($1.displayName) == .orderedAscending }
    }

    /// Called from UIDocumentPickerViewController / SwiftUI fileImporter.
    func `import`(from source: URL) {
        let gotAccess = source.startAccessingSecurityScopedResource()
        defer { if gotAccess { source.stopAccessingSecurityScopedResource() } }
        processFile(at: source, removeSourceAfterImport: false)
        scan()
    }

    // MARK: - Private helpers

    /// Move chart/zip files from the Inbox folder into Charts/.
    private func drainInbox() {
        let inbox = docsURL.appendingPathComponent("Inbox", isDirectory: true)
        guard let items = try? FileManager.default.contentsOfDirectory(
            at: inbox,
            includingPropertiesForKeys: nil,
            options: .skipsHiddenFiles
        ) else { return }
        for item in items {
            let ext = item.pathExtension.lowercased()
            if ext == "json" || ext == "phbc" || ext == "zip" {
                processFile(at: item, removeSourceAfterImport: true)
            }
        }
    }

    /// Route a single file: extract if zip, copy if chart, then remove original.
    private func processFile(at source: URL, removeSourceAfterImport: Bool) {
        let ext = source.pathExtension.lowercased()
        if ext == "zip" {
            extractZip(at: source)
        } else if ext == "json" || ext == "phbc" {
            copyChart(from: source)
        }
        if removeSourceAfterImport {
            try? FileManager.default.removeItem(at: source)
        }
    }

    private func copyChart(from source: URL) {
        let dest = chartsURL.appendingPathComponent(source.lastPathComponent)
        do {
            if FileManager.default.fileExists(atPath: dest.path) {
                try FileManager.default.removeItem(at: dest)
            }
            try FileManager.default.copyItem(at: source, to: dest)
        } catch {
            lastError = error.localizedDescription
        }
    }

    private func extractZip(at zip: URL) {
        let targetDir = chartsURL.appendingPathComponent(zip.deletingPathExtension().lastPathComponent, isDirectory: true)
        try? FileManager.default.removeItem(at: targetDir)
        try? FileManager.default.createDirectory(at: targetDir, withIntermediateDirectories: true)
        let count = phigros_extract_zip_to_dir(zip.path, targetDir.path)
        if count < 0 {
            lastError = "Failed to extract \(zip.lastPathComponent)"
        }
    }
}
