#!/usr/bin/env bash
#
# ShareFS Server - macOS build and packaging
#
# Copyright (C) 2025-2026 Andy Timmins
# Licensed under the GNU General Public License version 3 or later.
#
# Builds a single-architecture "slice", bundles the libraries ShareFS.app needs
# so it runs on a machine with no Homebrew, and can fuse two slices into a
# universal release.
#
#   ./build-macos.sh --arch arm64            # build one slice
#   ./build-macos.sh --arch x86_64           # build the other
#   ./build-macos.sh --fuse                  # lipo both into a universal tree
#   ./build-macos.sh --fuse --dmg            # and package it for release
#   ./build-macos.sh --arch arm64 --zip      # slice + zip it
#
# What ships is ShareFS.app and nothing else. It is the whole product: the
# window, the server it runs in-process, and the same command line as every
# other platform at Contents/MacOS/ShareFS. Earlier releases put a separate
# sharefs-server binary and a sample configuration file in the archive and left
# the user to copy the latter into /usr/local/etc as root.
#
# wxWidgets is Homebrew-only and per-architecture, hence the two-slice dance.

set -euo pipefail

ARCH=""
DO_BUILD=true
DO_FUSE=false
DO_ZIP=false
DO_DMG=false

while [ $# -gt 0 ]; do
	case "$1" in
		--arch) ARCH="${2:-}"; shift ;;
		--fuse) DO_FUSE=true ;;
		--zip)  DO_ZIP=true ;;
		--dmg)  DO_DMG=true ;;
		--help|-h)
			echo "Usage: $0 [--arch x86_64|arm64] [--fuse] [--zip] [--dmg]"
			exit 0 ;;
		*) echo "Unknown option: $1" >&2; exit 1 ;;
	esac
	shift
done

# A bare --fuse means "lipo what is already there" rather than build first.
if [ "$DO_FUSE" = true ] && [ -z "$ARCH" ]; then
	DO_BUILD=false
fi
# Packaging on its own means "package what is already there".
if { [ "$DO_ZIP" = true ] || [ "$DO_DMG" = true ]; } && [ -z "$ARCH" ] &&
	[ "$DO_FUSE" = false ]; then
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
#
# Not every reference is an absolute path. Newer Homebrew bottles record their
# dependencies as @rpath/libfoo.dylib and carry an LC_RPATH of
# @loader_path/../lib, which resolves back into Homebrew. Those used to be
# skipped here, so libwebp went into the bundle while the libsharpyuv it needs
# did not, and the reference from libwebpdemux to libwebp was left as @rpath.
# The result launched perfectly on any machine with Homebrew installed - every
# machine that builds it - and aborted on a user's, which is what a
# self-contained bundle is supposed to prevent.
# ---------------------------------------------------------------------------

