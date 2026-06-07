import SwiftUI

struct ResultView: View {
    let bridge: GameBridge
    let chartName: String
    let onRetry: () -> Void
    let onQuit:  () -> Void

    private var grade: String {
        let acc = Double(bridge.accuracy)
        if acc >= 1.0          { return "AP" }
        if bridge.maxCombo == bridge.combo && acc >= 0.96 { return "FC" }
        if acc >= 0.96         { return "V" }
        if acc >= 0.70         { return "F" }
        return "P"
    }

    private var gradeColor: Color {
        switch grade {
        case "AP": return .yellow
        case "FC": return .cyan
        case "V":  return .green
        case "F":  return .orange
        default:   return .white
        }
    }

    var body: some View {
        ZStack {
            Color.black.opacity(0.9).ignoresSafeArea()

            VStack(spacing: 32) {
                Text(chartName)
                    .font(.title2.weight(.semibold))
                    .foregroundStyle(.white.opacity(0.8))

                // Grade badge
                Text(grade)
                    .font(.system(size: 96, weight: .black, design: .rounded))
                    .foregroundStyle(gradeColor)
                    .shadow(color: gradeColor.opacity(0.6), radius: 20)
                    .transition(.scale.combined(with: .opacity))

                // Stats grid
                VStack(spacing: 12) {
                    resultRow(label: "Score",     value: bridge.scoreText)
                    resultRow(label: "Accuracy",  value: bridge.accText)
                    resultRow(label: "Max Combo", value: "\(bridge.maxCombo)")
                }
                .padding()
                .background(.white.opacity(0.08), in: RoundedRectangle(cornerRadius: 16))

                // Buttons
                HStack(spacing: 24) {
                    Button(action: onQuit) {
                        Label("Menu", systemImage: "list.bullet")
                            .frame(minWidth: 120)
                    }
                    .buttonStyle(.bordered)
                    .tint(.white)

                    Button(action: onRetry) {
                        Label("Retry", systemImage: "arrow.counterclockwise")
                            .frame(minWidth: 120)
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(.accentColor)
                }
            }
            .padding(40)
        }
        .animation(.spring(duration: 0.6), value: grade)
    }

    private func resultRow(label: String, value: String) -> some View {
        HStack {
            Text(label).foregroundStyle(.white.opacity(0.6))
            Spacer()
            Text(value).foregroundStyle(.white).bold()
        }
    }
}
