#!/usr/bin/env bash

set -o pipefail
set -e

URL="$1"
WORKDIR="$2"

cd "${WORKDIR}"

echo "Downloading VGM file from ${URL}..."
wget -qO output.vgm "${URL}"

echo "Trying to render the file..."
vgmplay output.vgm >/dev/null

echo "Compressing the render for uploading..."
ffmpeg -i output.wav -q:a 8 output.ogg -hide_banner -y -v error
