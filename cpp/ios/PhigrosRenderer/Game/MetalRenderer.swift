import Foundation
import Metal
import MetalKit
import simd

// MARK: — Vertex layout (matches Shaders.metal VertexIn)
struct PhigrosVertex {
    var position: SIMD2<Float>
    var texcoord: SIMD2<Float>
    var color:    SIMD4<Float>
}

// MARK: — Uniforms (matches Shaders.metal Uniforms)
struct PhigrosUniforms {
    var screenSize: SIMD2<Float>
    var center:     SIMD2<Float>
    var cosA:  Float
    var sinA:  Float
    var scaleX: Float
    var scaleY: Float
    var useTexture: Int32  // 0=color, 1=textured, 2=ring
    var ringInner: Float   // ring mode: inner-radius fraction (0..1), 0 for filled disc
}

// MARK: — MetalRenderer
final class MetalRenderer {
    private static let embeddedShaderSource = """
    #include <metal_stdlib>
    using namespace metal;

    struct VertexIn {
        float2 position [[attribute(0)]];
        float2 texcoord [[attribute(1)]];
        float4 color    [[attribute(2)]];
    };

    struct VertexOut {
        float4 position [[position]];
        float2 texcoord;
        float4 color;
    };

    struct Uniforms {
        float2 screenSize;
        float2 center;
        float  cosA;
        float  sinA;
        float  scaleX;
        float  scaleY;
        int    useTexture;  // 0=color, 1=textured, 2=ring
        float  ringInner;   // inner radius fraction for ring mode
    };

    vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                                 constant Uniforms& u [[buffer(1)]]) {
        float2 local = float2(in.position.x * u.scaleX, in.position.y * u.scaleY);
        float2 rotated = float2(u.cosA * local.x - u.sinA * local.y,
                                u.sinA * local.x + u.cosA * local.y);
        float2 world = rotated + u.center;

        float2 ndc = float2(world.x / u.screenSize.x * 2.0 - 1.0,
                            1.0 - world.y / u.screenSize.y * 2.0);
        VertexOut out;
        out.position = float4(ndc, 0.0, 1.0);
        out.texcoord = in.texcoord;
        out.color = in.color;
        return out;
    }

    fragment float4 fragment_main(VertexOut in [[stage_in]],
                                  texture2d<float> tex [[texture(0)]],
                                  sampler smp [[sampler(0)]],
                                  constant Uniforms& u [[buffer(1)]]) {
        if (u.useTexture == 2) {
            // Ring/disc mode — discard pixels outside [ringInner, 1.0] normalized radius
            float2 centered = in.texcoord * 2.0 - 1.0;
            float d = length(centered);
            if (d > 1.0 || d < u.ringInner) discard_fragment();
            return in.color;
        }
        if (u.useTexture == 1) {
            float4 sampled = tex.sample(smp, in.texcoord);
            return sampled * in.color;
        }
        return in.color;
    }
    """

    let device: MTLDevice
    let commandQueue: MTLCommandQueue
    private var pipelineState: MTLRenderPipelineState?
    private var samplerState: MTLSamplerState?
    private var respack: MetalRespack?

    // Screen size (updated on resize)
    var screenW: Float = 1280
    var screenH: Float = 720

    // Note color palette (RGBA 0-1)
    static let colorTap:   SIMD4<Float> = [1.00, 0.86, 0.47, 1]
    static let colorDrag:  SIMD4<Float> = [0.55, 0.94, 1.00, 1]
    static let colorHold:  SIMD4<Float> = [0.47, 0.78, 1.00, 1]
    static let colorFlick: SIMD4<Float> = [1.00, 0.55, 0.86, 1]
    static let colorLine:  SIMD4<Float> = [1.00, 1.00, 1.00, 1]

    // Quad vertices (unit square centered at origin, -0.5..0.5)
    private static let quadVerts: [PhigrosVertex] = [
        PhigrosVertex(position: [-0.5, -0.5], texcoord: [0, 1], color: [1,1,1,1]),
        PhigrosVertex(position: [ 0.5, -0.5], texcoord: [1, 1], color: [1,1,1,1]),
        PhigrosVertex(position: [-0.5,  0.5], texcoord: [0, 0], color: [1,1,1,1]),
        PhigrosVertex(position: [ 0.5,  0.5], texcoord: [1, 0], color: [1,1,1,1]),
    ]

