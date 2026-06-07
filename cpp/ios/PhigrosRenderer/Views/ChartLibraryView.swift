import SwiftUI
import UniformTypeIdentifiers

struct ChartLibraryView: View {
    @EnvironmentObject var library: ChartLibrary
    @Binding var selected: ChartEntry?
    @State private var showImporter = false

    var body: some View {
        List(library.charts, selection: $selected) { chart in
            ChartRow(chart: chart)
                .tag(chart)
        }
        .navigationTitle("Charts")
        .overlay {
            if library.charts.isEmpty {
                VStack(spacing: 12) {
                    Image(systemName: "music.note.list")
                        .font(.system(size: 48))
                        .foregroundStyle(.secondary)
                    Text("No Charts")
                        .font(.headline)
                        .foregroundStyle(.secondary)
                    Text("Import a .json, .phbc, or .zip chart file.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .toolbar {
            ToolbarItem(placement: .topBarLeading) {
                Button { showImporter = true }
                    label: { Image(systemName: "plus") }
            }
            ToolbarItem(placement: .topBarTrailing) {
                Button { library.scan() }
                    label: { Image(systemName: "arrow.clockwise") }
            }
        }
        .fileImporter(
            isPresented: $showImporter,
            allowedContentTypes: [
                .json,
                UTType(exportedAs: "org.phigros.phbc"),
                UTType(exportedAs: "org.phigros.phr"),
                .zip,
            ],
            allowsMultipleSelection: true
        ) { result in
            if case .success(let urls) = result {
                urls.forEach { library.import(from: $0) }
            }
        }
        .onAppear { library.scan() }
        .alert("Import Error", isPresented: .constant(!library.lastError.isEmpty)) {
            Button("OK") { library.lastError = "" }
        } message: { Text(library.lastError) }
    }
}

private struct ChartRow: View {
    let chart: ChartEntry

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(chart.displayName)
                .font(.body)
            Text(chart.url.pathExtension.uppercased())
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
        .padding(.vertical, 2)
    }
}
