param(
    [switch]$Test,
    [switch]$Run,
    [switch]$BuildRelay,
    [switch]$Benchmark
)

$ErrorActionPreference = "Stop"

$Gpp = "C:\msys64\ucrt64\bin\g++.exe"
if (-not (Test-Path $Gpp)) {
    $Gpp = "g++"
}

$CoreSources = @(
    "src/simd/simd_kernels.S",
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
    "-mavx2",
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

Write-Host "[1/3] Compiling AeroDesk.exe (C++20 + x86-64 AVX2 Assembly)..." -ForegroundColor Cyan
& $Gpp @CommonFlags -mwindows @AppSources -o "AeroDesk.exe" @Libs
if ($LASTEXITCODE -ne 0) {
    throw "Failed to compile AeroDesk.exe"
}
Write-Host "  -> Built AeroDesk.exe ($((Get-Item 'AeroDesk.exe').Length / 1KB -as [int]) KB)" -ForegroundColor Green

if ($BuildRelay -or $Test -or $Benchmark) {
    Write-Host "[2/3] Building C# (.NET 10) Standalone Relay Server..." -ForegroundColor Cyan
    & dotnet build "relay-dotnet/AeroDeskRelay.csproj" -c Release --nologo -v q
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to build C# AeroDeskRelay.csproj"
    }
    Write-Host "  -> Verified C# .NET 10 Relay Server (--self-test)..." -ForegroundColor Green
    & dotnet "relay-dotnet/bin/Release/net10.0/AeroDeskRelay.dll" --port 51998 --self-test
    if ($LASTEXITCODE -ne 0) {
        throw "C# AeroDeskRelay self-test failed"
    }
}

if ($Test) {
    Write-Host "[3/3] Compiling and running AeroDeskTests.exe..." -ForegroundColor Cyan
    & $Gpp @CommonFlags @TestSources -o "AeroDeskTests.exe" @Libs
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile AeroDeskTests.exe"
    }
    & .\AeroDeskTests.exe
    if ($LASTEXITCODE -ne 0) {
        throw "AeroDeskTests.exe reported test failures!"
    }
}

if ($Benchmark) {
    Write-Host "[Polyglot Benchmark] Running Python 3.14 Benchmark & Stress Suite..." -ForegroundColor Cyan
    & python "scripts/benchmark_suite.py"
    if ($LASTEXITCODE -ne 0) {
        throw "Python polyglot benchmark suite failed"
    }
}

if ($Run) {
    Start-Process ".\AeroDesk.exe"
}
