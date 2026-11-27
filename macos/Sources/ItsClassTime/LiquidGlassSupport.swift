// macOS 26+ / 27+ 官方 Liquid Glass（液态玻璃）适配层：
// - AppKit: NSGlassEffectView (.clear / .regular, macOS 26+) 与 effectIsInteractive (macOS 27+)
// - SwiftUI: .glassEffect(.clear / .regular, in:) / GlassEffectContainer / .buttonStyle(.glass) /
//            .scrollEdgeEffectStyle(.soft) / .containerBackground(.clear, for: .window)
// - macOS 13–25 自动回退至原生半透明材质；系统开启“减少透明度 / 增强对比度”时自动保底高可读性。
import AppKit
import ClassTimeEngine
import SwiftUI

enum LiquidGlassCapabilities {
  static var isAvailable: Bool {
    if #available(macOS 26.0, *) {
      return true
    }
    return false
  }

  static var isInteractiveAppKitGlassAvailable: Bool {
    if #available(macOS 27.0, *) {
      return true
    }
    return false
  }

  static var isExpandedStatusItemAvailable: Bool {
    if #available(macOS 27.0, *) {
      return true
    }
    return false
  }

  static var capabilitySummary: String {
    if #available(macOS 27.0, *) {
      return "Liquid Glass 原生材质（通透折射 + 交互式液态玻璃与状态栏面板，macOS 27+）"
    }
    if #available(macOS 26.0, *) {
      return "Liquid Glass 原生材质（macOS 26+）"
    }
    return "macOS 原生半透明材质（升级至 macOS 26+ 自动启用 Liquid Glass）"
  }
}

// MARK: - AppKit NSPanel 液态玻璃宿主封装

@MainActor
final class GlassPanelHost<Content: View> {
  let hostingView: NSHostingView<Content>
  let rootView: NSView
  private var glassBackingView: NSView?

  init(
    rootView content: Content,
    frame: NSRect,
    cornerRadius: CGFloat,
    interactive: Bool,
    clearStyle: Bool = true
  ) {
    let hosting = NSHostingView(rootView: content)
    // 遵循官方 AppKit + SwiftUI 混排建议：清空 sizingOptions，避免 SwiftUI 固有尺寸与手工 NSPanel 布局互相拉扯。
    hosting.sizingOptions = []
    hosting.frame = NSRect(origin: .zero, size: frame.size)
    hosting.autoresizingMask = [.width, .height]
    self.hostingView = hosting

    if #available(macOS 26.0, *) {
      let glass = NSGlassEffectView(frame: NSRect(origin: .zero, size: frame.size))
      glass.autoresizingMask = [.width, .height]
      glass.cornerRadius = cornerRadius
      let forceRegular =
        NSWorkspace.shared.accessibilityDisplayShouldReduceTransparency ||
        NSWorkspace.shared.accessibilityDisplayShouldIncreaseContrast
      glass.style = (clearStyle && !forceRegular) ? .clear : .regular
      if #available(macOS 27.0, *) {
        glass.effectIsInteractive = interactive
      }
      glass.contentView = hosting
      self.glassBackingView = glass
      self.rootView = glass
    } else {
      self.glassBackingView = nil
      self.rootView = hosting
    }
  }

  func updateAppearance(
    size: NSSize? = nil,
    cornerRadius: CGFloat,
    tintColor: NSColor?,
    interactive: Bool,
    clearStyle: Bool = true
  ) {
    if let size {
      let bounds = NSRect(origin: .zero, size: size)
      rootView.frame = bounds
      hostingView.frame = bounds
    }
    guard #available(macOS 26.0, *), let glass = glassBackingView as? NSGlassEffectView else {
      return
    }
    glass.cornerRadius = max(0, cornerRadius)

    // 遵循 Apple HIG 无障碍规范：开启“减少透明度”或“增强对比度”时自动回退至更稳重的 .regular 且不叠加着色。
    let reduceTransparency = NSWorkspace.shared.accessibilityDisplayShouldReduceTransparency
    let increaseContrast = NSWorkspace.shared.accessibilityDisplayShouldIncreaseContrast
    if reduceTransparency || increaseContrast {
      glass.style = .regular
      glass.tintColor = nil
    } else {
      glass.style = clearStyle ? .clear : .regular
      // 高通透 .clear 模式下不叠加浊色 tint，让桌面壁纸与底层窗口光线自然折射穿过玻璃。
      glass.tintColor = clearStyle ? nil : tintColor
    }

    if #available(macOS 27.0, *) {
      glass.effectIsInteractive = interactive
    }
  }
}

// MARK: - 设置窗口背后通透采样层（让桌面与底层光线自然透过设置窗，衬托上层 Liquid Glass 卡片）

struct WindowTranslucentBackdrop: NSViewRepresentable {
  var clearStyle: Bool

  func makeNSView(context: Context) -> NSVisualEffectView {
    let view = NSVisualEffectView()
    view.blendingMode = .behindWindow
    view.state = .active
    applyMaterial(to: view)
    return view
  }

  func updateNSView(_ nsView: NSVisualEffectView, context: Context) {
    nsView.blendingMode = .behindWindow
    nsView.state = .active
    applyMaterial(to: nsView)
  }

