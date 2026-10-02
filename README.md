<p align="center">
  <img src="assets/icon.png" width="112" height="112" alt="AeroDesk App Icon" />
</p>

<h1 align="center">AeroDesk — Standalone Windows Remote Desktop (<code>AeroDesk.exe</code>)</h1>

<p align="center">
  <a href="https://github.com/oocs07/Remote-Desktop/releases/latest"><strong>⬇ Download Latest Release (<code>AeroDesk.exe</code> v2.1.0)</strong></a>
</p>

AeroDesk is a zero-install, hardware-accelerated Remote Desktop application for Windows distributed as a single portable executable (**`AeroDesk.exe`**). Built with **C++20** and hand-tuned **x86-64 AVX2 SIMD Assembly**, it delivers ultra-low-latency remote screen sharing, input control, clipboard synchronization, chunked file transfer, interactive remote terminal, TCP port forwarding, real-time remote audio streaming, privacy screen mode, bidirectional whiteboard annotations, and live encrypted chat using a **9-Digit Desk ID** (`XXX XXX XXX`) and password or interactive approval.

---

## Architecture Overview

| Layer / Component | Language & Technology | Purpose & Measured Impact |
| :--- | :--- | :--- |
| **Standalone Desktop App (`AeroDesk.exe`)** | **C++20** (Win32, Direct2D, DirectWrite, DXGI, WASAPI, Windows CNG) | Single-file portable Host & Viewer GUI with White & Blue / Black & Blue themes, VSync-locked animations, GPU screen capture, low-latency audio, and layered annotation overlays. |
| **SIMD Hot-Loop Kernels (`src/simd/`)** | **x86-64 AVX2 Assembly (`.S`)** | 256-bit `ymm` vector instructions (`vmovdqu`, `vpcmpeqb`, `vpmovmskb`, `vpxor`) for 64×64 dirty-tile hashing/diffing (**~80 GB/s, ~20× faster than scalar C++**) and stream cipher XOR. |
| **Dedicated Relay Server (`relay-dotnet/`)** | **C# / .NET 10 (`AeroDeskRelay`)** | Optional headless `async/await` Rendezvous & TCP Bridge server for cross-subnet 9-digit Desk ID routing. |
| **Benchmark & Stress Harness (`scripts/`)** | **Python 3.14 (`benchmark_suite.py`)** | Automated verification and stress-testing harness for SIMD throughput and Relay concurrency. |

---

## Key Features in v2.1.0

### Real-Time Remote Audio Streaming
- **WASAPI Loopback Capture**: Captures 48 kHz stereo 16-bit PCM multimedia output directly from the host's default audio endpoint with RMS silence gating.
- **Low-Latency Win32 `waveOut` Rendering**: Multi-buffered viewer audio playback engine with zero-lag synchronization alongside video streams.
- **In-Session Audio Controls**: Quick mute toggle and volume sliders in both the main Session HUD and the macOS-style Dynamic Island toolbar.

### Host Privacy Curtain Mode
- **Screen Blackout**: Borderless topmost blackout window spanning all virtual screens utilizing `WDA_EXCLUDEFROMCAPTURE` (`0x11`). Protects confidential on-screen data from local onlookers while allowing DXGI Desktop Duplication to stream unhindered.
- **Physical Input Blockade**: Automatically activates `BlockInput(TRUE)` on the host to suppress physical keyboard and mouse tampering during remote sessions while accepting synthetic remote input.

### TCP Port Forwarding & Tunneling Manager
- **Multiplexed Encrypted Tunnels**: Non-blocking TCP proxy multiplexer (`TUNNEL_OPEN 0x19`, `TUNNEL_DATA 0x1A`, `TUNNEL_CLOSE 0x1B`) tunneling local ports to remote host services.
- **Service Presets & Telemetry**: One-click presets for RDP (`3389`), SSH (`22`), HTTP (`80`), HTTPS (`443`), and VNC (`5900`) with live transfer metrics (bytes sent/received).

