#!/usr/bin/env bash

URL="$1"
WORKDIR="$2"

cd "${WORKDIR}"

echo "Downloading VGM file from ${URL}..."
wget -qO output.vgm "${URL}"
ret=$?
if [ "$ret" -ne 0 ]; then
	echo "Downloading ${URL} failed!" >&2
	exit 1
fi

echo "Trying to render the file..."
errorMsg="$(echo '' | vgmplay output.vgm 2>&1 >/dev/null)"
# ret value is useless here, check if any errors were printed instead
if [ "$(printf "$errorMsg" | wc -c)" -ne 0 ]; then
	echo "Rendering failed: $errorMsg" >&2
	exit 2
fi

echo "Compressing the render for uploading..."
errorMsg="$(ffmpeg -i output.wav -q:a 8 output.ogg -hide_banner -y -v error 2>&1)"
ret=$?
if [ "$ret" -ne 0 ]; then
	echo "Compressing failed: $errorMsg" >&2
	exit 3
fi
