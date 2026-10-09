// Locate on-screen text in a screenshot with the Vision framework.
//
// Usage: find-text <image.png> <text>
// Prints a JSON array of {text, x, y, width, height} in image pixels (origin
// top-left, x/y = centre) for every recognised line containing <text>
// (case-insensitive). Lets the e2e gate click controls by their label instead
// of by coordinates that move between macOS versions.
import AppKit
import Foundation
import Vision

let arguments = CommandLine.arguments
guard arguments.count == 3,
      let image = NSImage(contentsOfFile: arguments[1]),
      let cgImage = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
    FileHandle.standardError.write("usage: find-text <image.png> <text>\n".data(using: .utf8)!)
    exit(2)
}
let needle = arguments[2].lowercased()
let width = Double(cgImage.width)
let height = Double(cgImage.height)

let request = VNRecognizeTextRequest()
request.recognitionLevel = .accurate
request.usesLanguageCorrection = false
try VNImageRequestHandler(cgImage: cgImage).perform([request])

var matches: [[String: Any]] = []
for observation in request.results ?? [] {
    guard let candidate = observation.topCandidates(1).first,
          candidate.string.lowercased().contains(needle) else { continue }
    let box = observation.boundingBox
    matches.append([
        "text": candidate.string,
        "x": Int((box.midX * width).rounded()),
        "y": Int(((1 - box.midY) * height).rounded()),
        "width": Int((box.width * width).rounded()),
        "height": Int((box.height * height).rounded()),
    ])
}
let data = try JSONSerialization.data(withJSONObject: matches)
print(String(data: data, encoding: .utf8)!)