### Interactive Remote Terminal Console
- **Anonymous Pipe Shell**: Spawns background `cmd.exe` or `powershell.exe` with redirected anonymous pipes on the host.
- **Integrated Monospace Console**: Monospaced terminal output drawer tab (`[ Files ] [ Chat ] [ Terminal ]`) with scrollback buffer, command prompt, and one-click admin shortcuts (`ipconfig`, `tasklist`, `netstat`, `whoami`, `Clear`).

### Enhanced Address Book & Search Filtering
- **Metadata Management**: Extend sessions with custom aliases, organizational category tags (`Work`, `Servers`, `Personal`), and notes.
- **Backwards-Compatible Persistence**: 7-field INI parser seamlessly loading and upgrading older 3-field and 4-field configurations.
- **Live Search & Filter Chips**: Real-time search bar on the Dashboard with category chip filtering by ID, alias, hostname, IP endpoint, tag, or notes.

### Bidirectional Whiteboard & Screen Annotation
- **Creative Tools**: Pen, Highlighter (0.4 alpha), Vector Arrow, decaying Laser pointer glowing dot, and Clear All (`WHITEBOARD_PACKET 0x1D`).
- **Dual Screen Rendering**: Real-time rendering over the viewer desktop canvas and simultaneous click-through transparent layered overlay on the host screen (`WS_EX_TRANSPARENT | WS_EX_LAYERED`, `LWA_COLORKEY`).
- **Floating Palette**: Floating toolbar with color pills (Red, Green, Blue, Yellow, Magenta, Cyan, White) and tool switchers.

### Push Notifications, Tray System & Lifecycle Resilience
- **Windows Push Notifications**: Native Windows notifications and taskbar icon flashing (`FlashWindowEx`) for connection requests, chat messages, and file transfers.
- **Tray Icon Responsiveness**: Full `NOTIFYICON_VERSION_4` compatibility for single-click, double-click, and context menu actions (`IDM_TRAY_RESTORE`, `IDM_TRAY_QUIT`).
- **Clean Process Teardown**: Guaranteed clean process shutdown on quit with zero orphan background threads, audio endpoints, or leaked handles.

---

## Core Remote Desktop Features

- **Multi-Monitor Display Switching (`MONITOR_LIST` & `MONITOR_SELECT`)**: Live display switcher (`Ctrl+Alt+[1-9]`) with automatic cursor coordinate normalization.
- **Remote Administration Suite**: Quick actions for `LockWorkstation()`, `ShowDesktop` (`Win+D`), `Task Manager / Security Desktop` (`sas.dll` / `taskmgr.exe`), and `Emergency Reboot` (`ExitWindowsEx`).
- **DXGI `ACCESS_LOST` Instant GDI Fallback**: Bridges to GDI capture with zero dropped frames during UAC prompts, display resolution changes, or GPU mode switches.
- **Client Auto-Reconnect**: Automatic 3-attempt backoff reconnect loop (1s, 3s, 5s) preserving canvas and crypto credentials.
- **Direct2D UI/UX Engine**: Smooth 60 FPS hardware-rendered UI with White & Blue / Black & Blue themes, macOS-style Dynamic Island pill, and keyboard shortcut modal (`F1` / `?`).

---

## How to Use

