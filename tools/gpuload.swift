import Metal
import Foundation

// Compute load on the Intel iGPU only (the default Metal device here is the NVIDIA card).
// Usage: gpuload <seconds> [gapMs] [sizeShift]
//   gapMs = 0     -> back-to-back batches (sustained load)
//   gapMs > 0     -> CPU sleeps gapMs between batches (bursty, WindowServer-like)
//   sizeShift     -> threads = 1 << sizeShift (default 18)
// Prints per-second batch count and median GPU execution time per batch (gpuEndTime - gpuStartTime).
// Same work per batch, so a longer GPU time per batch means a lower effective GPU clock.
let args = CommandLine.arguments.dropFirst().map { Double($0) ?? 0 }
let seconds = args.count > 0 ? args[0] : 8
let gapMs = args.count > 1 ? args[1] : 0
let n = 1 << Int(args.count > 2 ? args[2] : 18)
guard let dev = MTLCopyAllDevices().first(where: { $0.name.contains("Intel") }) else { fatalError("no Intel GPU") }
let src = """
#include <metal_stdlib>
using namespace metal;
kernel void spin(device float *out [[buffer(0)]], uint id [[thread_position_in_grid]]) {
    float x = float(id) * 1e-6f, y = 1.0f;
    for (int i = 0; i < 4096; i++) { x = fma(x, 1.0001f, y); y = fma(y, 0.9999f, x * 1e-7f); }
    out[id] = x + y;
}
"""
let lib = try dev.makeLibrary(source: src, options: nil)
let pso = try dev.makeComputePipelineState(function: lib.makeFunction(name: "spin")!)
let q = dev.makeCommandQueue()!
let buf = dev.makeBuffer(length: n * 4, options: .storageModePrivate)!
print("device: \(dev.name) threads=\(n) gapMs=\(gapMs)")
let start = Date(); var lastPrint = start; var batches = 0; var gpuMs: [Double] = []; var all: [Double] = []
while Date().timeIntervalSince(start) < seconds {
    let cb = q.makeCommandBuffer()!
    let enc = cb.makeComputeCommandEncoder()!
    enc.setComputePipelineState(pso); enc.setBuffer(buf, offset: 0, index: 0)
    enc.dispatchThreads(MTLSize(width: n, height: 1, depth: 1),
                        threadsPerThreadgroup: MTLSize(width: pso.threadExecutionWidth, height: 1, depth: 1))
    enc.endEncoding(); cb.commit(); cb.waitUntilCompleted()
    let g = (cb.gpuEndTime - cb.gpuStartTime) * 1000; gpuMs.append(g); all.append(g)
    batches += 1
    if gapMs > 0 { Thread.sleep(forTimeInterval: gapMs / 1000) }
    if Date().timeIntervalSince(lastPrint) >= 1 {
        let m = gpuMs.sorted()[gpuMs.count / 2]
        print(String(format: "t=%.0fs batches/s=%d gpu_ms/batch median=%.2f", Date().timeIntervalSince(start), batches, m))
        batches = 0; gpuMs = []; lastPrint = Date()
    }
}
let s = all.sorted()
print(String(format: "overall gpu_ms/batch p10=%.2f median=%.2f p90=%.2f", s[s.count / 10], s[s.count / 2], s[s.count * 9 / 10]))