  private func applyMaterial(to view: NSVisualEffectView) {
    if NSWorkspace.shared.accessibilityDisplayShouldReduceTransparency {
      view.material = .windowBackground
    } else {
      // .hudWindow 在 macOS 上具有最清透的背后采样透光度；标准玻璃档位采用更克制的 .underWindowBackground。
      view.material = clearStyle ? .hudWindow : .underWindowBackground
    }
  }
}

// MARK: - SwiftUI Liquid Glass 视图修饰与容器（兼容 macOS 13–27）

struct AdaptiveGlassGroup<Content: View>: View {
  let spacing: CGFloat
  let content: Content

  init(spacing: CGFloat = 10, @ViewBuilder content: () -> Content) {
    self.spacing = spacing
    self.content = content()
  }

  var body: some View {
    if #available(macOS 26.0, *) {
      GlassEffectContainer(spacing: spacing) {
        content
      }
    } else {
      content
    }
  }
}

struct AdaptiveGlassButtonModifier: ViewModifier {
  let prominent: Bool

  func body(content: Content) -> some View {
    if #available(macOS 26.0, *) {
      if prominent {
        content.buttonStyle(.glassProminent)
      } else {
        content.buttonStyle(.glass)
      }
    } else {
      if prominent {
        content.buttonStyle(.borderedProminent)
      } else {
        content.buttonStyle(.bordered)
      }
    }
  }
}

struct AdaptiveScrollEdgeModifier: ViewModifier {
  let edges: Edge.Set

  func body(content: Content) -> some View {
    if #available(macOS 26.0, *) {
      content.scrollEdgeEffectStyle(.soft, for: edges)
    } else {
      content
    }
  }
}

extension View {
  func adaptiveGlassButton(prominent: Bool = false) -> some View {
    modifier(AdaptiveGlassButtonModifier(prominent: prominent))
  }

  func adaptiveScrollEdgeEffect(for edges: Edge.Set = .top) -> some View {
    modifier(AdaptiveScrollEdgeModifier(edges: edges))
  }

  @ViewBuilder
  func adaptiveClearWindowContainer() -> some View {
    if #available(macOS 15.0, *) {
      self.containerBackground(.clear, for: .window)
    } else {
      self
    }
  }

  @ViewBuilder
  func adaptiveTranslucentSheet() -> some View {
    if #available(macOS 13.3, *) {
      self.presentationBackground(.ultraThinMaterial)
    } else {
      self
    }
  }

  @ViewBuilder
  func adaptiveGlassCard(
    cornerRadius: CGFloat = 16,
    clearStyle: Bool = true,
    tint: Color? = nil,
    interactive: Bool = false
  ) -> some View {
    if #available(macOS 26.0, *) {
      self.modifier(
        AdaptiveGlassSurfaceModifier(
          cornerRadius: cornerRadius,
          clearStyle: clearStyle,
          tint: tint,
          interactive: interactive
        )
      )
    } else {
      self
        .background(
          RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
            .fill(clearStyle ? AnyShapeStyle(.ultraThinMaterial) : AnyShapeStyle(.regularMaterial))
        )
        .overlay(
          RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
            .stroke(Color.primary.opacity(0.11), lineWidth: 1)
        )
    }
  }

  @ViewBuilder
  func adaptiveGlassCapsule(
    tint: Color? = nil,
    clearStyle: Bool = true,
    interactive: Bool = false
  ) -> some View {
    if #available(macOS 26.0, *) {
      self.modifier(
        AdaptiveGlassCapsuleModifier(
          tint: tint,
          clearStyle: clearStyle,
          interactive: interactive
        )
      )
    } else {
      self
        .background(
          Capsule()
            .fill(tint.map { $0.opacity(0.14) } ?? Color.primary.opacity(0.07))
        )
        .overlay(
          Capsule()
            .stroke(tint.map { $0.opacity(0.42) } ?? Color.primary.opacity(0.10), lineWidth: 1)
        )
    }
  }
}

@available(macOS 26.0, *)
private struct AdaptiveGlassSurfaceModifier: ViewModifier {
  @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
  @Environment(\.colorSchemeContrast) private var contrast
  let cornerRadius: CGFloat
  let clearStyle: Bool
  let tint: Color?
  let interactive: Bool

  func body(content: Content) -> some View {
    let shape = RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
    let highLegibility = reduceTransparency || (contrast == .increased)
    let glass: Glass = {
      var g: Glass = (clearStyle && !highLegibility) ? .clear : .regular
      if !highLegibility, let tint {
        g = g.tint(tint)
      }
      if interactive {
        g = g.interactive(true)
      }
      return g
    }()

    content
      .glassEffect(glass, in: shape)
      .overlay {
        if contrast == .increased {
          shape.stroke(Color.primary.opacity(0.42), lineWidth: 1.1)
        }
      }
  }
}

@available(macOS 26.0, *)
private struct AdaptiveGlassCapsuleModifier: ViewModifier {
  @Environment(\.accessibilityReduceTransparency) private var reduceTransparency
  @Environment(\.colorSchemeContrast) private var contrast
  let tint: Color?
  let clearStyle: Bool
  let interactive: Bool

  func body(content: Content) -> some View {
    let highLegibility = reduceTransparency || (contrast == .increased)
    let glass: Glass = {
      var g: Glass = (clearStyle && !highLegibility) ? .clear : .regular
      if !highLegibility, let tint {
        g = g.tint(tint.opacity(clearStyle ? 0.16 : 0.22))
      }
      if interactive {
        g = g.interactive(true)
      }
      return g
    }()
    content.glassEffect(glass, in: Capsule())
  }
}
