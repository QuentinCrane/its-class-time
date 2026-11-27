@echo off
setlocal
REM Direct cl+link build for the unpackaged ClassTime app, no MSBuild/XAML compiler required.
REM Usage: tools\build-unpackaged.cmd [output-directory]

for %%i in ("%~dp0..") do set "ROOT=%%~fi"
set "PKG=%ROOT%\packages\Microsoft.WindowsAppSDK.1.6.250602001"
set "CPPWINRT=%ROOT%\packages\Microsoft.Windows.CppWinRT.2.0.240111.5"
set "OUTDIR=%ROOT%\build\app"
if not "%~1"=="" set "OUTDIR=%~f1"
set "OBJDIR=%OUTDIR%\obj"
set "EXE=%OUTDIR%\ClassTimeApp.exe"

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
  if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
      set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsall.bat"
    )
  )
)
if not exist "%VCVARS%" (
  echo Visual Studio 2022 C++ Build Tools ^(vcvarsall.bat^) not found.
  exit /b 1
)
call "%VCVARS%" x64 >nul
if errorlevel 1 exit /b 1

REM Put NuGet CppWinRT projections before the SDK's older ones, or Microsoft.UI.h fails with C2888.
set "INCLUDE=%CPPWINRT%\include;%ROOT%\Generated Files;%INCLUDE%"

set "INC=/I"%ROOT%" /I"%ROOT%\app" /I"%ROOT%\engine\include" /I"%CPPWINRT%\include" /I"%PKG%\include" /I"%ROOT%\Generated Files""
set "CFLAGS=/nologo /Zi /std:c++17 /EHsc /utf-8 /bigobj /W3 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN"

if not exist "%OBJDIR%" mkdir "%OBJDIR%"

echo [1/4] pch
cl %CFLAGS% /I"%ROOT%" /c "%ROOT%\pch.cpp" /Ycpch.h /Fp"%OBJDIR%\pch.pch" /Fo"%OBJDIR%\pch.obj" %INC% || exit /b 1

echo [2/4] app sources
for %%f in (main.cpp app\desktop_app.cpp app\settings_window.cpp app\boundary_monitor.cpp app\notification_service.cpp app\overlay_window.cpp app\services.cpp app\tray_icon.cpp) do (
  cl %CFLAGS% /I"%ROOT%" /c "%ROOT%\%%f" /Yu"pch.h" /Fp"%OBJDIR%\pch.pch" /Fo"%OBJDIR%\%%~nf.obj" %INC% || exit /b 1
)

echo [3/4] engine + resources
for %%f in (config.cpp edit_ops.cpp json.cpp schedule.cpp time_engine.cpp) do (
  cl %CFLAGS% /c "%ROOT%\engine\src\%%f" /Fo"%OBJDIR%\%%~nf.obj" %INC% || exit /b 1
)
rc /nologo /fo "%OBJDIR%\ClassTime.res" /I "%ROOT%" "%ROOT%\ClassTime.rc" || exit /b 1

echo [4/4] link
link /nologo /OUT:"%EXE%" /MACHINE:X64 /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:"%ROOT%\app.manifest" ^
  "%OBJDIR%\pch.obj" "%OBJDIR%\main.obj" "%OBJDIR%\desktop_app.obj" "%OBJDIR%\settings_window.obj" ^
  "%OBJDIR%\boundary_monitor.obj" "%OBJDIR%\notification_service.obj" "%OBJDIR%\overlay_window.obj" ^
  "%OBJDIR%\services.obj" "%OBJDIR%\tray_icon.obj" "%OBJDIR%\config.obj" "%OBJDIR%\edit_ops.obj" ^
  "%OBJDIR%\json.obj" "%OBJDIR%\schedule.obj" "%OBJDIR%\time_engine.obj" "%OBJDIR%\ClassTime.res" ^
  "%PKG%\lib\win10-x64\Microsoft.WindowsAppRuntime.Bootstrap.lib" ^
  windowsapp.lib user32.lib gdi32.lib shell32.lib dwmapi.lib advapi32.lib ole32.lib oleaut32.lib uuid.lib kernel32.lib || exit /b 1

echo BUILD OK: %EXE%
exit /b 0
