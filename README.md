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

CppDesk is a zero-install, hardware-accelerated Remote Desktop application for Windows distributed as a single portable executable (**`CppDesk.exe`**). Works out of the box with no external runtime dependencies. You have full control of your connection and data, backed by end-to-end authenticated encryption.

Connect seamlessly across networks using a **9-Digit Desk ID** (`XXX XXX XXX`) with permanent password or interactive approval.

---

## Features

- 🖥️ **High-Performance Screen Sharing**: Smooth 60 FPS remote desktop streaming with multi-monitor switching, borderless fullscreen, and live quality adjustments (High, Balanced, Fast).
- 🔊 **Real-Time Remote Audio**: Stream crystal-clear stereo audio from the host PC with low-latency playback and instant mute/volume controls.
- 🛡️ **Host Privacy Curtain Mode**: Blank out the physical host display and block local keyboard/mouse input while you work securely from afar.
- 🔌 **TCP Port Forwarding & Tunneling**: Tunnel local ports to remote services with one-click presets for RDP (3389), SSH (22), Web (80/443), and VNC (5900).
- 💻 **Interactive Remote Terminal**: Built-in Command Prompt and PowerShell console accessible directly from the side drawer with administrator quick actions.
- 🎨 **Screen Annotation & Whiteboard**: Collaborative drawing tools (Pen, Highlighter, Arrow, Laser pointer) rendered live on both viewer and host screens.
- 🔄 **Integrated Auto-Updater**: Background GitHub Release verification ensuring security fixes, protocol compatibility, and latest features.
- 📇 **Address Book & Categories**: Save and organize remote desks with custom aliases, category tags (Work, Personal, Servers), and instant search.
- 📁 **Chunked File Transfer & Chat**: High-speed drag-and-drop file transfers with integrity verification, plus integrated real-time text chat.
- 🔔 **Windows Integration**: Native push notifications, taskbar flashing alerts, and responsive system tray support.
- 🔒 **End-to-End Security**: Protected with modern cryptographic handshakes (ECDH P-256) and authenticated AES-256-GCM stream encryption.

---

## Quick Start

### 1. Download & Run
Download **[`CppDesk.exe`](https://github.com/oocs07/Remote-Desktop/releases/latest)** — no installation required. Just double-click to launch.

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
│   ├── capture/                  # Screen capture & tile encoding
│   ├── control/                  # Input injection, file transfer & whiteboard
│   ├── core/                     # Wire protocol, security & address book
│   ├── net/                      # Networking, audio streaming, TCP tunnels & updater
│   ├── simd/                     # Hardware vector acceleration
│   └── ui/                       # Direct2D interface & notification manager
├── relay-dotnet/                 # Optional .NET 10 Rendezvous & Relay server
├── scripts/                      # Benchmark and verification scripts
└── tests/                        # Automated integration test suite
```

---

## Acknowledgments & Credits

CppDesk proudly draws architectural and feature inspiration from [**RustDesk**](https://github.com/rustdesk/rustdesk), an outstanding open-source remote desktop software. We express our sincere appreciation to the RustDesk project, its maintainers, and community for pioneering open-source remote desktop capabilities.

### Copyright & Licensing Notice
- **Independent Clean-Room Implementation**: CppDesk is an independent software project developed in native C++20 and x86-64 AVX2 SIMD Assembly, using standard Windows operating system APIs (Win32, Direct2D, DirectWrite, DXGI Desktop Duplication, WASAPI Audio, WinHTTP, and Windows CNG Cryptography).
- **No Shared Code or Binaries**: CppDesk does **not** copy, bundle, link against, or distribute any source code, libraries, or binaries from the RustDesk codebase (which is licensed under the GNU AGPL-3.0). Consequently, CppDesk does not trigger AGPL copyleft obligations and is distributed independently under its own permissive [MIT License](LICENSE).
- **Trademark Disclaimer**: "RustDesk" is a trademark of its respective owners. CppDesk is an independent project and is not affiliated with, sponsored by, or endorsed by Pursuit Technology Ltd. or the RustDesk project.

---

## License

Licensed under the [MIT License](LICENSE). Copyright (c) 2026 oocs07.