### 1. Run `AeroDesk.exe`
Download **`AeroDesk.exe`** from [**GitHub Releases**](https://github.com/oocs07/Remote-Desktop/releases/latest) (or run it directly from the repository root):
```powershell
.\AeroDesk.exe
```
To run multiple isolated instances on the same PC for local testing:
```powershell
.\AeroDesk.exe --instance 1
.\AeroDesk.exe --instance 2
```

### 2. Connect to a Remote Desk
1. On the **Host PC**, copy the **9-Digit AeroDesk Address** (`XXX XXX XXX`) and the **6-Character One-Time Session Code** (or configure a permanent unattended password).
2. On the **Viewer PC**, enter the Host's 9-digit ID (or click a discovered LAN Desk card), enter the password/code (or leave blank to request interactive approval on the Host), and click **Connect to Desk**.

---

## Building & Testing from Source

### Prerequisites
- **Windows 10 / 11 (x64)**
- **MSYS2 MinGW-w64 UCRT64 (`g++` & GNU Assembler `as`)** with static `libzstd`

### Build & Verification Commands
```powershell
# Compile standalone AeroDesk.exe (C++20 + x86-64 AVX2 Assembly)
powershell -ExecutionPolicy Bypass -File .\build.ps1

# Compile and run all 266 automated assertions in AeroDeskTests.exe
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Test

# Run full build, test suite, and Python benchmark harness
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Test -Benchmark
```

---

## Project Structure

```text
├── AeroDesk.exe                         # Standalone portable Windows executable (C++20 + AVX2 ASM)
├── build.ps1                            # PowerShell build & verification script
├── src/
│   ├── main.cpp                         # WinMain entry point & CLI argument parser
│   ├── simd/
│   │   ├── simd_kernels.hpp             # CPUID AVX2 detection & extern "C" assembly declarations
│   │   └── simd_kernels.S               # x86-64 AVX2 SIMD assembly (tile diff, hash & stream cipher XOR)
│   ├── core/
│   │   ├── protocol.hpp                 # Wire protocol headers, opcodes, FPS, audio & tunnel types
│   │   └── crypto_identity.hpp/.cpp     # 9-digit Desk ID, Windows CNG SHA-256, AVX2 stream cipher, INI
│   ├── capture/
│   │   └── screen_capture.hpp/.cpp      # DXGI Desktop Duplication, GDI fallback, Zstd/JPEG tile codec
│   ├── control/
│   │   ├── input_injector.hpp/.cpp      # Win32 SendInput mouse/keyboard injection & modifier release
│   │   ├── clipboard_file_manager.hpp/.cpp # Clipboard sync & chunked file transfer with CNG SHA-256
│   │   └── whiteboard_manager.hpp/.cpp  # Screen annotation, stroke serialization & layered window
│   ├── net/
│   │   └── network_engine.hpp/.cpp      # UDP discovery, TCP Relay, E2EE, WASAPI audio & TCP tunneling
│   └── ui/
│       ├── ui_window.hpp/.cpp           # Direct2D / DirectWrite UI, custom cursor & spring animations
│       └── notification_manager.hpp/.cpp # Windows toast, taskbar flashing & tray context menu
├── relay-dotnet/
│   ├── AeroDeskRelay.csproj             # Optional C# .NET 10 Headless Rendezvous & Relay Server
│   └── Program.cs                       # Async TCP Relay & self-test implementation
├── scripts/
│   └── benchmark_suite.py               # Python 3.14 benchmark & stress verification suite
└── tests/
    └── test_suite.cpp                   # Automated C++20 + AVX2 integration test suite (266 assertions)
```

---

## Acknowledgments & Credits

AeroDesk proudly draws architectural and feature inspiration from [**RustDesk**](https://github.com/rustdesk/rustdesk), an outstanding open-source remote desktop software. We express our sincere appreciation to the RustDesk project, its maintainers, and community for pioneering open-source remote desktop capabilities.

### Copyright & Licensing Notice
- **Independent Clean-Room Implementation**: AeroDesk is an independent software project developed in native C++20 and x86-64 AVX2 SIMD Assembly, using standard Windows operating system APIs (Win32, Direct2D, DirectWrite, DXGI Desktop Duplication, WASAPI Audio, and Windows CNG Cryptography).
- **No Shared Code or Binaries**: AeroDesk does **not** copy, bundle, link against, or distribute any source code, libraries, or binaries from the RustDesk codebase (which is licensed under the GNU AGPL-3.0). Consequently, AeroDesk does not trigger AGPL copyleft obligations and is distributed independently under its own permissive [MIT License](LICENSE).
- **Trademark Disclaimer**: "RustDesk" is a trademark of its respective owners. AeroDesk is an independent project and is not affiliated with, sponsored by, or endorsed by Pursuit Technology Ltd. or the RustDesk project.

---

## License

Licensed under the [MIT License](LICENSE). Copyright (c) 2026 oocs07.