    init?(device: MTLDevice, respackPath: String?) {
        self.device = device
        guard let queue = device.makeCommandQueue() else { return nil }
        commandQueue = queue
        respack = MetalRespackLoader.load(device: device, zipPath: respackPath)
        setupPipeline()
        setupSampler()
    }

    private func setupPipeline() {
        // Try default library first; fall back to any .metallib in the bundle.
        var lib = device.makeDefaultLibrary()
        if lib == nil {
            // CMake/Xcode sometimes names the library after the product — scan bundle.
            let urls = Bundle.main.urls(forResourcesWithExtension: "metallib",
                                        subdirectory: nil) ?? []
            lib = urls.compactMap { try? device.makeLibrary(URL: $0) }.first
        }
        if lib == nil {
            lib = compileBundledShaderSource()
        }
        guard let lib else {
            print("[MetalRenderer] ERROR: No usable Metal library found in bundle or embedded fallback.")
            return
        }

        guard let vert = lib.makeFunction(name: "vertex_main"),
              let frag = lib.makeFunction(name: "fragment_main") else {
            print("[MetalRenderer] ERROR: vertex_main or fragment_main not found in library.")
            return
        }

        let desc = MTLRenderPipelineDescriptor()
        desc.vertexFunction   = vert
        desc.fragmentFunction = frag

        // Vertex descriptor
        let vd = MTLVertexDescriptor()
        vd.attributes[0].format = .float2; vd.attributes[0].offset = 0;  vd.attributes[0].bufferIndex = 0
        vd.attributes[1].format = .float2; vd.attributes[1].offset = 8;  vd.attributes[1].bufferIndex = 0
        vd.attributes[2].format = .float4; vd.attributes[2].offset = 16; vd.attributes[2].bufferIndex = 0
        vd.layouts[0].stride = MemoryLayout<PhigrosVertex>.stride
        desc.vertexDescriptor = vd

        let ca = desc.colorAttachments[0]!
        ca.pixelFormat                 = .bgra8Unorm
        ca.isBlendingEnabled           = true
        ca.sourceRGBBlendFactor        = .sourceAlpha
        ca.destinationRGBBlendFactor   = .oneMinusSourceAlpha
        ca.sourceAlphaBlendFactor      = .one
        ca.destinationAlphaBlendFactor = .oneMinusSourceAlpha

        do {
            pipelineState = try device.makeRenderPipelineState(descriptor: desc)
        } catch {
            print("[MetalRenderer] ERROR: makeRenderPipelineState failed: \(error)")
        }
    }

    private func compileBundledShaderSource() -> MTLLibrary? {
        if let url = Bundle.main.url(forResource: "Shaders", withExtension: "metal"),
           let source = try? String(contentsOf: url, encoding: .utf8),
           let library = tryCompileLibrary(source: source, label: url.lastPathComponent) {
            return library
        }
        return tryCompileLibrary(source: Self.embeddedShaderSource, label: "embedded shader source")
    }

    private func tryCompileLibrary(source: String, label: String) -> MTLLibrary? {
        let options = MTLCompileOptions()
        do {
            let library = try device.makeLibrary(source: source, options: options)
            print("[MetalRenderer] Loaded Metal shaders from \(label).")
            return library
        } catch {
            print("[MetalRenderer] ERROR: Failed to compile Metal shaders from \(label): \(error)")
            return nil
        }
    }

    private func setupSampler() {
        let sd = MTLSamplerDescriptor()
        sd.minFilter = .linear; sd.magFilter = .linear; sd.mipFilter = .linear
        samplerState = device.makeSamplerState(descriptor: sd)
    }

