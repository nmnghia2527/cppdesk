<p align="center">
  <img src="assets/icon.png" width="96" height="96" alt="CppDesk App Icon"><br>
  <b>CppDesk</b><br>
  <span>Fast, lightweight, standalone Remote Desktop for Windows built in C++</span><br><br>
  <a href="#features">Features</a> •
  <a href="#quick-start">Quick Start</a> •
  <a href="#how-to-build-from-source">Build</a> •
  <a href="#project-structure">Structure</a> •
  <a href="#acknowledgments--credits">Credits</a> •
  <a href="https://github.com/oocs07/Remote-Desktop/releases">Releases</a>
</p>

<p align="center">
  <a href="https://github.com/oocs07/Remote-Desktop/releases/latest">
    <img src="https://img.shields.io/github/v/release/oocs07/Remote-Desktop?style=for-the-badge&color=2563EB&label=DOWNLOAD%20CPPDESK" alt="Download CppDesk">
  </a>
  <a href="https://github.com/oocs07/Remote-Desktop/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="License: MIT">
  </a>
  <img src="https://img.shields.io/badge/Platform-Windows%2010%20%2F%2011-0078D6?style=for-the-badge&logo=windows" alt="Platform: Windows">
</p>

---

CppDesk is a zero-install, hardware-accelerated Remote Desktop application for Windows distributed as a single portable executable (**`CppDesk.exe`**). It operates out of the box with no external runtime dependencies. Connection control and data remain strictly with you, protected by end-to-end authenticated encryption.

Connect across networks using a **9-Digit Desk ID** (`XXX XXX XXX`) with a permanent password or interactive host approval.

---

## Features

- **High-Performance Screen Sharing**: Smooth 60 FPS desktop streaming using DXGI Desktop Duplication with GDI fallback, multi-monitor enumeration and switching, borderless fullscreen, and live quality adjustments (High, Balanced, Fast).
- **Virtual Display Fit & Dynamic Resolution Matching**: FillAspect zoom-to-fill scaling to eliminate black bars, plus on-demand host resolution adaptation matching the viewer viewport via `ChangeDisplaySettingsExW` with automatic restoration upon disconnect.
- **Multi-Session Tabbed Management**: Browser-style session tabs with live connection status dots, unread chat message badges, cached framebuffer previews, and keyboard navigation shortcuts (`Ctrl+Tab`, `Ctrl+Shift+Tab`, `Ctrl+W`, `Ctrl+T`, `Ctrl+1..9`).
- **Remote Hardware Diagnostics & Process Manager**: Real-time CPU, RAM, and network telemetry charts, complete process table with PID, footprint, and CPU utilization, plus remote process termination.
- **Canvas Drag-and-Drop & Recursive Directory Transfer**: Direct file and directory drop onto the remote canvas with recursive directory hierarchy preservation, chunked transfer, and streaming SHA-256 verification.
- **In-Session Screen Recording**: Direct recording of decrypted session video to seekable RIFF AVI containers with asynchronous disk I/O, elapsed timecode, and floating recording pill.
- **Bidirectional Voice Intercom**: Real-time VoIP microphone capture and low-latency audio playback via Win32 `waveIn`/`waveOut` with live RMS VU meters and mute controls.
- **Host Audio Streaming**: Real-time system audio capture using Windows WASAPI loopback at 48000 Hz stereo PCM with low-latency playback and instant mute controls.
- **Host Privacy Curtain Mode**: Blanks out the physical host display with a topmost security window and blocks local keyboard/mouse input (`BlockInput`) while remote control proceeds unhindered.
- **TCP Port Forwarding & Tunneling**: Local-to-remote TCP port proxy with built-in presets for RDP (3389), SSH (22), Web (80/443), and VNC (5900).
- **Interactive Remote Terminal**: Embedded Command Prompt and PowerShell console with redirected anonymous pipes, monospaced font, and administrator action triggers.
- **Bidirectional Whiteboard & Screen Annotation**: Collaborative drawing overlay (pen, highlighter, arrow, laser pointer) synchronized in real time between viewer and host screens.
- **Address Book & Machine Aliases**: Save and organize remote desks with custom aliases, category tags (Work, Personal, Servers), notes, and instant search filter.
- **End-to-End Cryptographic Security**: NIST P-256 ephemeral ECDH key exchange, AES-256-GCM AEAD encryption with ratcheted keys, brute-force rate limiting, and short authentication string (SAS) fingerprint verification.
- **Integrated Auto-Updater**: Zero-dependency WinHTTP GitHub release checking with version triad comparison and enforced update modal.
- **Native Windows Integration**: Desktop push notifications, taskbar flashing alerts, and system tray management.

