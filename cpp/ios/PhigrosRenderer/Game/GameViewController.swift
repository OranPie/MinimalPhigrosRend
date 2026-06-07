import AVFoundation
import UIKit
import MetalKit
import SwiftUI
import Combine

/// UIViewController that owns the MetalKit game view and hosts SwiftUI overlays.
final class GameViewController: UIViewController {

    // Inputs set before presentation
    var chartEntry: ChartEntry!
    var playMode: PlayMode = .autoplay
    var onDismiss: (() -> Void)?

    private var bridge: GameBridge?
    private var metalView: GameMetalView?
    private var backgroundView: UIImageView?
    private var backgroundDimView: UIView?
    private var backgroundBlurView: UIVisualEffectView?
    private var hudHost:   UIHostingController<AnyView>?
    private var overlayHost: UIHostingController<AnyView>?
    private var musicPlayer: AVAudioPlayer?

    // Pause / result state (observed by SwiftUI overlays via closure)
    private var isPaused  = false
    private var showResult = false

    // MARK: — Lifecycle
    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .black
        setupBackground()
        setupMetalView()
        setupBridge()
        setupHUD()
        bindMediaSync()
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        guard let bridge, let metalView else { return }
        view.layoutIfNeeded()
        bridge.attachSurface(Unmanaged.passUnretained(metalView.layer).toOpaque(),
                             width: max(1, Int(metalView.drawableSize.width)),
                             height: max(1, Int(metalView.drawableSize.height)))
        bridge.setPlayMode(playMode)
        bridge.loadChart(url: chartEntry.url)
        prepareMedia()
        bridge.setPaused(false)
        syncMedia(chartTime: bridge.chartTime)
    }

    override var supportedInterfaceOrientations: UIInterfaceOrientationMask {
        .landscape
    }

    // MARK: — Setup helpers
    private func setupBackground() {
        let imageView = UIImageView(frame: view.bounds)
        imageView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        imageView.contentMode = .scaleAspectFill
        imageView.clipsToBounds = true
        view.addSubview(imageView)
        backgroundView = imageView

        let blurView = UIVisualEffectView(effect: UIBlurEffect(style: .systemUltraThinMaterialDark))
        blurView.frame = view.bounds
        blurView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        view.addSubview(blurView)
        backgroundBlurView = blurView

        let dimView = UIView(frame: view.bounds)
        dimView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        dimView.backgroundColor = .black
        view.addSubview(dimView)
        backgroundDimView = dimView

        applyDisplaySettings()
    }

    private func setupBridge() {
        let drawableSize = metalView?.drawableSize ?? CGSize(width: view.bounds.width * view.contentScaleFactor,
                                                             height: view.bounds.height * view.contentScaleFactor)
        let w = max(1, Int(drawableSize.width))
        let h = max(1, Int(drawableSize.height))
        bridge = GameBridge(width: w, height: h,
                            respackPath: bundledRespackPath())
        if let metalView, let device = metalView.device, let bridge {
            metalView.configure(device: device, bridge: bridge, respackPath: bundledRespackPath())
        }
        // Observe chart-ended to show result screen
        bridge?.objectWillChange
            .receive(on: DispatchQueue.main)
            .sink { [weak self] _ in self?.checkChartEnd() }
            .store(in: &cancellables)
    }

    private func setupMetalView() {
        guard let device = MTLCreateSystemDefaultDevice() else { return }
        let mv = GameMetalView(frame: view.bounds, device: device)
        mv.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        view.addSubview(mv)
        metalView = mv
    }

    private func setupHUD() {
        guard let bridge else { return }
        let hudView = GameHUDView(bridge: bridge) { [weak self] in
            self?.togglePause()
        }
        let host = UIHostingController(rootView: AnyView(hudView))
        addSwiftUIOverlay(host)
        hudHost = host
    }

    private func togglePause() {
        isPaused.toggle()
        bridge?.setPaused(isPaused)
        syncMedia(chartTime: bridge?.chartTime ?? 0.0)
        if isPaused { showPauseMenu() } else { dismissOverlay() }
    }

    private func showPauseMenu() {
        let menu = PauseMenuView(
            onResume:  { [weak self] in self?.togglePause() },
            onRestart: { [weak self] in self?.restartGame() },
            onQuit:    { [weak self] in self?.quitGame() }
        )
        let host = UIHostingController(rootView: AnyView(menu))
        host.view.backgroundColor = .clear
        addSwiftUIOverlay(host)
        overlayHost = host
    }

    private func dismissOverlay() {
        overlayHost?.willMove(toParent: nil)
        overlayHost?.view.removeFromSuperview()
        overlayHost?.removeFromParent()
        overlayHost = nil
    }

    private func restartGame() {
        dismissOverlay()
        showResult = false
        isPaused   = false
        bridge?.restart()
        syncMedia(chartTime: bridge?.chartTime ?? 0.0)
    }

    private func quitGame() {
        musicPlayer?.stop()
        dismiss(animated: true) { [weak self] in self?.onDismiss?() }
    }

    // MARK: — Chart end → result screen
    private func checkChartEnd() {
        guard let bridge, bridge.chartEnded, !showResult else { return }
        showResult = true
        bridge.setPaused(true)
        showResultScreen()
    }

    private func showResultScreen() {
        guard let bridge else { return }
        let result = ResultView(
            bridge:    bridge,
            chartName: chartEntry?.displayName ?? "",
            onRetry:   { [weak self] in self?.restartGame() },
            onQuit:    { [weak self] in self?.quitGame() }
        )
        let host = UIHostingController(rootView: AnyView(result))
        host.view.backgroundColor = .clear
        addSwiftUIOverlay(host)
        overlayHost = host
    }

    private func bindMediaSync() {
        bridge?.$chartTime
            .receive(on: DispatchQueue.main)
            .sink { [weak self] in self?.syncMedia(chartTime: $0) }
            .store(in: &cancellables)
    }

    private func prepareMedia() {
        let assets = ChartMediaResolver.resolve(for: chartEntry.url)
        if let bgURL = assets.backgroundURL {
            backgroundView?.image = UIImage(contentsOfFile: bgURL.path)
        } else {
            backgroundView?.image = nil
        }

        musicPlayer = nil
        if let audioURL = assets.audioURL {
            do {
                let session = AVAudioSession.sharedInstance()
                try session.setCategory(.playback, mode: .default, options: [])
                try session.setActive(true)
                let player = try AVAudioPlayer(contentsOf: audioURL)
                player.prepareToPlay()
                player.volume = Float(GameplaySettings.musicVolume)
                musicPlayer = player
            } catch {
                print("[GameViewController] ERROR: Failed to load audio: \(error)")
            }
        }
        applyDisplaySettings()
    }

    private func applyDisplaySettings() {
        backgroundView?.isHidden = !GameplaySettings.showBackground
        backgroundBlurView?.isHidden = !GameplaySettings.showBackground
        backgroundDimView?.isHidden = !GameplaySettings.showBackground
        backgroundBlurView?.alpha = GameplaySettings.backgroundBlur
        backgroundDimView?.alpha = GameplaySettings.backgroundDim
        musicPlayer?.volume = Float(GameplaySettings.musicVolume)
    }

    private func syncMedia(chartTime: Double) {
        applyDisplaySettings()
        guard let bridge else { return }
        guard let player = musicPlayer else { return }
        let targetTime = max(0.0, chartTime - bridge.chartOffset - GameplaySettings.audioOffsetMs / 1000.0)

        if bridge.isPaused || showResult || !bridge.chartLoaded {
            if player.isPlaying { player.pause() }
            if abs(player.currentTime - targetTime) > 0.05 {
                player.currentTime = min(targetTime, player.duration)
            }
            return
        }

        let clampedTarget = min(targetTime, player.duration)
        if !player.isPlaying {
            player.currentTime = clampedTarget
            player.play()
            return
        }
        if abs(player.currentTime - clampedTarget) > 0.08 {
            player.currentTime = clampedTarget
        }
    }

    // MARK: — Overlay utility
    private func addSwiftUIOverlay(_ host: UIHostingController<AnyView>) {
        addChild(host)
        host.view.frame = view.bounds
        host.view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        host.view.backgroundColor = .clear
        view.addSubview(host.view)
        host.didMove(toParent: self)
    }

    // MARK: — Helpers
    private func bundledRespackPath() -> String? {
        Bundle.main.path(forResource: "respack", ofType: "zip")
    }

    private var cancellables = Set<AnyCancellable>()
}

// MARK: — SwiftUI wrapper (UIViewControllerRepresentable)
struct GameplayContainerView: UIViewControllerRepresentable {
    let chart: ChartEntry
    let playMode: PlayMode
    @Binding var pendingPlayMode: PlayMode?

    func makeUIViewController(context: Context) -> GameViewController {
        let vc = GameViewController()
        vc.chartEntry = chart
        vc.playMode   = playMode
        vc.modalPresentationStyle = .fullScreen
        vc.onDismiss = { pendingPlayMode = nil }
        return vc
    }

    func updateUIViewController(_ uiViewController: GameViewController, context: Context) {}
}
