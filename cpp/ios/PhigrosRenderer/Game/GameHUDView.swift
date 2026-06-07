import SwiftUI

struct GameHUDView: View {
    @ObservedObject var bridge: GameBridge
    let onPause: () -> Void
    @AppStorage("setting_hud_opacity") private var hudOpacity: Double = 0.85
    @AppStorage("setting_show_hud") private var showHUD: Bool = true

    var body: some View {
        ZStack {
            // Progress bar (top edge)
            VStack {
                GeometryReader { geo in
                    Rectangle()
                        .fill(Color.white.opacity(0.7))
                        .frame(width: geo.size.width * CGFloat(bridge.progress), height: 3)
                }
                .frame(height: 3)
                Spacer()
            }

            // Top-left: accuracy + pause button
            VStack {
                HStack(alignment: .top) {
                    Button(action: onPause) {
                        Image(systemName: "pause.fill")
                            .font(.title3)
                            .foregroundStyle(.white)
                            .padding(10)
                            .background(.black.opacity(0.4), in: Circle())
                    }
                    .padding([.top, .leading], 16)

                    VStack(alignment: .leading, spacing: 2) {
                        Text(bridge.accText)
                            .font(.system(.caption, design: .monospaced).weight(.semibold))
                            .foregroundStyle(.white)
                    }
                    .padding(.top, 18)
                    .padding(.leading, 4)

                    Spacer()

                    // Top-right: score
                    Text(bridge.scoreText)
                        .font(.system(size: 28, weight: .bold, design: .monospaced))
                        .foregroundStyle(.white)
                        .shadow(color: .black.opacity(0.5), radius: 4)
                        .padding([.top, .trailing], 16)
                }
                Spacer()
            }

            // Center: combo
            if bridge.showCombo {
                VStack(spacing: 0) {
                    Text("\(bridge.combo)")
                        .font(.system(size: 64, weight: .black, design: .rounded))
                        .foregroundStyle(.white)
                        .shadow(color: .white.opacity(0.4), radius: 10)
                    Text("COMBO")
                        .font(.system(size: 14, weight: .semibold))
                        .foregroundStyle(.white.opacity(0.8))
                        .tracking(4)
                }
                .transition(.scale.combined(with: .opacity))
                .animation(.spring(duration: 0.25), value: bridge.combo)
            }
        }
        .opacity(showHUD ? hudOpacity : 0.0)
    }
}
