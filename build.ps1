param(
    [switch]$Test,
    [switch]$Run
)

$ErrorActionPreference = "Stop"

$Gpp = "C:\msys64\ucrt64\bin\g++.exe"
if (-not (Test-Path $Gpp)) {
    $Gpp = "g++"
}

$CoreSources = @(
    "src/core/crypto_identity.cpp",
    "src/capture/screen_capture.cpp",
    "src/control/input_injector.cpp",
    "src/control/clipboard_file_manager.cpp",
    "src/net/network_engine.cpp"
)

$AppSources = $CoreSources + @(
    "src/ui/ui_window.cpp",
    "src/main.cpp"
)

$TestSources = $CoreSources + @(
    "tests/test_suite.cpp"
)

$CommonFlags = @(
    "-std=c++20",
    "-O2",
    "-Wall",
    "-Wextra",
    "-DNOMINMAX",
    "-DUNICODE",
    "-D_UNICODE",
    "-static"
)

$Libs = @(
    "-lzstd",
    "-ld3d11",
    "-ldxgi",
    "-ld2d1",
    "-ldwrite",
    "-lgdiplus",
    "-lws2_32",
    "-ldwmapi",
    "-lbcrypt",
    "-lole32",
    "-luuid",
    "-lshlwapi",
    "-lcomdlg32",
    "-lgdi32",
    "-luser32",
    "-lshell32"
)

Write-Host "[1/2] Compiling AeroDesk.exe..." -ForegroundColor Cyan
& $Gpp @CommonFlags -mwindows @AppSources -o "AeroDesk.exe" @Libs
if ($LASTEXITCODE -ne 0) {
    throw "Failed to compile AeroDesk.exe"
}
Write-Host "  -> Built AeroDesk.exe ($((Get-Item 'AeroDesk.exe').Length / 1KB -as [int]) KB)" -ForegroundColor Green

if ($Test) {
    Write-Host "[2/2] Compiling and running AeroDeskTests.exe..." -ForegroundColor Cyan
    & $Gpp @CommonFlags @TestSources -o "AeroDeskTests.exe" @Libs
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile AeroDeskTests.exe"
    }
    & .\AeroDeskTests.exe
    if ($LASTEXITCODE -ne 0) {
        throw "AeroDeskTests.exe reported test failures!"
    }
}

if ($Run) {
    Start-Process ".\AeroDesk.exe"
}
