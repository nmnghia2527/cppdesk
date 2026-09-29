# AeroDesk — High-Performance Windows Remote Desktop

AeroDesk is a lightweight, hardware-accelerated Remote Desktop application for Windows built in modern **C++20**. Inspired by AnyDesk, it enables fast, low-latency remote screen sharing, input control, clipboard synchronization, chunked file transfer, and live encrypted chat using a **9-Digit Desk ID** (`XXX XXX XXX`) and password or interactive approval.

---

## What It Is For

- **Instant Remote Support & Administration**: Connect to another Windows machine on your LAN, across a rendezvous/relay server, or by direct `IP:Port` using a 9-digit Desk ID.
- **Unattended Remote Access**: Configure a salted SHA-256 unattended password or use the dynamic **6-Character One-Time Session Code** for temporary access.
- **Interactive Session Approval**: When connecting without a password, the host user receives an interactive Accept/Decline modal with granular permission toggles (Mouse & Keyboard Input, Clipboard Sync, File Transfer).
- **Secure File & Clipboard Sharing**: Transfer files up to 2 GB with streaming SHA-256 verification and atomic `.part` writes, plus bidirectional UTF-8 clipboard synchronization and live encrypted chat.

---

## Key Features

### Security & Cryptography (Windows CNG)
- **End-to-End Stream Encryption (`FLAG_ENCRYPTED`)**: All post-handshake video, input, clipboard, file transfer, and chat frames are encrypted using a 256-bit session key derived via Windows `bcrypt.dll` (`SHA-256` challenge-response) and monotonic sequence counters.
- **Zero Plaintext Passwords on Disk**: Unattended passwords are stored exclusively as salted SHA-256 verifier tokens (`unattended_verifier`) in `%APPDATA%\AeroDesk`.
- **Dynamic One-Time Session Code**: Generates a random 6-character alphanumeric session code in memory on startup that can be regenerated with one click (`New Code`).
- **8-Hex SAS Fingerprint**: Displays a Short Authentication String (`XXXX-XXXX`) on both Host and Viewer to verify zero MITM tampering.
- **Brute-Force Protection**: Automatic per-IP rate limiting locks out callers for 60 seconds after 5 failed password attempts within 60 seconds.

### Video Capture, Encoding & Adaptive Frame Rate
- **DXGI Desktop Duplication + GDI Fallback**: Captures 64×64 dirty screen tiles directly from the GPU with automatic fallback to GDI when needed.
- **Hybrid Zstd & JPEG Tile Codec**: Uses lossless **Zstd** compression for crisp UI/text tiles and **GDI+ JPEG** for high-entropy photographic/video regions across 3 presets (`Ultra`, `Balanced`, `Low Bandwidth`).
- **15 / 30 / 60 FPS + Adaptive Network Throttling**: Switch target frame rates live during a session; when `Adaptive FPS` is enabled, AeroDesk automatically steps down (`60 → 30 → 15 FPS`) during high RTT or socket congestion and recovers when latency stabilizes.

### Direct2D Hardware-Accelerated UI
- **Light & Dark Crimson Themes**: Instant runtime switching between **Alabaster & Crimson Red (`#E11D48`)** and **Obsidian & Rose Crimson (`#F43F5E`)** palettes.
- **Custom Direct2D Vector Cursor**: Context-aware custom in-window vector cursor with a spring-physics trailing ring across 4 states (Precision Arrow, Interactive Hover Ring, Text I-Beam, and Remote Canvas Crosshair).
- **Seamless Spring Animations**: Directional horizontal tab carousel (`Home`, `Remote Session`, `Settings`), sliding sidebar indicator pill, staggered card entrance choreography, and smart idle frame pacing (`60 Hz` active / `10 Hz` idle).
- **Power Tools**:
  - **Live Encrypted Chat** (`Files | Chat` side drawer with unread badge and toast notifications)
  - **One-Click Remote Screenshot (`Snap`)** saved to `Downloads\AeroDesk_Received`
  - **Remote Task Manager Shortcut (`Task Mgr`)**
  - **Pinned Favorite Desks (`★`)** in Recent Sessions
  - **Live Latency Sparkline** in the Session HUD

---

## How to Use