---

## Quick Start

### 1. Download & Run
Download **[`CppDesk.exe`](https://github.com/oocs07/Remote-Desktop/releases/latest)** (no installation required). Double-click to launch.

```powershell
.\CppDesk.exe
```

> **Testing locally?** You can launch multiple isolated instances on one PC:
> ```powershell
> .\CppDesk.exe --instance 1
> .\CppDesk.exe --instance 2
> ```

### 2. Connect
1. **On the Host PC**: Share your **9-Digit ID** (`XXX XXX XXX`) and one-time session code (or set a permanent password in Settings).
2. **On the Viewer PC**: Enter the Host's 9-digit ID, enter the password (or leave blank for interactive approval), and click **Connect**.

---

## How to Build from Source

### Prerequisites
- **Windows 10 / 11 (x64)**
- **MSYS2 MinGW-w64 UCRT64** (`g++` & GNU Assembler `as`) with static `libzstd`

### Build Commands
```powershell
# Compile standalone CppDesk.exe
powershell -ExecutionPolicy Bypass -File .\build.ps1

# Run the automated verification test suite
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Test
```

---

## Project Structure

```text
├── CppDesk.exe                   # Standalone portable Windows executable
├── build.ps1                     # Build and test automation script
├── src/
│   ├── capture/                  # Screen capture, DXGI/GDI & display manager
│   ├── control/                  # Input injection, tab manager, file transfer & whiteboard
│   ├── core/                     # Wire protocol, security & address book
│   ├── media/                    # Voice intercom & AVI session recording
│   ├── net/                      # Networking, audio streaming, TCP tunnels & updater
│   ├── simd/                     # Hardware vector acceleration (AVX2 assembly)
│   └── ui/                       # Direct2D interface & notification manager
├── relay-dotnet/                 # Optional .NET 10 Rendezvous & Relay server
├── scripts/                      # Benchmark and verification scripts
└── tests/                        # Automated integration test suite
```

---

## Acknowledgments & Credits

CppDesk draws architectural and feature inspiration from [**RustDesk**](https://github.com/rustdesk/rustdesk), an open-source remote desktop software. We express our appreciation to the RustDesk project, its maintainers, and community for pioneering open-source remote desktop capabilities.

### Copyright & Licensing Notice
- **Independent Clean-Room Implementation**: CppDesk is an independent software project developed in native C++20 and x86-64 AVX2 SIMD Assembly, using standard Windows operating system APIs (Win32, Direct2D, DirectWrite, DXGI Desktop Duplication, WASAPI Audio, WinHTTP, and Windows CNG Cryptography).
- **No Shared Code or Binaries**: CppDesk does **not** copy, bundle, link against, or distribute any source code, libraries, or binaries from the RustDesk codebase (which is licensed under the GNU AGPL-3.0). Consequently, CppDesk does not trigger AGPL copyleft obligations and is distributed independently under its own permissive [MIT License](LICENSE).
- **Trademark Disclaimer**: "RustDesk" is a trademark of its respective owners. CppDesk is an independent project and is not affiliated with, sponsored by, or endorsed by Pursuit Technology Ltd. or the RustDesk project.

---

## License

Licensed under the [MIT License](LICENSE). Copyright (c) 2026 oocs07.
