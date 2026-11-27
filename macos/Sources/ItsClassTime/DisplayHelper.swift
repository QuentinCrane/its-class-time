// 显示器识别、枚举与通用配色工具：
// 严格遵循 Apple AppKit 官方文档：NSScreen.screens.first 为系统主显示器（带菜单栏、原点 (0,0)），
// 不误用 NSScreen.main（其仅代表当前键盘焦点所在屏幕）。
import AppKit
import ClassTimeEngine
import SwiftUI

struct DisplayChoice: Identifiable, Equatable {
  let id: String
  let label: String
  let isPrimary: Bool
  let frame: NSRect
}

@MainActor
enum DisplayCatalog {
  /// 系统主显示器（带菜单栏、原点 (0, 0)）。
  static var primaryScreen: NSScreen? {
    NSScreen.screens.first
  }

  static func screenIdentifier(_ screen: NSScreen) -> String {
    if let number = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber {
      return "display-\(number.uint32Value)"
    }
    return screen.localizedName
  }

  static func findScreen(id: String) -> NSScreen? {
    if !id.isEmpty,
       let matched = NSScreen.screens.first(where: { screenIdentifier($0) == id }) {
      return matched
    }
    return primaryScreen
  }

  static func screenContaining(point: NSPoint) -> NSScreen? {
    if let hit = NSScreen.screens.first(where: { $0.frame.contains(point) }) {
      return hit
    }
    return primaryScreen
  }

  static func enumerateDisplays() -> [DisplayChoice] {
    let screens = NSScreen.screens
    let primary = screens.first
    let primaryFrame = primary?.frame

    var choices: [DisplayChoice] = screens.enumerated().map { idx, screen in
      let isPrimary = (idx == 0)
      let id = screenIdentifier(screen)
      let frame = screen.frame
      let width = Int(frame.width.rounded())
      let height = Int(frame.height.rounded())
      let name = screen.localizedName.isEmpty ? "显示器 \(idx + 1)" : screen.localizedName
      var label = name
      if isPrimary {
        label += "（系统主屏）"
      }
      label += " · \(width) × \(height)"

      // AppKit 全局屏幕坐标系：X 向右递增，Y 向上递增。
      if !isPrimary, let pf = primaryFrame {
        if frame.maxX <= pf.minX {
          label += " · 主屏左侧"
        } else if frame.minX >= pf.maxX {
          label += " · 主屏右侧"
        } else if frame.minY >= pf.maxY {
          label += " · 主屏上方"
        } else if frame.maxY <= pf.minY {
          label += " · 主屏下方"
        }
      }

      return DisplayChoice(id: id, label: label, isPrimary: isPrimary, frame: frame)
    }

    choices.sort { lhs, rhs in
      if lhs.isPrimary != rhs.isPrimary { return lhs.isPrimary }
      return lhs.id < rhs.id
    }
    return choices
  }
}

struct RGBAColor: Equatable {
  var r: Double
  var g: Double
  var b: Double
  var a: Double

  var swiftUIColor: Color {
    Color(.sRGB, red: r, green: g, blue: b, opacity: a)
  }
}

struct ScreenEdgeColorStop {
  var position: Double
  var rgba: RGBAColor
}

extension WidgetPalette {
  static func parseHexRGB(_ hex: String) -> (r: Int, g: Int, b: Int)? {
    guard isValidHexColor(hex) else { return nil }
    let bytes = Array(hex.utf8)
    func nibble(_ c: UInt8) -> Int {
      if c >= UInt8(ascii: "0") && c <= UInt8(ascii: "9") { return Int(c - UInt8(ascii: "0")) }
      if c >= UInt8(ascii: "a") && c <= UInt8(ascii: "f") { return Int(c - UInt8(ascii: "a")) + 10 }
      return Int(c - UInt8(ascii: "A")) + 10
    }
    return (
      r: nibble(bytes[0]) * 16 + nibble(bytes[1]),
      g: nibble(bytes[2]) * 16 + nibble(bytes[3]),
      b: nibble(bytes[4]) * 16 + nibble(bytes[5])
    )
  }

  static func color(fromHex hex: String) -> Color? {
    guard let rgb = parseHexRGB(hex) else { return nil }
    return Color(
      .sRGB,
      red: Double(rgb.r) / 255.0,
      green: Double(rgb.g) / 255.0,
      blue: Double(rgb.b) / 255.0,
      opacity: 1.0
    )
  }

