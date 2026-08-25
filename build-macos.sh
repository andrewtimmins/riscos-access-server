#!/usr/bin/env bash
#
# ShareFS Server - macOS build and packaging
#
# Copyright (C) 2025-2026 Andy Timmins
# Licensed under the GNU General Public License version 3 or later.
#
# Builds a single-architecture "slice", bundles the libraries the admin GUI
# needs into the .app so it runs on a machine with no Homebrew, and can fuse
# two slices into a universal release.
#
#   ./build-macos.sh --arch arm64            # build one slice
#   ./build-macos.sh --arch x86_64           # build the other
#   ./build-macos.sh --fuse                  # lipo both into a universal tree
#   ./build-macos.sh --arch arm64 --zip      # slice + zip it
#
# The server binary has no dependencies at all, so it is built universal
# directly when fusing. The admin GUI links wxWidgets, which Homebrew ships
# per-architecture only, hence the two-slice dance.

set -euo pipefail

ARCH=""
DO_BUILD=true
DO_FUSE=false
DO_ZIP=false

while [ $# -gt 0 ]; do
	case "$1" in
		--arch) ARCH="${2:-}"; shift ;;
		--fuse) DO_FUSE=true ;;
		--zip)  DO_ZIP=true ;;
		--help|-h)
			echo "Usage: $0 [--arch x86_64|arm64] [--fuse] [--zip]"
			exit 0 ;;
		*) echo "Unknown option: $1" >&2; exit 1 ;;
	esac
	shift
done

# A bare --fuse means "lipo what is already there" rather than build first.
if [ "$DO_FUSE" = true ] && [ -z "$ARCH" ]; then
	DO_BUILD=false
fi
[ -n "$ARCH" ] || ARCH="$(uname -m)"

case "$ARCH" in
	x86_64|arm64) ;;
	*) echo "Unsupported architecture: $ARCH" >&2; exit 1 ;;
esac

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/build-mac-$ARCH"
STAGE="$BUILD_DIR/appstage"
RELEASE="$ROOT/releases/macos"

# Homebrew lives in a different prefix per architecture, and the slice must
# link the libraries matching its own.
if [ "$ARCH" = "arm64" ]; then
	BREW_PREFIX="/opt/homebrew"
else
	BREW_PREFIX="/usr/local"
fi

log() { printf '==> %s\n' "$*"; }

# ---------------------------------------------------------------------------
# Dependency bundling
#
# Homebrew dylibs record absolute install names such as
# /opt/homebrew/opt/wxwidgets/lib/libwx_baseu-3.3.dylib. Those paths do not
# exist on a machine without Homebrew, so the app has to carry its own copies
# and refer to them relatively. Collect the closure, then rewrite every
# reference to point inside the bundle.
# ---------------------------------------------------------------------------

# Print the non-system libraries a Mach-O file links against.
direct_deps() {
	otool -L "$1" | tail -n +2 | awk '{print $1}' |
		grep -v '^/usr/lib/' | grep -v '^/System/' |
		grep -v '^@' || true
}

# Resolve a recorded install name to a real file on disk.
resolve_lib() {
	local name="$1"
	if [ -f "$name" ]; then
		printf '%s\n' "$name"
		return 0
	fi
	# Fall back to the same basename inside this arch's Homebrew.
	local base
	base="$(basename "$name")"
	local candidate
	candidate="$(find "$BREW_PREFIX/opt" -name "$base" -type f 2>/dev/null | head -1 || true)"
	[ -n "$candidate" ] && printf '%s\n' "$candidate"
}

