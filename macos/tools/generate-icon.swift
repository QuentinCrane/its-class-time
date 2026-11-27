#!/usr/bin/env swift
// 生成标准 macOS 多分辨率 AppIcon.iconset（视觉与 windows/Assets/ClassTimeIcon.svg 对齐）。
import AppKit
import CoreGraphics
import Foundation

guard CommandLine.arguments.count > 1 else {
  fputs("用法: generate-icon.swift <输出 .iconset 目录>\n", stderr)
  exit(1)
}

let iconsetURL = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: iconsetURL, withIntermediateDirectories: true)

func renderIcon(pixelSize: Int) -> Data? {
  guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB),
        let ctx = CGContext(
          data: nil,
          width: pixelSize,
          height: pixelSize,
          bitsPerComponent: 8,
          bytesPerRow: 0,
          space: colorSpace,
          bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else {
    return nil
  }

  let s = CGFloat(pixelSize) / 48.0
  ctx.scaleBy(x: s, y: s)
  // 翻转到左上角原点（与 SVG viewBox 0 0 48 48 一致）
  ctx.translateBy(x: 0, y: 48)
  ctx.scaleBy(x: 1, y: -1)

  func cgColor(_ r: Int, _ g: Int, _ b: Int, _ a: CGFloat = 1.0) -> CGColor {
    CGColor(
      srgbRed: CGFloat(r) / 255.0,
      green: CGFloat(g) / 255.0,
      blue: CGFloat(b) / 255.0,
      alpha: a
    )
  }

  // 1. 底部投影圆角矩形
  let shadowPath = CGPath(
    roundedRect: CGRect(x: 3.5, y: 4.6, width: 41, height: 41),
    cornerWidth: 10.5,
    cornerHeight: 10.5,
    transform: nil
  )
  ctx.saveGState()
  ctx.addPath(shadowPath)
  ctx.setFillColor(cgColor(39, 91, 159, 0.24))
  ctx.fillPath()
  ctx.restoreGState()

  // 2. 主体渐变圆角矩形 (#ACD6FF -> #649AE0)
  let tileRect = CGRect(x: 3.5, y: 3.0, width: 41, height: 41)
  let tilePath = CGPath(
    roundedRect: tileRect,
    cornerWidth: 10.5,
    cornerHeight: 10.5,
    transform: nil
  )
  ctx.saveGState()
  ctx.addPath(tilePath)
  ctx.clip()
  if let grad = CGGradient(
    colorsSpace: colorSpace,
    colors: [cgColor(172, 214, 255), cgColor(100, 154, 224)] as CFArray,
    locations: [0.0, 1.0]
  ) {
    ctx.drawLinearGradient(
      grad,
      start: CGPoint(x: 3.5, y: 3.0),
      end: CGPoint(x: 36.0, y: 44.0),
      options: [.drawsBeforeStartLocation, .drawsAfterEndLocation]
    )
  }
  ctx.restoreGState()

  // 外描边
  ctx.saveGState()
  ctx.addPath(tilePath)
  ctx.setStrokeColor(cgColor(49, 109, 177, 0.28))
  ctx.setLineWidth(1.0)
  ctx.strokePath()
  ctx.restoreGState()

  // 3. 白色圆盘阴影与底盘
  ctx.setFillColor(cgColor(40, 90, 148, 0.20))
  ctx.fillEllipse(in: CGRect(x: 24 - 15.0, y: 24.4 - 15.0, width: 30.0, height: 30.0))

  ctx.setFillColor(cgColor(250, 252, 255))
  ctx.fillEllipse(in: CGRect(x: 24 - 15.0, y: 23.5 - 15.0, width: 30.0, height: 30.0))

  // 4. 进度轨道
  ctx.setStrokeColor(cgColor(220, 233, 248))
  ctx.setLineWidth(1.65)
  ctx.strokeEllipse(in: CGRect(x: 24 - 11.6, y: 23.5 - 11.6, width: 23.2, height: 23.2))

  // 5. 蓝色进度弧（顶部顺时针约 110 度）
  ctx.saveGState()
  ctx.setStrokeColor(cgColor(67, 125, 195))
  ctx.setLineWidth(2.15)
  ctx.setLineCap(.round)
  let startRad = -CGFloat.pi / 2.0
  let endRad = startRad + CGFloat.pi * 0.62
  ctx.addArc(
    center: CGPoint(x: 24, y: 23.5),
    radius: 11.6,
    startAngle: startRad,
    endAngle: endRad,
    clockwise: false
  )
  ctx.strokePath()
  ctx.restoreGState()

  // 6. 表针
  ctx.saveGState()
  ctx.setStrokeColor(cgColor(34, 76, 125))
  ctx.setLineWidth(2.6)
  ctx.setLineCap(.round)
  ctx.setLineJoin(.round)
  ctx.move(to: CGPoint(x: 19.6, y: 19.5))
  ctx.addLine(to: CGPoint(x: 24.0, y: 23.5))
  ctx.addLine(to: CGPoint(x: 30.2, y: 19.9))
  ctx.strokePath()
  ctx.restoreGState()

  guard let cgImage = ctx.makeImage() else { return nil }
  let rep = NSBitmapImageRep(cgImage: cgImage)
  return rep.representation(using: .png, properties: [:])
}

let specs: [(String, Int)] = [
  ("icon_16x16.png", 16),
  ("icon_16x16@2x.png", 32),
  ("icon_32x32.png", 32),
  ("icon_32x32@2x.png", 64),
  ("icon_128x128.png", 128),
  ("icon_128x128@2x.png", 256),
  ("icon_256x256.png", 256),
  ("icon_256x256@2x.png", 512),
  ("icon_512x512.png", 512),
  ("icon_512x512@2x.png", 1024),
]

for (filename, px) in specs {
  guard let data = renderIcon(pixelSize: px) else {
    fputs("生成图标失败: \(filename)\n", stderr)
    exit(1)
  }
  try data.write(to: iconsetURL.appendingPathComponent(filename))
}
