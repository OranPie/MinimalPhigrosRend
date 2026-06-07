import SwiftUI
import UniformTypeIdentifiers

struct PlayModeSheet: View {
    let chart: ChartEntry
    @Binding var pendingPlayMode: PlayMode?

    @State private var selectedMode: PlayMode = .autoplay
    @State private var scriptURL: URL?
    @State private var showScriptPicker = false

    var body: some View {
        Form {
            Section("Chart") {
                LabeledContent("Name", value: chart.displayName)
                LabeledContent("File", value: chart.url.lastPathComponent)
            }

            Section("Play Mode") {
                Picker("Mode", selection: $selectedMode) {
                    ForEach(PlayMode.allCases, id: \.cMode) { mode in
                        Text(mode.displayName).tag(mode)
                    }
                }
                .pickerStyle(.segmented)

                Text(selectedMode.description)
                    .font(.caption)
                    .foregroundStyle(.secondary)

                if selectedMode == .scriptplay(scriptURL: nil) {
                    HStack {
                        Text(scriptURL?.lastPathComponent ?? "No script selected")
                            .font(.caption)
                            .foregroundStyle(scriptURL == nil ? .red : .primary)
                        Spacer()
                        Button("Choose…") { showScriptPicker = true }
                            .buttonStyle(.bordered)
                    }
                }
            }

            Section {
                Button(action: startGame) {
                    Label("Start Game", systemImage: "play.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .disabled(selectedMode == .scriptplay(scriptURL: nil) && scriptURL == nil)
            }
        }
        .navigationTitle("Play Mode")
        .fileImporter(
            isPresented: $showScriptPicker,
            allowedContentTypes: [UTType(exportedAs: "org.phigros.phr"),
                                  .json],
            allowsMultipleSelection: false
        ) { result in
            if case .success(let urls) = result, let url = urls.first {
                scriptURL = url
                selectedMode = .scriptplay(scriptURL: url)
            }
        }
    }

    private func startGame() {
        let mode: PlayMode
        if case .scriptplay = selectedMode {
            mode = .scriptplay(scriptURL: scriptURL)
        } else {
            mode = selectedMode
        }
        pendingPlayMode = mode
    }
}
