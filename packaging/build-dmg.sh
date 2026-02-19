#!/usr/bin/env bash
#
# Build a fully self-contained macOS .app bundle and .dmg for Lancast.
# The resulting DMG needs zero dependencies — users just open it, drag
# Lancast.app to Applications, and run.
#
# Usage: bash packaging/build-dmg.sh
#
# This script will automatically:
#   - Install Homebrew (if missing)
#   - Install FFmpeg, cmake, and pkg-config via Homebrew (if missing)
#   - Install Xcode command-line tools (if missing)
#   - Build the project in Release mode
#   - Bundle ALL dynamic libraries (FFmpeg, SDL3) inside the .app
#   - Create a drag-to-install .dmg
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-dmg"
VERSION="0.1.0"
APP_NAME="Lancast"
BUNDLE_NAME="$APP_NAME.app"
DMG_NAME="$APP_NAME-$VERSION-macOS.dmg"

echo "=== Lancast macOS DMG Builder ==="
echo "Project: $PROJECT_DIR"
echo "Build:   $BUILD_DIR"
echo "Version: $VERSION"
echo ""

# ──────────────────────────────────────────────────────────────
# Step 0: Auto-install all build dependencies
# ──────────────────────────────────────────────────────────────
echo "--- Checking build dependencies ---"

# Xcode command-line tools
if ! xcode-select -p &>/dev/null; then
    echo "    Installing Xcode command-line tools..."
    xcode-select --install
    echo "    Waiting for Xcode tools installation to complete..."
    echo "    (A dialog may have appeared — click Install, then re-run this script)"
    exit 1
fi
echo "    Xcode CLI tools: OK"

# Homebrew
if ! command -v brew &>/dev/null; then
    echo "    Installing Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    # Add Homebrew to PATH for the rest of this script
    if [ -f /opt/homebrew/bin/brew ]; then
        eval "$(/opt/homebrew/bin/brew shellenv)"
    elif [ -f /usr/local/bin/brew ]; then
        eval "$(/usr/local/bin/brew shellenv)"
    fi
fi
echo "    Homebrew: OK"

# cmake
if ! command -v cmake &>/dev/null; then
    echo "    Installing cmake..."
    brew install cmake
fi
echo "    cmake: OK"

# pkg-config
if ! command -v pkg-config &>/dev/null; then
    echo "    Installing pkg-config..."
    brew install pkg-config
fi
echo "    pkg-config: OK"

# FFmpeg
if ! brew list ffmpeg &>/dev/null; then
    echo "    Installing FFmpeg..."
    brew install ffmpeg
fi
echo "    FFmpeg: OK"

echo ""

# ──────────────────────────────────────────────────────────────
# Step 1: Build in Release mode
# ──────────────────────────────────────────────────────────────
echo "--- Configuring (Release) ---"
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release

echo ""
echo "--- Building ---"
cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"

LANCAST_BIN="$BUILD_DIR/lancast"
if [ ! -f "$LANCAST_BIN" ]; then
    echo "ERROR: lancast binary not found at $LANCAST_BIN"
    exit 1
fi
echo ""

# ──────────────────────────────────────────────────────────────
# Step 2: Create .app bundle structure
# ──────────────────────────────────────────────────────────────
echo "--- Creating $BUNDLE_NAME ---"
APPDIR="$BUILD_DIR/$BUNDLE_NAME"
rm -rf "$APPDIR"

mkdir -p "$APPDIR/Contents/MacOS"
mkdir -p "$APPDIR/Contents/Resources"
mkdir -p "$APPDIR/Contents/Frameworks"

# Copy binary
cp "$LANCAST_BIN" "$APPDIR/Contents/MacOS/lancast"

# Copy Info.plist (substitute version)
sed "s/VERSION_PLACEHOLDER/$VERSION/g" "$SCRIPT_DIR/Info.plist" \
    > "$APPDIR/Contents/Info.plist"

# ──────────────────────────────────────────────────────────────
# Step 3: Create .icns icon from PNG
# ──────────────────────────────────────────────────────────────
echo "--- Creating app icon ---"
ICONSET_DIR="$BUILD_DIR/lancast.iconset"
rm -rf "$ICONSET_DIR"
mkdir -p "$ICONSET_DIR"

