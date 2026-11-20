import Metal
import Foundation

// Memory bandwidth seen by the Intel iGPU (blit copy, read+write) and by the CPU (memcpy), for comparison.
// Build: swiftc -O -o tools/bwtest tools/bwtest.swift
guard let dev = MTLCopyAllDevices().first(where: { $0.name.contains("Intel") }) else { fatalError("no Intel GPU") }
let size = 256 << 20
let src = dev.makeBuffer(length: size, options: .storageModePrivate)!
let dst = dev.makeBuffer(length: size, options: .storageModePrivate)!
let q = dev.makeCommandQueue()!
var best = 0.0
for _ in 0..<8 {
    let cb = q.makeCommandBuffer()!
    let b = cb.makeBlitCommandEncoder()!
    for _ in 0..<4 { b.copy(from: src, sourceOffset: 0, to: dst, destinationOffset: 0, size: size) }
    b.endEncoding(); cb.commit(); cb.waitUntilCompleted()
    let s = cb.gpuEndTime - cb.gpuStartTime
    best = max(best, Double(4 * size * 2) / s / 1e9)   // bytes read + written
}
print(String(format: "GPU (%@) blit read+write: %.1f GB/s", dev.name, best))

let a = UnsafeMutableRawPointer.allocate(byteCount: size, alignment: 4096)
let c = UnsafeMutableRawPointer.allocate(byteCount: size, alignment: 4096)
memset(a, 1, size); memset(c, 2, size)
var cbest = 0.0
for _ in 0..<6 {
    let t = Date(); memcpy(c, a, size); let s = Date().timeIntervalSince(t)
    cbest = max(cbest, Double(size * 2) / s / 1e9)
}
print(String(format: "CPU memcpy (1 thread) read+write: %.1f GB/s", cbest))
