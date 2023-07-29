#!/usr/bin/env bash

set -o pipefail
set -e

URL="$1"
WORKDIR="$2"

pushd "${WORKDIR}"

wget -O output.vgm "${URL}"
vgmplay output.vgm

popd
