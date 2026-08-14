#!/usr/bin/env sh
# Copyright (c) 2026 Vinicius Pedrosa
# SPDX-License-Identifier: MIT OR Apache-2.0
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
APP_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"

# The 64-bit variant is the default host target: plain "native_sim" is 32-bit
# and needs multilib host packages. Board overlays live in
# apps/<app>/boards/<board target>.overlay, so the board target spelling here
# has to match the overlay file names.
BOARD="${BOARD:-native_sim/native/64}"
BUILD_ROOT="${BUILD_ROOT:-build}"

if ! west topdir >/dev/null 2>&1; then
	echo "error: run this script from a Zephyr west workspace" >&2
	echo "hint: configure the workspace manifest.path to this repository" >&2
	exit 1
fi

west build -p always -b "$BOARD" -s "$APP_ROOT/apps/transmitter" -d "$BUILD_ROOT/transmitter"
west build -p always -b "$BOARD" -s "$APP_ROOT/apps/receiver" -d "$BUILD_ROOT/receiver"
