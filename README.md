# Qifi Win32

Port of [qifi](https://github.com/ntnyq/qrs) (Luby Transform fountain encoding + QR codes) to a native Windows desktop application.

Reads a file, Luby-Transform encodes it into fountain blocks, and displays a continuous stream of QR codes in a Win32 window. Any compatible QR scanner can reconstruct the original file by scanning enough frames.

## Project Structure

```
qifi-win32/
  lib/
    qrcodegen.h          # Nayuki QR Code generator (MIT, vendored)
    qrcodegen.c
  src/
    main.c               # Settings dialog + CLI entry point
    qr_window.h           # QR window interface
    qr_window.c           # LT encoder + QR rendering window
  resource.h              # Dialog resource IDs
  resource.rc             # Win32 dialog template
  CMakeLists.txt          # CMake build (MSVC/MinGW)
  Makefile                # MinGW make build
  build_msvc.bat          # MSVC command-line build script
```

## Dependencies

- **zlib** — compression (install via vcpkg, MSYS2, or download pre-built)
- **Win32 API** — user32, gdi32, comdlg32, comctl32 (included in Windows SDK)

### Installing zlib

**vcpkg (MSVC):**
```
vcpkg install zlib:x64-windows
```

**MSYS2/MinGW:**
```
pacman -S mingw-w64-x86_64-zlib
```

**Manual:** Download zlib source, build, and set `ZLIB_DIR` in `build_msvc.bat`.

## Build

### CMake (recommended)

```bash
# MSVC with vcpkg
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# MinGW
cmake -G "MinGW Makefiles" -B build
cmake --build build
```

### MinGW Make

```bash
make
```

### MSVC batch

Open "x64 Native Tools Command Prompt", edit `ZLIB_DIR` in `build_msvc.bat`, then:
```bash
build_msvc.bat
```

## Usage

### GUI mode (double-click or no arguments)
```
qifi-win32.exe
```
Opens the Settings dialog where you can configure FPS, slice size, error correction level, and select a file.

### CLI mode
```
qifi-win32.exe <file> [options]

Options:
  --fps N       Frames per second (default: 30)
  --slice N     Slice size in bytes (default: 80)
  --ecc L|M|Q|H  Error correction level (default: L)
  --prefix STR  URL prefix prepended to QR data
```

## How It Works

1. **Read** the file and prepend JSON metadata (`filename`, `contentType`)
2. **Compress** with zlib deflate
3. **Luby Transform** encode: split into slices, XOR with random subsets (Ideal Soliton Distribution)
4. **Serialize** each fountain block to binary (header + data)
5. **Base64** encode and (optionally) prepend a URL prefix
6. **QR encode** using Nayuki's generator
7. **Render** in a Win32 window at the configured FPS

The receiver scans enough QR codes to reconstruct the original file via LT decoding.

## License

- `lib/qrcodegen.{h,c}` — MIT, Project Nayuki
- `src/`, `resource.*` — MIT