    // MARK: — Frame rendering entry point
    func renderFrame(_ frame: phigros_frame_data,
                     commandBuffer: MTLCommandBuffer,
                     descriptor: MTLRenderPassDescriptor) {
        guard let pipelineState, let samplerState else {
            // Pipeline not ready yet — try rebuilding once (e.g. after shader compilation)
            setupPipeline()
            setupSampler()
            return
        }
        guard let enc = commandBuffer.makeRenderCommandEncoder(descriptor: descriptor) else { return }
        enc.setRenderPipelineState(pipelineState)
        enc.setFragmentSamplerState(samplerState, index: 0)

        // Draw lines — use withUnsafeBytes to iterate C fixed-size array
        withUnsafeBytes(of: frame.lines) { rawLines in
            let lines = rawLines.bindMemory(to: phigros_line_data.self)
            for i in 0 ..< Int(frame.line_count) {
                let ln = lines[i]
                let alpha = max(0, min(1, ln.alpha))
                guard alpha > 0.01 else { continue }
                let color = SIMD4<Float>(Float(ln.r)/255, Float(ln.g)/255, Float(ln.b)/255, alpha)
                let lw: Float = screenW * 0.65
                drawRect(encoder: enc, cx: ln.x, cy: ln.y,
                         w: lw, h: 4.0, rotation: ln.rot, color: color)
            }
        }

        // Draw notes — holds first so they're beneath taps
        withUnsafeBytes(of: frame.notes) { rawNotes in
            let notes = rawNotes.bindMemory(to: phigros_note_data.self)
            for i in 0 ..< Int(frame.note_count) {
                drawNote(encoder: enc, note: notes[i], samplerState: samplerState)
            }
        }

        // Draw hit-flash effects on top of notes
        drawEffects(encoder: enc, frame: frame, samplerState: samplerState)

        enc.endEncoding()
    }

    // MARK: — Note drawing
    private func drawNote(encoder enc: MTLRenderCommandEncoder,
                          note nt: phigros_note_data,
                          samplerState: MTLSamplerState) {
        let alpha = max(0, min(1, nt.alpha))
        guard alpha > 0.01 else { return }

        let isMultiHit = nt.is_mh != 0
        let noteW = 0.06 * screenW * Float(GameplaySettings.noteScale) * nt.size_px

        if nt.is_hold != 0 {
            drawHold(encoder: enc,
                     note: nt,
                     noteWidth: noteW,
                     multiHit: isMultiHit,
                     samplerState: samplerState)
        } else {
            guard let texture = respack?.noteTexture(kind: Int(nt.kind), multiHit: isMultiHit) else {
                let fallbackH: Float = (nt.kind == 2) ? 28.8 : 48.0
                drawRect(encoder: enc, cx: nt.wx, cy: nt.wy,
                         w: noteW, h: fallbackH,
                         rotation: nt.line_rot, color: noteTint(note: nt))
                return
            }
            let noteH = noteW * Float(texture.height) / Float(texture.width)
            drawTexturedQuad(encoder: enc,
                             texture: texture.texture,
                             samplerState: samplerState,
                             cx: nt.wx,
                             cy: nt.wy,
                             w: noteW,
                             h: noteH,
                             rotation: nt.line_rot + nt.skew * (.pi / 180),
                             color: noteTint(note: nt),
                             region: SIMD4<Float>(0, 0, 1, 1))
        }
    }

