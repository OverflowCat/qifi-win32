# CMakeLists.txt — Qifi Win32
#
# Prerequisites:
#   - zlib (install via vcpkg, or have zlib.lib/zlib1.dll available)
#
# MSVC + vcpkg:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
#   cmake --build build --config Release
#
# MinGW:
#   cmake -G "MinGW Makefiles" -B build
#   cmake --build build
#
cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0074 NEW)
project(qifi-win32 C)

set(CMAKE_C_STANDARD 99)

# --- Vendored QR code generator (Nayuki, MIT) ---
add_library(qrcodegen STATIC lib/qrcodegen.c)
target_include_directories(qrcodegen PUBLIC lib)

# --- Main executable ---
add_executable(qifi-win32 WIN32
    src/main.c
    src/qr_window.c
    resource.rc
)

target_include_directories(qifi-win32 PRIVATE . src lib)
target_link_libraries(qifi-win32 PRIVATE qrcodegen)

# --- zlib ---
set(ZLIB_USE_STATIC_LIBS ON)
find_package(ZLIB REQUIRED)
target_link_libraries(qifi-win32 PRIVATE ZLIB::ZLIB)

# --- Win32 libs ---
target_link_libraries(qifi-win32 PRIVATE user32 gdi32 comdlg32 comctl32)
