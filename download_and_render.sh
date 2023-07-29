#!/usr/bin/env bash

set -o pipefail
set -e

URL="$1"
WORKDIR="$2"

pushd "${WORKDIR}"

wget -O output.vgm "${URL}"
vgmplay output.vgm
ffmpeg -i output.wav -q:a 8 output.ogg -hide_banner -y

popd