    private func drawHold(encoder enc: MTLRenderCommandEncoder,
                          note nt: phigros_note_data,
                          noteWidth: Float,
                          multiHit: Bool,
                          samplerState: MTLSamplerState) {
        guard let respack else {
            let dx = nt.wx - nt.wx2
            let dy = nt.wy - nt.wy2
            let len = sqrtf(dx * dx + dy * dy)
            if len > 1 {
                drawRect(encoder: enc, cx: (nt.wx + nt.wx2) * 0.5, cy: (nt.wy + nt.wy2) * 0.5,
                         w: noteWidth * 0.55, h: len,
                         rotation: atan2f(dy, dx) - .pi * 0.5,
                         color: noteTint(note: nt))
            }
            return
        }
        guard let texture = respack.holdTexture(multiHit: multiHit) else { return }

        let useMH = respack.useHoldAtlasMH(multiHit: multiHit)
        let headH = Float(useMH ? respack.config.holdHeadHMH : respack.config.holdHeadH)
        let tailH = Float(useMH ? respack.config.holdTailHMH : respack.config.holdTailH)
        let bodyH = max(1, Float(texture.height) - headH - tailH)

        let hx = nt.wx
        let hy = nt.wy
        let tx = nt.wx2
        let ty = nt.wy2
        let dx = hx - tx
        let dy = hy - ty
        let totalLen = sqrtf(dx * dx + dy * dy)
        guard totalLen >= 1 else { return }

        let angle = atan2f(dy, dx) - .pi * 0.5
        let pxPerTexel = noteWidth / Float(texture.width)
        let headScreenH = headH * pxPerTexel
        let tailScreenH = tailH * pxPerTexel
        let bodyScreenH = max(0, totalLen - headScreenH - tailScreenH)
        let ux = dx / totalLen
        let uy = dy / totalLen

        let tint = holdTint(note: nt)
        let texH = Float(texture.height)

        let tailCenter = SIMD2<Float>(tx + ux * tailScreenH * 0.5, ty + uy * tailScreenH * 0.5)
        drawTexturedQuad(encoder: enc,
                         texture: texture.texture,
                         samplerState: samplerState,
                         cx: tailCenter.x,
                         cy: tailCenter.y,
                         w: noteWidth,
                         h: tailScreenH,
                         rotation: angle,
                         color: tint,
                         region: SIMD4<Float>(0, (headH + bodyH) / texH, 1, tailH / texH))

        if bodyScreenH > 0.5 {
            let bodyCenter = SIMD2<Float>(tx + ux * (tailScreenH + bodyScreenH * 0.5),
                                          ty + uy * (tailScreenH + bodyScreenH * 0.5))
            drawTexturedQuad(encoder: enc,
                             texture: texture.texture,
                             samplerState: samplerState,
                             cx: bodyCenter.x,
                             cy: bodyCenter.y,
                             w: noteWidth,
                             h: bodyScreenH,
                             rotation: angle,
                             color: tint,
                             region: SIMD4<Float>(0, headH / texH, 1, bodyH / texH))
        }

        if nt.draw_hold_head != 0 || respack.config.holdKeepHead {
            let headCenter = SIMD2<Float>(hx - ux * headScreenH * 0.5, hy - uy * headScreenH * 0.5)
            drawTexturedQuad(encoder: enc,
                             texture: texture.texture,
                             samplerState: samplerState,
                             cx: headCenter.x,
                             cy: headCenter.y,
                             w: noteWidth,
                             h: headScreenH,
                             rotation: angle,
                             color: tint,
                             region: SIMD4<Float>(0, 0, 1, headH / texH))
        }
    }

    private func noteTint(note nt: phigros_note_data) -> SIMD4<Float> {
        var tint = SIMD4<Float>(Float(nt.r) / 255, Float(nt.g) / 255, Float(nt.b) / 255, max(0, min(1, nt.alpha)))
        if nt.miss != 0 {
            tint.w *= 0.5
        }
        return tint
    }

    private func holdTint(note nt: phigros_note_data) -> SIMD4<Float> {
        var tint = noteTint(note: nt)
        if nt.hold_hit_failed != 0 {
            tint.x = 0.5
            tint.y = 0.5
            tint.z = 0.5
            tint.w *= 0.5
        }
        return tint
    }

    private func drawTexturedQuad(encoder enc: MTLRenderCommandEncoder,
                                  texture: MTLTexture,
                                  samplerState: MTLSamplerState,
                                  cx: Float,
                                  cy: Float,
                                  w: Float,
                                  h: Float,
                                  rotation: Float,
                                  color: SIMD4<Float>,
                                  region: SIMD4<Float>) {
        var verts = MetalRenderer.quadVerts
        let u0 = region.x
        let v0 = region.y
        let u1 = region.x + region.z
        let v1 = region.y + region.w
        verts[0].texcoord = [u0, v1]
        verts[1].texcoord = [u1, v1]
        verts[2].texcoord = [u0, v0]
        verts[3].texcoord = [u1, v0]
        for i in verts.indices { verts[i].color = color }

        var uni = PhigrosUniforms(
            screenSize: [screenW, screenH],
            center: [cx, cy],
            cosA: cosf(rotation),
            sinA: sinf(rotation),
            scaleX: w,
            scaleY: h,
            useTexture: 1,
            ringInner: 0
        )

        enc.setFragmentTexture(texture, index: 0)
        enc.setFragmentSamplerState(samplerState, index: 0)
        enc.setVertexBytes(&verts, length: MemoryLayout<PhigrosVertex>.stride * 4, index: 0)
        enc.setVertexBytes(&uni, length: MemoryLayout<PhigrosUniforms>.stride, index: 1)
        enc.setFragmentBytes(&uni, length: MemoryLayout<PhigrosUniforms>.stride, index: 1)
        enc.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
    }

