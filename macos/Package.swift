// swift-tools-version: 5.9
import PackageDescription

let package = Package(
  name: "ItsClassTime",
  platforms: [
    .macOS(.v13)
  ],
  products: [
    .library(name: "ClassTimeEngine", targets: ["ClassTimeEngine"]),
    .executable(name: "ItsClassTime", targets: ["ItsClassTime"]),
    .executable(name: "engine-tests", targets: ["EngineTestsRunner"]),
  ],
  targets: [
    .target(
      name: "ClassTimeEngine",
      path: "Sources/ClassTimeEngine"
    ),
    .executableTarget(
      name: "ItsClassTime",
      dependencies: ["ClassTimeEngine"],
      path: "Sources/ItsClassTime"
    ),
    .executableTarget(
      name: "EngineTestsRunner",
      dependencies: ["ClassTimeEngine"],
      path: "Tests/EngineTestsRunner"
    ),
  ]
)
