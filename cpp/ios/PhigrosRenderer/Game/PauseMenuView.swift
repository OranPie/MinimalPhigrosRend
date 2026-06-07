import SwiftUI

struct PauseMenuView: View {
    let onResume:  () -> Void
    let onRestart: () -> Void
    let onQuit:    () -> Void

    var body: some View {
        ZStack {
            Color.black.opacity(0.6)
                .ignoresSafeArea()

            VStack(spacing: 20) {
                Text("Paused")
                    .font(.largeTitle.weight(.bold))
                    .foregroundStyle(.white)

                Divider().background(.white.opacity(0.3))

                pauseButton(title: "Resume",  icon: "play.fill",             action: onResume)
                pauseButton(title: "Restart", icon: "arrow.counterclockwise", action: onRestart)
                pauseButton(title: "Quit",    icon: "xmark",                  action: onQuit,
                            role: .destructive)
            }
            .padding(40)
            .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 24))
            .frame(maxWidth: 320)
        }
        .transition(.opacity)
        .animation(.easeInOut(duration: 0.2), value: true)
    }

    @ViewBuilder
    private func pauseButton(title: String, icon: String,
                             action: @escaping () -> Void,
                             role: ButtonRole? = nil) -> some View {
        Button(role: role, action: action) {
            Label(title, systemImage: icon)
                .frame(maxWidth: .infinity)
        }
        .buttonStyle(.borderedProminent)
        .controlSize(.large)
        .tint(role == .destructive ? .red : .accentColor)
    }
}
