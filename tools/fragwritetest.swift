import Metal
import Foundation

// Which part of CA's in-place copy fails on the Intel GPU? A fragment shader writes texture S (pre-filled red) in a
// pass whose framebuffer is cleared green; S is read back after the command buffer completes.
// Variants: constant vs framebuffer-fetched value, with vs without raster_order_group, color output on vs off.
// Build: swiftc -O -o tools/fragwritetest tools/fragwritetest.swift
guard let dev = MTLCopyAllDevices().first(where: { $0.name.contains("Intel") }) else { fatalError("no Intel GPU") }
let src = """
#include <metal_stdlib>
using namespace metal;
struct V { float4 p [[position]]; };
vertex V vs(uint i [[vertex_id]]) { float2 q = float2((i << 1) & 2, i & 2); V v; v.p = float4(q * 2 - 1, 0, 1); return v; }
fragment float4 const_plain(V v [[stage_in]], texture2d<float, access::write> s [[texture(0)]]) {
    s.write(float4(0, 1, 0, 1), uint2(v.p.xy)); return float4(0, 1, 0, 1); }
fragment float4 const_rog(V v [[stage_in]], texture2d<float, access::write> s [[texture(0), raster_order_group(0)]]) {
    s.write(float4(0, 1, 0, 1), uint2(v.p.xy)); return float4(0, 1, 0, 1); }
fragment float4 fetch_plain(V v [[stage_in]], float4 fb [[color(0)]], texture2d<float, access::write> s [[texture(0)]]) {
    s.write(fb, uint2(v.p.xy)); return fb; }
fragment float4 fetch_rog(V v [[stage_in]], float4 fb [[color(0)]], texture2d<float, access::write> s [[texture(0), raster_order_group(0)]]) {
    s.write(fb, uint2(v.p.xy)); return fb; }
"""
let lib = try dev.makeLibrary(source: src, options: nil)
let w = 256, h = 256
let td = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: w, height: h, mipmapped: false)
td.usage = [.renderTarget, .shaderRead, .shaderWrite]; td.storageMode = .private
let fb = dev.makeTexture(descriptor: td)!, s = dev.makeTexture(descriptor: td)!
let rd = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: w, height: h, mipmapped: false)
rd.storageMode = .managed
let out = dev.makeTexture(descriptor: rd)!
let q = dev.makeCommandQueue()!

func clear(_ cb: MTLCommandBuffer, _ t: MTLTexture, _ c: MTLClearColor) {
    let rp = MTLRenderPassDescriptor()
    rp.colorAttachments[0].texture = t; rp.colorAttachments[0].loadAction = .clear
    rp.colorAttachments[0].clearColor = c; rp.colorAttachments[0].storeAction = .store
    cb.makeRenderCommandEncoder(descriptor: rp)!.endEncoding()
}
func run(_ f: String, writeMask: MTLColorWriteMask) {
    let d = MTLRenderPipelineDescriptor()
    d.vertexFunction = lib.makeFunction(name: "vs"); d.fragmentFunction = lib.makeFunction(name: f)
    d.colorAttachments[0].pixelFormat = .bgra8Unorm; d.colorAttachments[0].writeMask = writeMask
    let p: MTLRenderPipelineState
    do { p = try dev.makeRenderPipelineState(descriptor: d) } catch { print("\(f): pipeline error \(error)"); return }
    let cb = q.makeCommandBuffer()!
    clear(cb, s, MTLClearColor(red: 1, green: 0, blue: 0, alpha: 1))
    let rp = MTLRenderPassDescriptor()
    rp.colorAttachments[0].texture = fb; rp.colorAttachments[0].loadAction = .clear
    rp.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 1, blue: 0, alpha: 1); rp.colorAttachments[0].storeAction = .store
    let e = cb.makeRenderCommandEncoder(descriptor: rp)!
    e.setRenderPipelineState(p); e.setFragmentTexture(s, index: 0); e.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
    e.endEncoding()
    let b = cb.makeBlitCommandEncoder()!
    b.copy(from: s, to: out); b.synchronize(resource: out); b.endEncoding()
    cb.commit(); cb.waitUntilCompleted()
    var px = [UInt32](repeating: 0, count: w * h)
    out.getBytes(&px, bytesPerRow: w * 4, from: MTLRegionMake2D(0, 0, w, h), mipmapLevel: 0)
    let green = px.filter { $0 == 0xFF00FF00 }.count
    print(String(format: "%-12@ writeMask=%-4@ S written: %5.1f%%  (center 0x%08X)", f as NSString,
                 (writeMask == [] ? "none" : "all") as NSString, 100.0 * Double(green) / Double(w * h), px[w * h / 2 + w / 2]))
}
print("device: \(dev.name)")
for f in ["const_plain", "const_rog", "fetch_plain", "fetch_rog"] {
    run(f, writeMask: .all)
    run(f, writeMask: [])
}
