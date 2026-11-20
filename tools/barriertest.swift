import Metal
import Foundation

// Reproduces CoreAnimation's ROG in-place copy pattern (MaskCorners begin/finish) on the Intel GPU, in ONE render pass:
//   draw 1: fragment reads the framebuffer ([[color(0)]]) and writes it into texture S (raster_order_group write)
//   draw 2: samples S and writes it to the framebuffer
// S starts red, the framebuffer is cleared green. Correct result = green everywhere. Red = draw 2 read stale S.
// Run with barrier scope none / RenderTargets (CA today on Intel) / RenderTargets|Textures between the draws.
// Build: swiftc -O -o tools/barriertest tools/barriertest.swift ; run: ./tools/barriertest [width height]
guard let dev = MTLCopyAllDevices().first(where: { $0.name.contains("Intel") }) else { fatalError("no Intel GPU") }
let src = """
#include <metal_stdlib>
using namespace metal;
struct V { float4 p [[position]]; };
vertex V vs(uint i [[vertex_id]]) { float2 q = float2((i << 1) & 2, i & 2); V v; v.p = float4(q * 2 - 1, 0, 1); return v; }
fragment float4 copyin(V v [[stage_in]], float4 fb [[color(0)]],
                       texture2d<float, access::write> s [[texture(0), raster_order_group(0)]]) {
    s.write(fb, uint2(v.p.xy));
    return fb;
}
fragment float4 red(V v [[stage_in]]) { return float4(1, 0, 0, 1); }
fragment float4 readback(V v [[stage_in]], texture2d<float> s [[texture(0)]]) {
    return s.read(uint2(v.p.xy));
}
"""
let lib = try dev.makeLibrary(source: src, options: nil)
func pipe(_ f: String) throws -> MTLRenderPipelineState {
    let d = MTLRenderPipelineDescriptor()
    d.vertexFunction = lib.makeFunction(name: "vs"); d.fragmentFunction = lib.makeFunction(name: f)
    d.colorAttachments[0].pixelFormat = .bgra8Unorm
    return try dev.makeRenderPipelineState(descriptor: d)
}
let pCopy = try pipe("copyin"), pRead = try pipe("readback"), pRed = try pipe("red")
let args = CommandLine.arguments.dropFirst().compactMap { Int($0) }
let w = args.first ?? 1920, h = args.dropFirst().first ?? 1080
let td = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: w, height: h, mipmapped: false)
td.usage = [.renderTarget, .shaderRead, .shaderWrite]; td.storageMode = .private
let fb = dev.makeTexture(descriptor: td)!
let smode = ProcessInfo.processInfo.environment["SMODE"] ?? "rt-clear"   // rt-clear (like CA) | rt-draw | rt-blit | nort-blit
let noOpt = ProcessInfo.processInfo.environment["NOOPT"] != nil             // S.allowGPUOptimizedContents = false
let sepCB = ProcessInfo.processInfo.environment["SEPCB"] != nil             // S prefill in its own command buffer
let sd = td.copy() as! MTLTextureDescriptor
if smode == "nort-blit" { sd.usage = [.shaderRead, .shaderWrite] }
if noOpt { sd.allowGPUOptimizedContents = false }
if ProcessInfo.processInfo.environment["PFV"] != nil { sd.usage.insert(.pixelFormatView) }   // MTLTextureUsagePixelFormatView = 0x10
if ProcessInfo.processInfo.environment["MANAGED"] != nil { sd.storageMode = .managed }
let s = dev.makeTexture(descriptor: sd)!
let redBuf = dev.makeBuffer(bytes: [UInt32](repeating: 0xFFFF0000, count: w * h), length: w * h * 4, options: .storageModeShared)!
let rd = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: w, height: h, mipmapped: false)
rd.storageMode = .managed
let out = dev.makeTexture(descriptor: rd)!
let q = dev.makeCommandQueue()!