# Print the libraries a Mach-O file links against that have to be dealt with:
# absolute paths outside the system, and @rpath references. Anything already
# pointing inside the bundle is left alone.
direct_deps() {
	otool -L "$1" | tail -n +2 | awk '{print $1}' | while IFS= read -r dep; do
		case "$dep" in
			/usr/lib/* | /System/*) ;;
			@executable_path/*) ;;
			# @loader_path/<name> with nothing further is a reference this
			# script wrote, and is already correct. One with a path in it, such
			# as @loader_path/../lib/x, is Homebrew's and is not.
			@loader_path/*/*) printf '%s\n' "$dep" ;;
			@loader_path/*) ;;
			*) printf '%s\n' "$dep" ;;
		esac
	done
}

# Resolve a recorded install name to a real file on disk. An @rpath or
# @loader_path name carries no usable path, so it is looked up by basename in
# this architecture's Homebrew, which is where it came from.
resolve_lib() {
	local name="$1"
	if [ -f "$name" ]; then
		printf '%s\n' "$name"
		return 0
	fi

	local base
	base="$(basename "$name")"

	# lib/ first: it is a flat directory of symlinks into the Cellar, so it
	# answers in one stat rather than a walk of every installed formula.
	if [ -e "$BREW_PREFIX/lib/$base" ]; then
		printf '%s\n' "$BREW_PREFIX/lib/$base"
		return 0
	fi

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

# Print the LC_RPATH entries of a Mach-O file that point into Homebrew.
brew_rpaths() {
	otool -l "$1" | awk '/LC_RPATH/ { rpath = 1; next }
	                     rpath && $1 == "path" { print $2; rpath = 0 }' |
		grep -E '^(/opt/homebrew|/usr/local)/' || true
}

# Remove them. Nothing in a finished bundle should need one: every reference
# has been rewritten to @loader_path or @executable_path by now. Leaving them
# in is what made the missing webp libraries invisible for two releases, since
# an @rpath reference nobody had rewritten still resolved through Homebrew on
# every machine that builds this. Without the rpath, such a miss fails on the
# build machine, where somebody will see it.
strip_brew_rpaths() {
	local binary="$1" rpath
	while IFS= read -r rpath; do
		[ -n "$rpath" ] || continue
		install_name_tool -delete_rpath "$rpath" "$binary" 2>/dev/null || true
	done < <(brew_rpaths "$binary")
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
	local exe="$app/Contents/MacOS/ShareFS"
	local frameworks="$app/Contents/Frameworks"

	mkdir -p "$frameworks"
	log "[$ARCH] collecting dependencies for ShareFS.app"
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
		strip_brew_rpaths "$lib"
	done
	strip_brew_rpaths "$exe"

	# Ad-hoc re-sign: editing a Mach-O invalidates any existing signature, and
	# on Apple Silicon an unsigned binary will not run at all.
	log "[$ARCH] re-signing"
	codesign --force --deep --sign - "$app" 2>/dev/null ||
		echo "    warning: codesign failed; the app may not launch" >&2
}

# Every Mach-O file in the bundle, not just the executable, and @rpath counts
# as a leak: it resolves through LC_RPATH entries that point into Homebrew.
# Checking the executable alone is what let the webp libraries ship broken.
verify_bundle() {
	local app="$1"
	local exe="$app/Contents/MacOS/ShareFS"
	local frameworks="$app/Contents/Frameworks"
	local leaked="" file found
	for file in "$exe" "$frameworks"/*.dylib; do
		[ -f "$file" ] || continue
		found="$(otool -L "$file" | tail -n +2 | awk '{print $1}' |
			grep -E '^(/opt/homebrew|/usr/local)/|^@rpath/' || true)"
		# A leftover Homebrew rpath is reported too: it is not a fault on its
		# own, but it is the thing that hides one.
		found="$found$(brew_rpaths "$file")"
		[ -n "$found" ] || continue
		leaked="$leaked
$(basename "$file"):"
		leaked="$leaked
$(printf '  %s\n' $found)"
	done
	if [ -n "$leaked" ]; then
		echo "ERROR: the app still references libraries outside itself:" >&2
		printf '%s\n' "$leaked" >&2
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
	cp "$ROOT/LICENSE" "$STAGE/"
	cp "$ROOT/README.md" "$STAGE/"

	if [ "$build_admin" = ON ]; then
		cp -R "$BUILD_DIR/admin/ShareFS.app" "$STAGE/"
		bundle_dependencies "$STAGE/ShareFS.app"
		verify_bundle "$STAGE/ShareFS.app"
	else
		# No wxWidgets, so there is no app to ship; the bare binary is still
		# useful to somebody running a headless Mac.
		cp "$BUILD_DIR/src/sharefs" "$STAGE/"
		cp "$ROOT/sharefs.conf.sample" "$STAGE/"
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

	if [ -f "$arm_stage/sharefs" ] && [ -f "$x86_stage/sharefs" ]; then
		lipo -create "$x86_stage/sharefs" "$arm_stage/sharefs" \
			-output "$RELEASE/sharefs"
	fi

	if [ -d "$arm_stage/ShareFS.app" ]; then
		local app="$RELEASE/ShareFS.app"
		lipo -create \
			"$x86_stage/ShareFS.app/Contents/MacOS/ShareFS" \
			"$arm_stage/ShareFS.app/Contents/MacOS/ShareFS" \
			-output "$app/Contents/MacOS/ShareFS"

		# Every bundled library has to be fused too, or the app is universal
		# but can only load its libraries on one architecture.
		local lib base x86_lib
		for lib in "$app/Contents/Frameworks"/*.dylib; do
			[ -f "$lib" ] || continue
			base="$(basename "$lib")"
			x86_lib="$x86_stage/ShareFS.app/Contents/Frameworks/$base"
			if [ ! -f "$x86_lib" ]; then
				echo "ERROR: $base is missing from the x86_64 slice." >&2
				echo "The two Homebrews are out of step; lipo cannot fuse" >&2
				echo "libraries that are not the same version." >&2
				exit 1
			fi
			lipo -create "$x86_lib" "$arm_stage/ShareFS.app/Contents/Frameworks/$base" \
				-output "$lib"
		done

		# Fusing rewrote the Mach-O files, so the signature is stale again.
		codesign --force --deep --sign - "$app" 2>/dev/null || true
		verify_bundle "$app"
	fi

	log "verifying universal binaries"
	local ok=true
	for target in "$RELEASE/sharefs" \
	              "$RELEASE/ShareFS.app/Contents/MacOS/ShareFS"; do
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

release_version() {
	local version
	version="$(sed -n 's/^project(sharefs-server VERSION \([0-9.]*\).*/\1/p' \
		"$ROOT/CMakeLists.txt")"
	[ -n "$version" ] || version="unknown"
	echo "$version"
}

make_zip() {
	local version
	version="$(release_version)"

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

# A disk image, because dragging one app to Applications is what a Mac user
# expects and what the README can describe in one line. The zip is kept for
# anyone scripting a download.
make_dmg() {
	local version
	version="$(release_version)"

	local src="$RELEASE"
	[ -d "$src" ] || src="$STAGE"

	if [ ! -d "$src/ShareFS.app" ]; then
		echo "ERROR: no ShareFS.app in $src to package" >&2
		echo "Build a slice with wxWidgets available first." >&2
		exit 1
	fi

	local name="sharefs-server_${version}-macos-universal"
	[ "$src" = "$STAGE" ] && name="sharefs-server_${version}-macos-${ARCH}"

	log "packaging $name.dmg"
	mkdir -p "$ROOT/releases"
	rm -f "$ROOT/releases/$name.dmg"

	local pkg="$ROOT/releases/.dmg/ShareFS"
	rm -rf "$ROOT/releases/.dmg"
	mkdir -p "$pkg"
	ditto "$src/ShareFS.app" "$pkg/ShareFS.app"
	cp "$ROOT/LICENSE" "$pkg/"
	cp "$ROOT/README.md" "$pkg/"
	ln -s /Applications "$pkg/Applications"

	hdiutil create \
		-volname "ShareFS $version" \
		-srcfolder "$pkg" \
		-ov -format UDZO \
		"$ROOT/releases/$name.dmg" > /dev/null

	rm -rf "$ROOT/releases/.dmg"
	log "wrote releases/$name.dmg"
}

[ "$DO_BUILD" = true ] && build_slice
[ "$DO_FUSE" = true ] && fuse_slices
[ "$DO_ZIP" = true ] && make_zip
[ "$DO_DMG" = true ] && make_dmg

log "done"
