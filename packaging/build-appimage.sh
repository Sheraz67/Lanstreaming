#!/usr/bin/env bash
#
# Build an AppImage for lancast
# Usage: bash packaging/build-appimage.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-appimage"
APPDIR="$BUILD_DIR/AppDir"
TOOLS_DIR="$BUILD_DIR/tools"
VERSION="0.1.0"

echo "=== Lancast AppImage Builder ==="
echo "Project: $PROJECT_DIR"
echo "Build:   $BUILD_DIR"

# --- Step 1: Build in Release mode ---
echo ""
echo "--- Configuring (Release, prefix=/usr) ---"
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

echo ""
echo "--- Building ---"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# --- Step 2: Install into AppDir ---
echo ""
echo "--- Installing into AppDir ---"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr

# --- Step 3: Copy desktop file and icon ---
echo ""
echo "--- Copying desktop file and icon ---"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$SCRIPT_DIR/lancast.desktop" "$APPDIR/usr/share/applications/"
cp "$SCRIPT_DIR/lancast.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/"

# --- Step 4: Download linuxdeploy (if not cached) ---
echo ""
echo "--- Preparing linuxdeploy ---"
mkdir -p "$TOOLS_DIR"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
if [ ! -x "$LINUXDEPLOY" ]; then
    echo "Downloading linuxdeploy..."
    curl -fL -o "$LINUXDEPLOY" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    chmod +x "$LINUXDEPLOY"
fi

# --- Step 5: Run linuxdeploy ---
echo ""
echo "--- Running linuxdeploy ---"

# Add SDL3 build dir so linuxdeploy can find the shared library
SDL3_LIB_DIR="$BUILD_DIR/_deps/sdl3-build"
export LD_LIBRARY_PATH="${SDL3_LIB_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Set version for the output filename
export VERSION

# linuxdeploy needs FUSE or --appimage-extract-and-run
export APPIMAGE_EXTRACT_AND_RUN=1

"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --desktop-file "$SCRIPT_DIR/lancast.desktop" \
    --icon-file "$SCRIPT_DIR/lancast.png" \
    --output appimage

# --- Step 6: Move result to project root ---
APPIMAGE_FILE=$(ls -1 Lancast-*.AppImage 2>/dev/null || true)
if [ -z "$APPIMAGE_FILE" ]; then
    # linuxdeploy may use different casing
    APPIMAGE_FILE=$(ls -1 lancast-*.AppImage 2>/dev/null || true)
fi

if [ -n "$APPIMAGE_FILE" ]; then
    mv "$APPIMAGE_FILE" "$PROJECT_DIR/"
    echo ""
    echo "=== SUCCESS ==="
    echo "AppImage: $PROJECT_DIR/$APPIMAGE_FILE"
    echo "Run with: chmod +x $APPIMAGE_FILE && ./$APPIMAGE_FILE"
else
    echo ""
    echo "=== ERROR: AppImage file not found ==="
    exit 1
fi