### 1. Run the Prebuilt Executable
Launch `AeroDesk.exe` directly:
```powershell
.\AeroDesk.exe
```
To run multiple isolated instances on the same machine for local testing, pass `--instance`:
```powershell
.\AeroDesk.exe --instance 1
.\AeroDesk.exe --instance 2
```

### 2. Connect to a Remote Desk
1. Open **AeroDesk** on both the **Host** (machine to be controlled) and **Viewer** (controlling machine).
2. On the **Host** window (`Home` tab), note the **9-Digit Desk ID** (e.g., `482 910 375`) and either the **One-Time Session Code** or your configured **Unattended Password**.
3. On the **Viewer** window (`Home` tab):
   - Enter the Host's **9-Digit Desk ID** (or `IP:Port`) in the **Remote Desk ID or IP:Port** field (or click a card under **Discovered on LAN** / **Recent Sessions**).
   - Enter the Host's password/session code for instant login, or leave the password blank to trigger an interactive **Accept / Decline** prompt on the Host.
   - Click **Connect to Desk** (or press `Enter`).

### 3. In-Session Controls & Shortcuts
Once connected inside the **Remote Session** tab:
- **Quality Preset**: Cycle between `Ultra (Zstd)`, `Balanced`, and `Low Bandwidth`.
- **FPS Selector**: Click `15 FPS`, `30 FPS`, `60 FPS`, or toggle `Auto FPS`.
- **Monitor Switcher**: Switch between displays if the Host has multiple monitors (`Mon 1/N`).
- **Scale Mode**: Toggle between `Fit Window` (aspect-ratio preserved) and `1:1 Original`.
- **View-Only Mode**: Click `Control ON` / `View Only` (or press `F8`) to disable local mouse/keyboard injection.
- **Screenshot (`Snap`)**: Save a full-resolution `.bmp` snapshot of the remote screen to `Downloads\AeroDesk_Received`.
- **Task Mgr**: Trigger `Ctrl+Shift+Esc` on the remote Host.
- **Files & Chat Drawer**:
  - **Send File...** or drag-and-drop any file onto the AeroDesk window to transfer it to the peer's `Downloads\AeroDesk_Received` folder.
  - Switch to the **Chat** tab in the drawer to exchange encrypted real-time messages.
- **Fullscreen**: Click `Full` or press `F11` to toggle borderless fullscreen mode.

---

## Building from Source

### Prerequisites
- **Windows 10 / 11 (x64)**
- **MSYS2 MinGW-w64 UCRT64 (`g++` with C++20 support)** and `libzstd` (`pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zstd`)

### Build & Run Commands
Use the included `build.ps1` script:

```powershell
# Compile AeroDesk.exe
powershell -ExecutionPolicy Bypass -File .\build.ps1

# Compile AeroDesk.exe + AeroDeskTests.exe and run the automated test suite
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Test

# Compile and immediately launch AeroDesk.exe
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Run
```

---

## Project Structure

```text
├── AeroDesk.exe                         # Prebuilt standalone Windows executable
├── build.ps1                            # PowerShell build & verification script
├── src/
│   ├── main.cpp                         # WinMain entry point & CLI argument parser
│   ├── core/
│   │   ├── protocol.hpp                 # Wire protocol headers, opcodes, FPS & permission types
│   │   ├── crypto_identity.hpp/.cpp     # 9-digit Desk ID, Windows CNG SHA-256, stream cipher, config INI
│   ├── capture/
│   │   ├── screen_capture.hpp/.cpp      # DXGI Desktop Duplication, GDI fallback, Zstd/JPEG tile codec
│   ├── control/
│   │   ├── input_injector.hpp/.cpp      # Win32 SendInput mouse/keyboard injection & modifier release
│   │   ├── clipboard_file_manager.hpp/.cpp # Clipboard sync & chunked file transfer with CNG SHA-256
│   ├── net/
│   │   ├── network_engine.hpp/.cpp      # UDP LAN discovery, TCP Relay, E2EE session & rate limiter
│   └── ui/
│       ├── ui_window.hpp/.cpp           # Direct2D / DirectWrite UI, custom cursor & spring animations
└── tests/
    └── test_suite.cpp                   # Automated integration & cryptographic test suite (99 assertions)
```