# Generate all required icon sizes from the 256x256 PNG
sips -z 16 16     "$SCRIPT_DIR/lancast.png" --out "$ICONSET_DIR/icon_16x16.png"      > /dev/null 2>&1
sips -z 32 32     "$SCRIPT_DIR/lancast.png" --out "$ICONSET_DIR/icon_16x16@2x.png"   > /dev/null 2>&1
sips -z 32 32     "$SCRIPT_DIR/lancast.png" --out "$ICONSET_DIR/icon_32x32.png"      > /dev/null 2>&1
sips -z 64 64     "$SCRIPT_DIR/lancast.png" --out "$ICONSET_DIR/icon_32x32@2x.png"   > /dev/null 2>&1
sips -z 128 128   "$SCRIPT_DIR/lancast.png" --out "$ICONSET_DIR/icon_128x128.png"    > /dev/null 2>&1
sips -z 256 256   "$SCRIPT_DIR/lancast.png" --out "$ICONSET_DIR/icon_128x128@2x.png" > /dev/null 2>&1
sips -z 256 256   "$SCRIPT_DIR/lancast.png" --out "$ICONSET_DIR/icon_256x256.png"    > /dev/null 2>&1

iconutil -c icns "$ICONSET_DIR" -o "$APPDIR/Contents/Resources/lancast.icns"
echo "    Icon created"
echo ""

# ──────────────────────────────────────────────────────────────
# Step 4: Bundle ALL dynamic libraries into Frameworks/
# ──────────────────────────────────────────────────────────────
echo "--- Bundling dynamic libraries ---"

FRAMEWORKS_DIR="$APPDIR/Contents/Frameworks"
MACOS_DIR="$APPDIR/Contents/MacOS"

