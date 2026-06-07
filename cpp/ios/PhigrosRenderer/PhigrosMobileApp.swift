import SwiftUI

@main
struct PhigrosMobileApp: App {
    @StateObject private var library = ChartLibrary()

    var body: some Scene {
        WindowGroup {
            RootNavigationView()
                .environmentObject(library)
                .onAppear { library.scan() }
                .onOpenURL { url in
                    // iOS delivers shared files via this callback; import drains Inbox too.
                    library.import(from: url)
                }
        }
    }
}

// MARK: — Adaptive root navigation (iPad split / iPhone stack)
struct RootNavigationView: View {
    @EnvironmentObject var library: ChartLibrary
    @State private var selectedChart: ChartEntry?
    @State private var pendingPlayMode: PlayMode?  // set by PlayModeSheet → triggers game push
    @State private var showSettings = false

    var body: some View {
        Group {
            if UIDevice.current.userInterfaceIdiom == .pad {
                iPadLayout
            } else {
                iPhoneLayout
            }
        }
    }

    // MARK: iPad — NavigationSplitView
    @ViewBuilder
    private var iPadLayout: some View {
        NavigationSplitView {
            SidebarView(selected: $selectedChart, showSettings: $showSettings)
                .environmentObject(library)
        } detail: {
            if let chart = selectedChart {
                PlayModeSheet(chart: chart, pendingPlayMode: $pendingPlayMode)
                    .fullScreenCover(item: $pendingPlayMode) { mode in
                        GameplayContainerView(chart: chart, playMode: mode,
                                              pendingPlayMode: $pendingPlayMode)
                    }
            } else {
                Text("Select a chart from the sidebar")
                    .foregroundStyle(.secondary)
            }
        }
        .sheet(isPresented: $showSettings) { SettingsView() }
    }

    // MARK: iPhone — NavigationStack
    @ViewBuilder
    private var iPhoneLayout: some View {
        NavigationStack {
            ChartLibraryView(selected: $selectedChart)
                .environmentObject(library)
                .toolbar {
                    ToolbarItem(placement: .topBarTrailing) {
                        Button { showSettings = true }
                        label: { Image(systemName: "gear") }
                    }
                }
                // navigationDestination(item:) is iOS 17+; use isPresented for iOS 16.
                .navigationDestination(isPresented: Binding(
                    get: { selectedChart != nil },
                    set: { if !$0 { selectedChart = nil } }
                )) {
                    if let chart = selectedChart {
                        PlayModeSheet(chart: chart, pendingPlayMode: $pendingPlayMode)
                            .fullScreenCover(item: $pendingPlayMode) { mode in
                                GameplayContainerView(chart: chart, playMode: mode,
                                                      pendingPlayMode: $pendingPlayMode)
                            }
                    }
                }
        }
        .sheet(isPresented: $showSettings) { SettingsView() }
    }
}

// MARK: — iPad sidebar
struct SidebarView: View {
    @Binding var selected: ChartEntry?
    @Binding var showSettings: Bool
    @EnvironmentObject var library: ChartLibrary

    var body: some View {
        ChartLibraryView(selected: $selected)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button { showSettings = true }
                        label: { Image(systemName: "gear") }
                }
            }
    }
}

// Conform PlayMode to Identifiable so fullScreenCover(item:) works
extension PlayMode: Identifiable {
    var id: String { cMode }
}