    // MARK: — Primitive: colored rotated rectangle
    private func drawRect(encoder enc: MTLRenderCommandEncoder,
                          cx: Float, cy: Float,
                          w: Float, h: Float,
                          rotation: Float,
                          color: SIMD4<Float>) {
        var verts = MetalRenderer.quadVerts
        for i in verts.indices { verts[i].color = color }

        var uni = PhigrosUniforms(
            screenSize: [screenW, screenH],
            center:     [cx, cy],
            cosA:  cosf(rotation),
            sinA:  sinf(rotation),
            scaleX: w,
            scaleY: h,
            useTexture: 0,
            ringInner: 0
        )

        // setVertexBytes avoids per-draw buffer allocation (data ≤ 4 KB)
        enc.setVertexBytes(&verts,
                           length: MemoryLayout<PhigrosVertex>.stride * 4,
                           index: 0)
        enc.setFragmentTexture(nil, index: 0)
        enc.setVertexBytes(&uni, length: MemoryLayout<PhigrosUniforms>.stride, index: 1)
        enc.setFragmentBytes(&uni, length: MemoryLayout<PhigrosUniforms>.stride, index: 1)
        enc.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
    }

    // MARK: — Hit-flash effects
    // Byte layout of the anonymous effects struct in phigros_frame_data:
    //   float x(0), y(4), t0(8), radius_start(12), radius_end(16)
    //   uint8 r(20), g(21), b(22), pad(23), int is_good(24)  → 28 bytes
    private static let effectStructStride = 28
    private static let flashDuration: Float = 0.18

    private func drawEffects(encoder enc: MTLRenderCommandEncoder,
                             frame: phigros_frame_data,
                             samplerState: MTLSamplerState) {
        let count = Int(frame.effect_count)
        guard count > 0 else { return }
        let chartTime = Float(frame.chart_time)

        withUnsafeBytes(of: frame.effects) { raw in
            guard let base = raw.baseAddress else { return }
            for i in 0 ..< count {
                let off = i * Self.effectStructStride
                let x      = (base + off     ).load(as: Float.self)
                let y      = (base + off +  4).load(as: Float.self)
                let t0     = (base + off +  8).load(as: Float.self)
                let rStart = (base + off + 12).load(as: Float.self)
                let rEnd   = (base + off + 16).load(as: Float.self)
                let cr     = Float((base + off + 20).load(as: UInt8.self)) / 255.0
                let cg     = Float((base + off + 21).load(as: UInt8.self)) / 255.0
                let cb     = Float((base + off + 22).load(as: UInt8.self)) / 255.0

                let progress = (chartTime - t0) / Self.flashDuration
                guard progress >= 0 && progress <= 1 else { continue }
                let alpha  = 1.0 - progress
                let radius = rStart + (rEnd - rStart) * progress
                let tint   = SIMD4<Float>(cr, cg, cb, alpha)

                if let hitFx = respack?.hitFx {
                    drawTexturedQuad(encoder: enc,
                                     texture: hitFx.texture,
                                     samplerState: samplerState,
                                     cx: x, cy: y,
                                     w: radius * 2, h: radius * 2,
                                     rotation: 0,
                                     color: tint,
                                     region: SIMD4<Float>(0, 0, 1, 1))
                } else {
                    drawRing(encoder: enc, cx: x, cy: y,
                             radius: radius, innerFraction: 0.55, color: tint)
                }
            }
        }
    }

    private func drawRing(encoder enc: MTLRenderCommandEncoder,
                          cx: Float, cy: Float,
                          radius: Float,
                          innerFraction: Float,
                          color: SIMD4<Float>) {
        var verts = MetalRenderer.quadVerts
        for i in verts.indices { verts[i].color = color }

        var uni = PhigrosUniforms(
            screenSize: [screenW, screenH],
            center: [cx, cy],
            cosA: 1, sinA: 0,
            scaleX: radius * 2, scaleY: radius * 2,
            useTexture: 2,
            ringInner: innerFraction
        )

        enc.setFragmentTexture(nil, index: 0)
        enc.setVertexBytes(&verts, length: MemoryLayout<PhigrosVertex>.stride * 4, index: 0)
        enc.setVertexBytes(&uni, length: MemoryLayout<PhigrosUniforms>.stride, index: 1)
        enc.setFragmentBytes(&uni, length: MemoryLayout<PhigrosUniforms>.stride, index: 1)
        enc.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
    }
}