# Walk the dependency graph, copying each library into the Frameworks dir.
collect_deps() {
	local target="$1" frameworks="$2"
	local pending=("$target")
	local seen=()

	while [ ${#pending[@]} -gt 0 ]; do
		local current="${pending[0]}"
		pending=("${pending[@]:1}")

		local dep
		while IFS= read -r dep; do
			[ -n "$dep" ] || continue
			local base
			base="$(basename "$dep")"

			# Already handled?
			local known=false s
			for s in ${seen[@]+"${seen[@]}"}; do
				[ "$s" = "$base" ] && known=true && break
			done
			[ "$known" = true ] && continue
			seen+=("$base")

			local real
			real="$(resolve_lib "$dep")"
			if [ -z "$real" ]; then
				echo "    warning: cannot locate $dep" >&2
				continue
			fi

			cp -f "$real" "$frameworks/$base"
			chmod u+w "$frameworks/$base"
			# Its own dependencies need collecting too.
			pending+=("$frameworks/$base")
		done < <(direct_deps "$current")
	done
}

# Point every reference at the copy inside the bundle.
rewrite_install_names() {
	local binary="$1" frameworks="$2" prefix="$3"
	local dep base
	while IFS= read -r dep; do
		[ -n "$dep" ] || continue
		base="$(basename "$dep")"
		[ -f "$frameworks/$base" ] || continue
		install_name_tool -change "$dep" "$prefix/$base" "$binary" 2>/dev/null || true
	done < <(direct_deps "$binary")
}

bundle_dependencies() {
	local app="$1"
	local exe="$app/Contents/MacOS/sharefs-admin"
	local frameworks="$app/Contents/Frameworks"

	mkdir -p "$frameworks"
	log "[$ARCH] collecting dependencies for the admin GUI"
	collect_deps "$exe" "$frameworks"

	# The executable looks one level up from Contents/MacOS.
	rewrite_install_names "$exe" "$frameworks" "@executable_path/../Frameworks"

	# Each bundled library refers to its siblings in the same directory, and
	# needs its own id rewritten so the loader does not go hunting for the
	# original absolute path.
	local lib base
	for lib in "$frameworks"/*.dylib; do
		[ -f "$lib" ] || continue
		base="$(basename "$lib")"
		install_name_tool -id "@loader_path/$base" "$lib" 2>/dev/null || true
		rewrite_install_names "$lib" "$frameworks" "@loader_path"
	done

	# Ad-hoc re-sign: editing a Mach-O invalidates any existing signature, and
	# on Apple Silicon an unsigned binary will not run at all.
	log "[$ARCH] re-signing"
	codesign --force --deep --sign - "$app" 2>/dev/null ||
		echo "    warning: codesign failed; the app may not launch" >&2
}

verify_bundle() {
	local app="$1"
	local exe="$app/Contents/MacOS/sharefs-admin"
	local leaked
	leaked="$(otool -L "$exe" | tail -n +2 | awk '{print $1}' |
		grep -E '^(/opt/homebrew|/usr/local)/' || true)"
	if [ -n "$leaked" ]; then
		echo "ERROR: the app still references Homebrew paths:" >&2
		printf '  %s\n' $leaked >&2
		return 1
	fi
	log "[$ARCH] bundle is self-contained"
}

# ---------------------------------------------------------------------------
# Build one slice
# ---------------------------------------------------------------------------
build_slice() {
	log "[$ARCH] configuring (Homebrew prefix $BREW_PREFIX)"

	local wx_config="$BREW_PREFIX/opt/wxwidgets/bin/wx-config"
	local build_admin=ON
	if [ ! -x "$wx_config" ]; then
		echo "    wxWidgets not found at $wx_config - building the server only" >&2
		build_admin=OFF
	fi

	cmake -S "$ROOT" -B "$BUILD_DIR" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_OSX_ARCHITECTURES="$ARCH" \
		-DCMAKE_PREFIX_PATH="$BREW_PREFIX" \
		-DSFS_BUILD_ADMIN="$build_admin"

	log "[$ARCH] building"
	cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"

	log "[$ARCH] staging"
	rm -rf "$STAGE"
	mkdir -p "$STAGE"
	cp "$BUILD_DIR/src/sharefs-server" "$STAGE/"
	cp "$ROOT/sharefs.conf.sample" "$STAGE/"
	cp "$ROOT/README.md" "$ROOT/LICENSE" "$STAGE/"

	if [ "$build_admin" = ON ]; then
		cp -R "$BUILD_DIR/admin/sharefs-admin.app" "$STAGE/"
		bundle_dependencies "$STAGE/sharefs-admin.app"
		verify_bundle "$STAGE/sharefs-admin.app"
	fi

	log "[$ARCH] slice staged in $STAGE"
}

# ---------------------------------------------------------------------------
# Fuse two slices into one universal tree
# ---------------------------------------------------------------------------
fuse_slices() {
	local x86_stage="$ROOT/build-mac-x86_64/appstage"
	local arm_stage="$ROOT/build-mac-arm64/appstage"

	for stage in "$x86_stage" "$arm_stage"; do
		if [ ! -d "$stage" ]; then
			echo "ERROR: missing slice $stage" >&2
			echo "Build both architectures before fusing." >&2
			exit 1
		fi
	done

	log "fusing slices into a universal build"
	rm -rf "$RELEASE"
	mkdir -p "$RELEASE"

	# Start from the arm64 tree, then replace each Mach-O with a fused one.
	cp -R "$arm_stage"/. "$RELEASE/"

	lipo -create "$x86_stage/sharefs-server" "$arm_stage/sharefs-server" \
		-output "$RELEASE/sharefs-server"

	if [ -d "$arm_stage/sharefs-admin.app" ]; then
		local app="$RELEASE/sharefs-admin.app"
		lipo -create \
			"$x86_stage/sharefs-admin.app/Contents/MacOS/sharefs-admin" \
			"$arm_stage/sharefs-admin.app/Contents/MacOS/sharefs-admin" \
			-output "$app/Contents/MacOS/sharefs-admin"

		# Every bundled library has to be fused too, or the app is universal
		# but can only load its libraries on one architecture.
		local lib base x86_lib
		for lib in "$app/Contents/Frameworks"/*.dylib; do
			[ -f "$lib" ] || continue
			base="$(basename "$lib")"
			x86_lib="$x86_stage/sharefs-admin.app/Contents/Frameworks/$base"
			if [ ! -f "$x86_lib" ]; then
				echo "ERROR: $base is missing from the x86_64 slice." >&2
				echo "The two Homebrews are out of step; lipo cannot fuse" >&2
				echo "libraries that are not the same version." >&2
				exit 1
			fi
			lipo -create "$x86_lib" "$arm_stage/sharefs-admin.app/Contents/Frameworks/$base" \
				-output "$lib"
		done

		# Fusing rewrote the Mach-O files, so the signature is stale again.
		codesign --force --deep --sign - "$app" 2>/dev/null || true
		verify_bundle "$app"
	fi

	log "verifying universal binaries"
	local ok=true
	for target in "$RELEASE/sharefs-server" \
	              "$RELEASE/sharefs-admin.app/Contents/MacOS/sharefs-admin"; do
		[ -f "$target" ] || continue
		local archs
		archs="$(lipo -archs "$target")"
		echo "    $(basename "$target"): $archs"
		case "$archs" in
			*x86_64*arm64*|*arm64*x86_64*) ;;
			*) echo "ERROR: $target is not universal" >&2; ok=false ;;
		esac
	done
	[ "$ok" = true ] || exit 1

	log "universal build in $RELEASE"
}

make_zip() {
	local version
	version="$(sed -n 's/^project(sharefs-server VERSION \([0-9.]*\).*/\1/p' \
		"$ROOT/CMakeLists.txt")"
	[ -n "$version" ] || version="unknown"

	local src="$RELEASE"
	[ -d "$src" ] || src="$STAGE"

	local name="sharefs-server_${version}-macos-universal"
	[ "$src" = "$STAGE" ] && name="sharefs-server_${version}-macos-${ARCH}"

	log "packaging $name.zip"
	mkdir -p "$ROOT/releases"
	rm -f "$ROOT/releases/$name.zip"

	# Copy into a directory named after the release so the archive unpacks to
	# something meaningful rather than "appstage".
	local pkg="$ROOT/releases/.pkg/$name"
	rm -rf "$ROOT/releases/.pkg"
	mkdir -p "$pkg"
	ditto "$src" "$pkg"

	# ditto rather than zip: it preserves the bundle's symlinks and extended
	# attributes, which a signed .app needs.
	ditto -c -k --sequesterRsrc --keepParent "$pkg" "$ROOT/releases/$name.zip"
	rm -rf "$ROOT/releases/.pkg"
	log "wrote releases/$name.zip"
}

[ "$DO_BUILD" = true ] && build_slice
[ "$DO_FUSE" = true ] && fuse_slices
[ "$DO_ZIP" = true ] && make_zip

log "done"