# Collect all non-system dylibs a binary links to
collect_dylibs() {
    local binary="$1"
    otool -L "$binary" 2>/dev/null | awk 'NR>1 {print $1}' | while read -r lib; do
        case "$lib" in
            /System/*|/usr/lib/*|@executable_path/*|@loader_path/*)
                # System libs — shipped with macOS, skip
                continue
                ;;
            @rpath/*)
                # Resolve @rpath — check SDL3 build dir and Homebrew
                local rpath_name="${lib#@rpath/}"
                local resolved=""
                # Try SDL3 build tree
                local sdl_candidate
                sdl_candidate=$(find "$BUILD_DIR/_deps/sdl3-build" -name "$rpath_name" 2>/dev/null | head -1)
                if [ -n "$sdl_candidate" ] && [ -f "$sdl_candidate" ]; then
                    resolved="$sdl_candidate"
                fi
                # Try Homebrew paths
                if [ -z "$resolved" ]; then
                    for prefix in /opt/homebrew/lib /usr/local/lib; do
                        if [ -f "$prefix/$rpath_name" ]; then
                            resolved="$prefix/$rpath_name"
                            break
                        fi
                    done
                fi
                if [ -n "$resolved" ]; then
                    echo "$resolved"
                fi
                ;;
            *)
                # Absolute path (e.g. /opt/homebrew/lib/libavcodec.dylib)
                echo "$lib"
                ;;
        esac
    done
}

BUNDLED_LIBS=()

bundle_lib() {
    local lib_path="$1"
    local lib_name
    lib_name=$(basename "$lib_path")

    # Skip if already bundled
    for bundled in "${BUNDLED_LIBS[@]+"${BUNDLED_LIBS[@]}"}"; do
        if [ "$bundled" = "$lib_name" ]; then
            return
        fi
    done

    if [ ! -f "$lib_path" ]; then
        return
    fi

    # Resolve symlinks to get the actual file
    local real_path
    real_path=$(python3 -c "import os; print(os.path.realpath('$lib_path'))")
    cp "$real_path" "$FRAMEWORKS_DIR/$lib_name"
    chmod 644 "$FRAMEWORKS_DIR/$lib_name"
    install_name_tool -id "@executable_path/../Frameworks/$lib_name" \
        "$FRAMEWORKS_DIR/$lib_name"

    BUNDLED_LIBS+=("$lib_name")
    echo "    Bundled: $lib_name"

    # Recursively bundle this library's own dependencies
    for dep in $(collect_dylibs "$FRAMEWORKS_DIR/$lib_name"); do
        bundle_lib "$dep"
    done
}

# Bundle every non-system dylib the main binary needs
for lib in $(collect_dylibs "$MACOS_DIR/lancast"); do
    bundle_lib "$lib"
done

echo ""

# ──────────────────────────────────────────────────────────────
# Step 5: Rewrite all dylib references to @executable_path
# ──────────────────────────────────────────────────────────────
echo "--- Fixing library references ---"

fix_references() {
    local target="$1"

    otool -L "$target" 2>/dev/null | awk 'NR>1 {print $1}' | while read -r ref; do
        local ref_name
        ref_name=$(basename "$ref")

        # If we have this lib bundled, rewrite the reference
        if [ -f "$FRAMEWORKS_DIR/$ref_name" ]; then
            local new_ref="@executable_path/../Frameworks/$ref_name"
            if [ "$ref" != "$new_ref" ]; then
                install_name_tool -change "$ref" "$new_ref" "$target" 2>/dev/null || true
            fi
        fi
    done
}

# Fix the main binary
fix_references "$MACOS_DIR/lancast"

# Fix all bundled libraries (cross-references between FFmpeg libs, etc.)
for lib_file in "$FRAMEWORKS_DIR"/*.dylib; do
    [ -f "$lib_file" ] || continue
    fix_references "$lib_file"
done

echo "    All references fixed"
echo ""

# ──────────────────────────────────────────────────────────────
# Step 6: Verify — no external paths should remain
# ──────────────────────────────────────────────────────────────
echo "--- Verifying bundle is self-contained ---"

VERIFY_FAILED=0

check_binary() {
    local binary="$1"
    local label="$2"
    local bad_refs
    bad_refs=$(otool -L "$binary" 2>/dev/null | awk 'NR>1 {print $1}' | while read -r ref; do
        case "$ref" in
            /System/*|/usr/lib/*|@executable_path/*|@rpath/*)
                ;;
            *)
                echo "  $ref"
                ;;
        esac
    done)
    if [ -n "$bad_refs" ]; then
        echo "    WARNING: $label has external references:"
        echo "$bad_refs"
        VERIFY_FAILED=1
    fi
}

check_binary "$MACOS_DIR/lancast" "lancast binary"
for lib_file in "$FRAMEWORKS_DIR"/*.dylib; do
    [ -f "$lib_file" ] || continue
    check_binary "$lib_file" "$(basename "$lib_file")"
done

if [ "$VERIFY_FAILED" -eq 0 ]; then
    echo "    Bundle is fully self-contained"
fi
echo ""

# ──────────────────────────────────────────────────────────────
# Step 7: Ad-hoc codesign
# ──────────────────────────────────────────────────────────────
echo "--- Code signing (ad-hoc) ---"

# Sign each framework individually first, then the whole app
for lib_file in "$FRAMEWORKS_DIR"/*.dylib; do
    [ -f "$lib_file" ] || continue
    codesign --force --sign - "$lib_file" 2>/dev/null || true
done

codesign --force --deep --sign - "$APPDIR" 2>/dev/null || {
    echo "    WARNING: codesign failed (non-fatal for local use)"
}
echo "    Signed"
echo ""

# ──────────────────────────────────────────────────────────────
# Step 8: Create DMG with drag-to-Applications layout
# ──────────────────────────────────────────────────────────────
echo "--- Creating DMG ---"
DMG_PATH="$PROJECT_DIR/$DMG_NAME"
rm -f "$DMG_PATH"

# Stage the DMG contents
DMG_STAGING="$BUILD_DIR/dmg-staging"
rm -rf "$DMG_STAGING"
mkdir -p "$DMG_STAGING"
cp -R "$APPDIR" "$DMG_STAGING/"

# Symlink to /Applications for the classic drag-install experience
ln -s /Applications "$DMG_STAGING/Applications"

hdiutil create -volname "$APP_NAME" \
    -srcfolder "$DMG_STAGING" \
    -ov -format UDZO \
    "$DMG_PATH"

echo ""
echo "=== SUCCESS ==="
echo ""
echo "  DMG: $DMG_PATH"
echo ""
echo "  Share this file. To install:"
echo "    1. Double-click $DMG_NAME"
echo "    2. Drag Lancast to Applications"
echo "    3. Open Lancast from Applications"
echo "    4. Grant Screen Recording permission when prompted"
echo ""
echo "  No Homebrew, FFmpeg, or any other install required on the target Mac."
