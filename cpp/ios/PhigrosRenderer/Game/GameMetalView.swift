import MetalKit
import UIKit

/// MTKView subclass.
/// • Captures UIKit touch events and forwards them to the C++ bridge.
/// • Implements MTKViewDelegate to call tick + Metal render each display frame.
final class GameMetalView: MTKView {

    var bridge: GameBridge?
    var renderer: MetalRenderer?
    var onFrameData: ((phigros_frame_data) -> Void)?  // called on render thread

    private var lastDrawTime: Double = 0
    private var frameData = phigros_frame_data()

    // MARK: — Setup
    func configure(device: MTLDevice, bridge: GameBridge, respackPath: String?) {
        self.device      = device
        self.bridge      = bridge
        self.renderer    = MetalRenderer(device: device, respackPath: respackPath)
        renderer?.screenW = Float(drawableSize.width)
        renderer?.screenH = Float(drawableSize.height)

        colorPixelFormat   = .bgra8Unorm
        clearColor         = MTLClearColor(red: 0.04, green: 0.04, blue: 0.08, alpha: 1)
        isPaused           = false
        enableSetNeedsDisplay = false
        preferredFramesPerSecond = UIScreen.main.maximumFramesPerSecond
        delegate           = self
        isMultipleTouchEnabled = true
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        renderer?.screenW = Float(drawableSize.width)
        renderer?.screenH = Float(drawableSize.height)
    }

    // MARK: — Touch → C++ bridge
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        forward(touches, phase: PHIGROS_MOBILE_TOUCH_BEGAN)
    }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        forward(touches, phase: PHIGROS_MOBILE_TOUCH_MOVED)
    }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        forward(touches, phase: PHIGROS_MOBILE_TOUCH_ENDED)
    }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        forward(touches, phase: PHIGROS_MOBILE_TOUCH_CANCELLED)
    }

    private func forward(_ touches: Set<UITouch>, phase: phigros_mobile_touch_phase) {
        guard let bridge else { return }
        for touch in touches {
            let pt  = touch.location(in: self)
            let scl = contentScaleFactor
            // Call C bridge directly — phigros_mobile_on_touch is mutex-protected,
            // so it is safe to call from any thread without an actor hop.
            bridge.sendTouchDirect(
                id:    Int32(bitPattern: UInt32(truncatingIfNeeded: ObjectIdentifier(touch).hashValue)),
                phase: phase,
                x:     Float(pt.x * scl),
                y:     Float(pt.y * scl))
        }
    }
}

// MARK: — MTKViewDelegate (runs on Metal thread)
extension GameMetalView: MTKViewDelegate {

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        renderer?.screenW = Float(size.width)
        renderer?.screenH = Float(size.height)
    }

    func draw(in view: MTKView) {
        guard let bridge, let renderer else { return }
        guard let drawable   = view.currentDrawable,
              let descriptor = view.currentRenderPassDescriptor,
              let cmdBuf     = renderer.commandQueue.makeCommandBuffer() else { return }

        // Compute dt
        let now = CACurrentMediaTime()
        let dt  = lastDrawTime == 0 ? 1.0/60.0 : min(now - lastDrawTime, 0.1)
        lastDrawTime = now

        // Tick C++ simulation and fill frame data
        let ok = bridge.tick(dt: dt, frameData: &frameData)
        guard ok else {
            cmdBuf.commit()
            return
        }

        // Render frame
        renderer.renderFrame(frameData, commandBuffer: cmdBuf, descriptor: descriptor)
        cmdBuf.present(drawable)
        cmdBuf.commit()

        // Publish HUD to main thread
        let snap = frameData
        Task { @MainActor in
            bridge.updateHUD(from: snap)
        }
    }
}
