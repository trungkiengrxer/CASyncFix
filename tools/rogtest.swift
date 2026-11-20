import Metal
import Foundation

// Can the Intel GPU build CoreAnimation's ROG in-place copy shaders (framebuffer fetch + raster-order-group write)?
// Loads QuartzCore's own default.metallib on the Intel device and pairs each inplace_copy* fragment function with
// CoreAnimation's own vertex functions until one links, then reports whether the pipeline builds.
// Build: swiftc -O -o tools/rogtest tools/rogtest.swift
guard let dev = MTLCopyAllDevices().first(where: { $0.name.contains("Intel") }) else { fatalError("no Intel GPU") }
let url = URL(fileURLWithPath: "/System/Library/Frameworks/QuartzCore.framework/Versions/A/Resources/default.metallib")
let lib = try dev.makeLibrary(URL: url)
// Functions with function constants must be specialized; give every constant a zero value.
func specialized(_ name: String) -> MTLFunction? {
    guard let raw = lib.makeFunction(name: name) else { return nil }
    if raw.functionConstantsDictionary.isEmpty { return raw }
    let cv = MTLFunctionConstantValues()
    var zero = [UInt8](repeating: 0, count: 64)
    for (_, c) in raw.functionConstantsDictionary { cv.setConstantValue(&zero, type: c.type, index: c.index) }
    return try? lib.makeFunction(name: name, constantValues: cv)
}
let frags = lib.functionNames.filter { $0.contains("inplace") }.sorted()
let verts = lib.functionNames.compactMap { specialized($0) }.filter { $0.functionType == .vertex }
print("device: \(dev.name); fragment candidates: \(frags); CA vertex functions: \(verts.count)")

for name in frags {
    guard let f = specialized(name) else { print("  \(name): cannot specialize"); continue }
    var built = false; var lastErr = ""; var linkMismatch = 0
    for v in verts {
        let d = MTLRenderPipelineDescriptor()
        d.vertexFunction = v; d.fragmentFunction = f
        if let attrs = v.vertexAttributes, !attrs.isEmpty {
            // Vertex descriptor packed in buffer 0, formats derived from the shader's declared attribute types.
            let fmt: [MTLDataType: (MTLVertexFormat, Int)] = [
                .float: (.float, 4), .float2: (.float2, 8), .float3: (.float3, 12), .float4: (.float4, 16),
                .half: (.half, 2), .half2: (.half2, 4), .half3: (.half3, 6), .half4: (.half4, 8),
                .int: (.int, 4), .int2: (.int2, 8), .uint: (.uint, 4), .uint2: (.uint2, 8),
                .ushort2: (.ushort2, 4), .short2: (.short2, 4), .uchar4: (.uchar4, 4), .char4: (.char4, 4)]
            let vd = MTLVertexDescriptor(); var off = 0; var ok = true
            for a in attrs where a.isActive {
                guard let (vf, size) = fmt[a.attributeType] else { ok = false; break }
                vd.attributes[a.attributeIndex].format = vf
                vd.attributes[a.attributeIndex].offset = off
                vd.attributes[a.attributeIndex].bufferIndex = 0
                off += size
            }
            if !ok { continue }
            vd.layouts[0].stride = max(off, 4)
            d.vertexDescriptor = vd
        }
        d.colorAttachments[0].pixelFormat = .bgra8Unorm
        do {
            _ = try dev.makeRenderPipelineState(descriptor: d)
            print("  \(name): pipeline OK with CA vertex function '\(v.name)'")
            built = true; break
        } catch {
            let msg = error.localizedDescription
            if msg.contains("not found in vertex shader outputs") || msg.contains("mismatch") { linkMismatch += 1 } else { lastErr = msg }
        }
    }
    if !built { print("  \(name): NOT built. interface mismatches=\(linkMismatch), other error: \(lastErr.prefix(200))") }
}
