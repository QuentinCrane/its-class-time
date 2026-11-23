param(
    [string]$Compiler
)

$ErrorActionPreference = 'Stop'
$windowsDir = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $windowsDir 'build'
$toolDir = Join-Path $buildDir 'installer-tools'
# 安装包与日常开发共用同一个产物目录，避免 build\app 里留着旧 exe。
$appDir = Join-Path $buildDir 'app'
$releaseDir = Join-Path $buildDir 'release'

if (Get-Process -Name ClassTimeApp -ErrorAction SilentlyContinue) {
    throw 'ClassTimeApp.exe is running. Quit it from the tray before building.'
}

if (-not $Compiler) {
    $compilerOnPath = Get-Command ISCC.exe -CommandType Application -ErrorAction SilentlyContinue
    if ($compilerOnPath) {
        $Compiler = $compilerOnPath.Source
    } else {
        $Compiler = @(
            (Join-Path $env:ProgramFiles 'Inno Setup 7/ISCC.exe')
            (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7/ISCC.exe')
            (Join-Path $env:LOCALAPPDATA 'Programs/Inno Setup 7/ISCC.exe')
        ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    }
}
if (-not $Compiler -or -not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw 'Install Inno Setup 7 or pass -Compiler with the full path to ISCC.exe.'
}
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path

[xml]$package = Get-Content -LiteralPath (Join-Path $windowsDir 'Package.appxmanifest')
[xml]$dependencies = Get-Content -LiteralPath (Join-Path $windowsDir 'packages.config')
$sdkVersion = ($dependencies.packages.package | Where-Object id -EQ 'Microsoft.WindowsAppSDK').version
if ($sdkVersion -ne '1.6.250602001') {
    throw "Update the bundled runtime URL and SHA-256 for Windows App SDK $sdkVersion."
}
$sdkDir = Join-Path $windowsDir "packages/Microsoft.WindowsAppSDK.$sdkVersion"
$bootstrapDll = Join-Path $sdkDir 'runtimes/win-x64/native/Microsoft.WindowsAppRuntime.Bootstrap.dll'
$runtimeLicense = Join-Path $sdkDir 'license.txt'
if (-not (Test-Path -LiteralPath $bootstrapDll -PathType Leaf)) {
    throw 'Restore the NuGet packages before building the installer.'
}
$appVersion = ([version]$package.Package.Identity.Version).ToString(3)

$null = New-Item -ItemType Directory -Force -Path $toolDir, $releaseDir
$runtimeInstaller = Join-Path $toolDir 'WindowsAppRuntimeInstall-x64.exe'
$runtimeSha256 = 'C7CD988425B76EA087E2E1D7B096B585F853E20BB826B8F38D45A5175410A877'
if (-not (Test-Path -LiteralPath $runtimeInstaller -PathType Leaf)) {
    Write-Host 'Downloading the Windows App Runtime 1.6.9 redistributable...'
    $runtimeUrl = 'https://download.microsoft.com/download/8a854124-ff27-48d0-b82e-e819feb247bd/WindowsAppRuntimeInstall-x64.exe'
    $download = "$runtimeInstaller.download"
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -UseBasicParsing -Uri $runtimeUrl -OutFile $download
    if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $runtimeSha256) {
        throw 'The downloaded Windows App Runtime installer did not match the expected SHA-256.'
    }
    Move-Item -LiteralPath $download -Destination $runtimeInstaller
}
if ((Get-FileHash -LiteralPath $runtimeInstaller -Algorithm SHA256).Hash -ne $runtimeSha256) {
    throw "The cached runtime installer did not match the expected SHA-256. Remove it and rebuild: $runtimeInstaller"
}

& (Join-Path $PSScriptRoot 'build-unpackaged.cmd') $appDir
if ($LASTEXITCODE -ne 0) {
    throw "ClassTime app build failed with exit code $LASTEXITCODE."
}

$script = Join-Path $windowsDir 'installer/ClassTime.iss'
& $Compiler "--define=ClassTimeVersion=$appVersion" "--define=AppBuildDir=$appDir" `
    "--define=BootstrapDll=$bootstrapDll" "--define=RuntimeInstaller=$runtimeInstaller" `
    "--define=RuntimeLicense=$runtimeLicense" "--define=ReleaseDir=$releaseDir" $script
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup build failed with exit code $LASTEXITCODE."
}

Write-Host "Installer: $(Join-Path $releaseDir "ItsClassTime-Setup-$appVersion-x64.exe")"
