#!/bin/sh
# Syncthing for plorpOS (plorpos-5jh): one static binary from the pinned
# release's source tarball (its Go modules vendored, its web UI pre-built) in
# the pinned Go image. Built with "noupgrade": plorpOS ships the binary, so
# Syncthing neither upgrades itself nor offers to. Pure Go throughout (SQLite
# is modernc's), so no C toolchain. The binary is the same on every port of
# one architecture, so it is built once per GOARCH.
#   mk/build-syncthing.sh          GOARCH=arm64 (default); GOARCH=arm GOARM=7
# Output: build/syncthing-<GOARCH>/syncthing and LICENSE-syncthing.md
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
GOARCH=${GOARCH:-arm64}
VER=2.1.7
SUM=5b84abd0e8b187f618fb2c38c37fa37ce0b377a275d3e0b981bad94176a24afa
STAMP=1792526078   # the release's own build time (its web UI's file times)
GO_IMAGE=docker.io/library/golang:1.27.4
DL=${DL:-$HOME/projects/misc/coding-team/plorpos-miyoo-mini-flip/dist-inputs/src}
GC=${GOCACHE_DIR:-$HOME/.cache/plorpos-go}   # build cache: big, not tmpfs
W=$R/build/syncthing-src
F=$DL/syncthing-source-v$VER.tar.gz
O=$R/build/syncthing-$GOARCH
B=github.com/syncthing/syncthing/lib/build

mkdir -p "$DL" "$GC"
[ -s "$F" ] || curl -sSfL --max-time 300 -o "$F" \
	"https://github.com/syncthing/syncthing/releases/download/v$VER/syncthing-source-v$VER.tar.gz"
echo "$SUM  $F" | sha256sum -c --quiet -
rm -rf "$W"; mkdir -p "$W"
tar -C "$W" --strip-components=1 -xzf "$F"
# build.go would stamp the same things, from a git checkout the tarball lacks.
docker run --rm -v "$W":/src:Z -v "$GC":/gc:Z -e GOCACHE=/gc/build -e GOTOOLCHAIN=local \
	-e GOFLAGS=-mod=vendor -e CGO_ENABLED=0 -e GOOS=linux -e GOARCH="$GOARCH" -e GOARM="${GOARM:-}" \
	-w /src "$GO_IMAGE" go build -tags noupgrade -trimpath \
	-ldflags "-w -s -X $B.Version=v$VER -X $B.Stamp=$STAMP -X $B.User=plorpos -X $B.Host=plorpos -X $B.Tags=noupgrade" \
	-o syncthing ./cmd/syncthing
mkdir -p "$O"
cp "$W/syncthing" "$O/syncthing"
# Its MPL-2.0 text, then each vendored module's own licence.
{
	cat "$W/LICENSE"
	grep '^# ' "$W/vendor/modules.txt" | awk '{print $2}' | while read -r m; do
		for l in "$W/vendor/$m"/LICENSE* "$W/vendor/$m"/COPYING* "$W/vendor/$m"/NOTICE*; do
			[ -f "$l" ] || continue
			printf '\n\n## %s (%s)\n\n' "$m" "$(basename "$l")"
			cat "$l"
		done
	done
} > "$O/LICENSE-syncthing.md"
rm -rf "$W"
ls -l "$O/syncthing"
