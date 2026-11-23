#!/bin/sh
# Tailscale for plorpOS (plorpos-8ns): one static binary, tailscaled with the
# CLI built in (run as "tailscale" through a symlink), from pinned source in
# the pinned Go image. Only what a handheld on a tailnet uses: kernel TUN,
# MagicDNS, routes from subnet routers, direct UDP. No log upload, no Tailscale
# SSH, Serve, Taildrop or userspace networking.
#   mk/build-tailscale.sh [PORT]   PORT zero40 (default); GOARCH=arm64 (default)
# Output: build/<PORT>/tailscaled and LICENSE-tailscale.md
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
PORT=${1:-zero40}
GOARCH=${GOARCH:-arm64}
VER=1.104.7
SUM=fa1c922b33d0f6db6b073800d7a62fa84081323d247b21547c63a98ed7ecac14
GO_IMAGE=docker.io/library/golang:1.27.4
DL=${DL:-$HOME/projects/misc/coding-team/plorpos-miyoo-mini-flip/dist-inputs/src}
GC=${GOCACHE_DIR:-$HOME/.cache/plorpos-go}   # module + build cache: big, not tmpfs
W=$R/build/$PORT/tailscale-src
F=$DL/tailscale-$VER.tar.gz
KEEP=cli,osrouter,nattraversal,udptransport,dns,linuxdnsfight,iptables,portmapper,ipnbus,health,useroutes,netstack

mkdir -p "$DL" "$GC"
[ -s "$F" ] || curl -sSfL --max-time 300 -o "$F" "https://github.com/tailscale/tailscale/archive/refs/tags/v$VER.tar.gz"
echo "$SUM  $F" | sha256sum -c --quiet -
rm -rf "$W"; mkdir -p "$W"
tar -C "$W" --strip-components=1 -xzf "$F"
# build_dist.sh stamps the version through cmd/mkversion, which needs a git
# checkout; a release tarball has none, so the same ldflags are given here.
docker run --rm -v "$W":/src:Z -v "$GC":/gc:Z -e GOMODCACHE=/gc/mod -e GOCACHE=/gc/build \
	-e CGO_ENABLED=0 -e GOOS=linux -e GOARCH="$GOARCH" -e GOARM="${GOARM:-}" -w /src "$GO_IMAGE" sh -c "
set -e
tags=\$(GOOS= GOARCH= go run ./cmd/featuretags --min --add=$KEEP)
go build -tags=\"\$tags\" -trimpath \
	-ldflags '-w -s -X tailscale.com/version.longStamp=$VER-plorpos -X tailscale.com/version.shortStamp=$VER' \
	-o tailscaled ./cmd/tailscaled"
cp "$W/tailscaled" "$R/build/$PORT/tailscaled"
# Its BSD-3 text, then its own list of the Go packages linked in and theirs.
{ cat "$W/LICENSE"; echo; cat "$W/licenses/tailscale.md"; } > "$R/build/$PORT/LICENSE-tailscale.md"
rm -rf "$W"
ls -l "$R/build/$PORT/tailscaled"
