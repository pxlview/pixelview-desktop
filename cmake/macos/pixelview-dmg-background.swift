// Regenerates cmake/macos/resources/pixelview-dmg-background.tiff, the install
// window backdrop behind "Pixelview Desktop.app" -> Applications in the release DMG.
//
//   xcrun swiftc -o /tmp/dmg-bg cmake/macos/pixelview-dmg-background.swift
//   /tmp/dmg-bg cmake/macos/resources/pixelview-dmg-background.tiff
//
// (Compile it; the Swift interpreter does not link AppKit.)
//
// Draws the 1x and 2x (Retina) representations and joins them with tiffutil so
// Finder picks the sharp one. The geometry must match pixelview-dmg-layout.applescript:
// a 640x480 pt window, 120 pt icons centred at (170,190) and (470,190), and the
// Licenses folder centred at (320,365) on the lower band.
import AppKit
import Foundation

let width = 640.0, height = 480.0
let rowY = 190.0, bandTop = 300.0

func render(scale: Double, to url: URL) {
    let pixelsWide = Int(width * scale), pixelsHigh = Int(height * scale)
    let space = CGColorSpace(name: CGColorSpace.sRGB)!
    let ctx = CGContext(data: nil, width: pixelsWide, height: pixelsHigh, bitsPerComponent: 8, bytesPerRow: 0,
                        space: space, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)!
    ctx.scaleBy(x: scale, y: scale)
    // Flip to Finder's top-left origin so coordinates read like the AppleScript layout.
    ctx.translateBy(x: 0, y: height)
    ctx.scaleBy(x: 1, y: -1)

    func color(_ hex: UInt32, _ alpha: Double = 1) -> CGColor {
        CGColor(srgbRed: Double((hex >> 16) & 0xff) / 255, green: Double((hex >> 8) & 0xff) / 255,
                blue: Double(hex & 0xff) / 255, alpha: alpha)
    }

    // Soft light backdrop: Finder keeps icon labels dark on it in both appearances.
    let gradient = CGGradient(colorsSpace: space, colors: [color(0xF6F6F9), color(0xEDEDF2)] as CFArray,
                              locations: [0, 1])!
    ctx.drawLinearGradient(gradient, start: CGPoint(x: 0, y: 0), end: CGPoint(x: 0, y: bandTop), options: [])

    // Lower band that holds the Licenses folder, set apart from the install gesture.
    ctx.setFillColor(color(0xE4E4EA))
    ctx.fill(CGRect(x: 0, y: bandTop, width: width, height: height - bandTop))
    ctx.setFillColor(color(0xD6D6DE))
    ctx.fill(CGRect(x: 0, y: bandTop, width: width, height: 1))

    // Chevron between the app and Applications.
    let cx = width / 2, arm = 26.0, half = 13.0
    ctx.setStrokeColor(color(0x2B2D35))
    ctx.setLineWidth(9)
    ctx.setLineCap(.round)
    ctx.setLineJoin(.round)
    ctx.move(to: CGPoint(x: cx - half, y: rowY - arm))
    ctx.addLine(to: CGPoint(x: cx + half, y: rowY))
    ctx.addLine(to: CGPoint(x: cx - half, y: rowY + arm))
    ctx.strokePath()

    // Short instruction under the chevron row. NSString drawing needs a flipped
    // NSGraphicsContext bound to the same CGContext.
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: true)
    let style = NSMutableParagraphStyle()
    style.alignment = .center
    let caption = NSAttributedString(string: "Drag Pixelview Desktop to Applications to install", attributes: [
        .font: NSFont.systemFont(ofSize: 13, weight: .medium),
        .foregroundColor: NSColor(cgColor: color(0x6E6F78))!,
        .paragraphStyle: style,
    ])
    caption.draw(in: CGRect(x: 0, y: 32, width: width, height: 20))
    NSGraphicsContext.restoreGraphicsState()

    let image = ctx.makeImage()!
    let rep = NSBitmapImageRep(cgImage: image)
    rep.size = NSSize(width: width, height: height)  // 72 dpi at 1x, 144 dpi at 2x
    try! rep.representation(using: .png, properties: [:])!.write(to: url)
}

guard CommandLine.arguments.count == 2 else {
    FileHandle.standardError.write("usage: pixelview-dmg-background.swift <output.tiff>\n".data(using: .utf8)!)
    exit(2)
}
let output = URL(fileURLWithPath: CommandLine.arguments[1])
let work = FileManager.default.temporaryDirectory.appendingPathComponent("pixelview-dmg-bg-\(getpid())")
try FileManager.default.createDirectory(at: work, withIntermediateDirectories: true)
defer { try? FileManager.default.removeItem(at: work) }
let one = work.appendingPathComponent("background.png"), two = work.appendingPathComponent("background@2x.png")
render(scale: 1, to: one)
render(scale: 2, to: two)
let tiffutil = Process()
tiffutil.executableURL = URL(fileURLWithPath: "/usr/bin/tiffutil")
tiffutil.arguments = ["-cathidpicheck", one.path, two.path, "-out", output.path]
try tiffutil.run()
tiffutil.waitUntilExit()
exit(tiffutil.terminationStatus)