func run(_ scope: MTLBarrierScope?, _ label: String) {
    var bad = 0, total = 0, sample: UInt32 = 0
    for _ in 0..<20 {
        var cb = q.makeCommandBuffer()!
        // S = red (stale content)
        if smode == "rt-draw" {
            let rp0 = MTLRenderPassDescriptor()
            rp0.colorAttachments[0].texture = s; rp0.colorAttachments[0].loadAction = .dontCare; rp0.colorAttachments[0].storeAction = .store
            let e0 = cb.makeRenderCommandEncoder(descriptor: rp0)!
            e0.setRenderPipelineState(pRed); e0.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3); e0.endEncoding()
        } else if smode == "rt-clear" {
            let rp0 = MTLRenderPassDescriptor()
            rp0.colorAttachments[0].texture = s; rp0.colorAttachments[0].loadAction = .clear
            rp0.colorAttachments[0].clearColor = MTLClearColor(red: 1, green: 0, blue: 0, alpha: 1); rp0.colorAttachments[0].storeAction = .store
            cb.makeRenderCommandEncoder(descriptor: rp0)!.endEncoding()
        } else {
            let b0 = cb.makeBlitCommandEncoder()!
            b0.copy(from: redBuf, sourceOffset: 0, sourceBytesPerRow: w * 4, sourceBytesPerImage: w * h * 4,
                    sourceSize: MTLSizeMake(w, h, 1), to: s, destinationSlice: 0, destinationLevel: 0, destinationOrigin: MTLOriginMake(0, 0, 0))
            b0.endEncoding()
        }
        if sepCB { cb.commit(); cb.waitUntilCompleted(); cb = q.makeCommandBuffer()! }
        let rp = MTLRenderPassDescriptor()
        rp.colorAttachments[0].texture = fb; rp.colorAttachments[0].loadAction = .clear
        rp.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 1, blue: 0, alpha: 1); rp.colorAttachments[0].storeAction = .store
        let e = cb.makeRenderCommandEncoder(descriptor: rp)!
        e.setRenderPipelineState(pCopy); e.setFragmentTexture(s, index: 0); e.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        if let scope { e.memoryBarrier(scope: scope, after: .fragment, before: .fragment) }
        e.setRenderPipelineState(pRead); e.setFragmentTexture(s, index: 0); e.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        e.endEncoding()
        let b = cb.makeBlitCommandEncoder()!
        b.copy(from: fb, to: out); b.synchronize(resource: out); b.endEncoding()
        cb.commit(); cb.waitUntilCompleted()
        var px = [UInt32](repeating: 0, count: w * h)
        out.getBytes(&px, bytesPerRow: w * 4, from: MTLRegionMake2D(0, 0, w, h), mipmapLevel: 0)
        for p in px { total += 1; if p != 0xFF00FF00 { bad += 1 } }   // BGRA green = 0xFF00FF00
        sample = px[w * h / 2 + w / 2]
    }
    // S after the pass: did the raster-order-group write land at all?
    let cb = q.makeCommandBuffer()!, b = cb.makeBlitCommandEncoder()!
    b.copy(from: s, to: out); b.synchronize(resource: out); b.endEncoding(); cb.commit(); cb.waitUntilCompleted()
    var sp = [UInt32](repeating: 0, count: 1)
    out.getBytes(&sp, bytesPerRow: w * 4, from: MTLRegionMake2D(w / 2, h / 2, 1, 1), mipmapLevel: 0)
    print(String(format: "  framebuffer center 0x%08X, S center after pass 0x%08X", sample, sp[0]))
    print(String(format: "%-28@ wrong pixels: %d / %d (%.2f%%)", label as NSString, bad, total, 100.0 * Double(bad) / Double(total)))
}
print("device: \(dev.name), S mode \(smode), noOpt \(noOpt), sepCB \(sepCB), \(w)x\(h)")
run(nil, "no barrier")
run(.renderTargets, "RenderTargets (CA today)")
run([.renderTargets, .textures], "RenderTargets|Textures")
run(.textures, "Textures")
