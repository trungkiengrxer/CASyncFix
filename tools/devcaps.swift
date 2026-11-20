import Metal
import Foundation

// Asks the Intel MTLDevice the same capability questions CoreAnimation asks in CA::OGL::new_metal_context.
// Build: swiftc -O -o tools/devcaps tools/devcaps.swift
guard let dev = MTLCopyAllDevices().first(where: { $0.name.contains("Intel") }) else { fatalError("no Intel GPU") }
let obj = dev as AnyObject
print("device: \(dev.name)")
for key in ["vendorName", "isFramebufferReadSupported", "supportsRasterOrderGroups", "areRasterOrderGroupsSupported",
            "supportsYCBCRFormats12", "supportsYCBCRPackedFormats12", "supportsNativeHardwareFP16",
            "supportsTileShaders", "supportsSIMDGroup", "supportsSIMDShuffleAndFill", "supportsSIMDReduction",
            "supportsMemorylessRenderTargets", "readWriteTextureSupport", "argumentBuffersSupport",
            "areProgrammableSamplePositionsSupported", "supportsShaderBarycentricCoordinates",
            "iosurfaceReadOnlyTextureAlignmentBytes", "minConstantBufferAlignmentBytes", "maxTextureHeight2D"] {
    let sel = NSSelectorFromString(key)
    if obj.responds(to: sel) {
        print(String(format: "  %-42@ %@", key as NSString, String(describing: obj.value(forKey: key) ?? "nil") as NSString))
    } else {
        print(String(format: "  %-42@ (not implemented)", key as NSString))
    }
}
let families: [(String, MTLGPUFamily)] = [("Apple1", .apple1), ("Apple2", .apple2), ("Apple3", .apple3), ("Apple4", .apple4),
    ("Apple5", .apple5), ("Apple6", .apple6), ("Apple7", .apple7), ("Apple8", .apple8), ("Apple9", .apple9),
    ("Mac2", .mac2), ("Common1", .common1), ("Common2", .common2), ("Common3", .common3), ("Metal3", .metal3)]
print("  supportsFamily: " + families.map { "\($0.0)=\(dev.supportsFamily($0.1) ? 1 : 0)" }.joined(separator: " "))