  static func nsColor(fromHex hex: String) -> NSColor? {
    guard let rgb = parseHexRGB(hex) else { return nil }
    return NSColor(
      srgbRed: CGFloat(rgb.r) / 255.0,
      green: CGFloat(rgb.g) / 255.0,
      blue: CGFloat(rgb.b) / 255.0,
      alpha: 1.0
    )
  }

  static func hexString(from color: Color) -> String {
    let ns = NSColor(color)
    return hexString(from: ns)
  }

  static func hexString(from nsColor: NSColor) -> String {
    let converted = nsColor.usingColorSpace(.sRGB) ?? nsColor
    let r = Int((max(0, min(1, converted.redComponent)) * 255.0).rounded())
    let g = Int((max(0, min(1, converted.greenComponent)) * 255.0).rounded())
    let b = Int((max(0, min(1, converted.blueComponent)) * 255.0).rounded())
    return String(format: "%02X%02X%02X", r, g, b)
  }

  static func isDarkHex(_ hex: String) -> Bool {
    guard let rgb = parseHexRGB(hex) else { return false }
    return (299 * rgb.r + 587 * rgb.g + 114 * rgb.b) < 128_000
  }

  @MainActor
  static func systemBackgroundHex() -> String {
    var resolved = NSColor(srgbRed: 0.96, green: 0.96, blue: 0.98, alpha: 1.0)
    if let app = NSApp {
      app.effectiveAppearance.performAsCurrentDrawingAppearance {
        if let c = NSColor.windowBackgroundColor.usingColorSpace(.sRGB) {
          resolved = c
        }
      }
    } else if let c = NSColor.windowBackgroundColor.usingColorSpace(.sRGB) {
      resolved = c
    }
    return hexString(from: resolved)
  }

  @MainActor
  static func kindRGBA(_ kind: SegmentKind) -> RGBAColor {
    var resolved = NSColor(srgbRed: 0.31, green: 0.42, blue: 0.93, alpha: 1.0)
    let source = kindNSColor(kind)
    if let app = NSApp {
      app.effectiveAppearance.performAsCurrentDrawingAppearance {
        if let c = source.usingColorSpace(.sRGB) {
          resolved = c
        }
      }
    } else if let c = source.usingColorSpace(.sRGB) {
      resolved = c
    }
    return RGBAColor(
      r: Double(resolved.redComponent),
      g: Double(resolved.greenComponent),
      b: Double(resolved.blueComponent),
      a: 1.0
    )
  }

  @MainActor
  static func screenEdgeRunRGBA(kind: SegmentKind?, elapsed: Bool) -> RGBAColor {
    guard let kind else {
      return RGBAColor(
        r: 100.0 / 255.0,
        g: 117.0 / 255.0,
        b: 137.0 / 255.0,
        a: elapsed ? (172.0 / 255.0) : (80.0 / 255.0)
      )
    }
    var base = kindRGBA(kind)
    base.a = elapsed ? 1.0 : (112.0 / 255.0)
    return base
  }

  @MainActor
  static func screenEdgeColorStops(runs: [ScreenEdgeRun]) -> [ScreenEdgeColorStop] {
    buildScreenEdgeBlendStops(runs).map { stop in
      ScreenEdgeColorStop(
        position: stop.position,
        rgba: screenEdgeRunRGBA(kind: stop.kind, elapsed: stop.elapsed)
      )
    }
  }

  static func screenEdgeColorAt(stops: [ScreenEdgeColorStop], position: Double) -> Color {
    guard !stops.isEmpty else { return .clear }
    if position <= stops[0].position {
      return stops[0].rgba.swiftUIColor
    }
    for i in 1..<stops.count {
      if position <= stops[i].position {
        let before = stops[i - 1]
        let after = stops[i]
        let dist = after.position - before.position
        let t = dist > 0 ? (position - before.position) / dist : 1.0
        let mixed = RGBAColor(
          r: before.rgba.r + (after.rgba.r - before.rgba.r) * t,
          g: before.rgba.g + (after.rgba.g - before.rgba.g) * t,
          b: before.rgba.b + (after.rgba.b - before.rgba.b) * t,
          a: before.rgba.a + (after.rgba.a - before.rgba.a) * t
        )
        return mixed.swiftUIColor
      }
    }
    return stops.last!.rgba.swiftUIColor
  }
}
