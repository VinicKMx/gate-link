#!/usr/bin/env sh
# Copyright (c) 2026 Vinicius Pedrosa
# SPDX-License-Identifier: MIT OR Apache-2.0
set -eu

# The 64-bit variant is the default host target: plain "native_sim" is 32-bit
# and needs multilib host packages. Board overlays live in
# apps/<app>/boards/<board target>.overlay, so the board target spelling here
# has to match the overlay file names.
BOARD="${BOARD:-native_sim/native/64}"

west build -p always -b "$BOARD" apps/transmitter -d build/transmitter
west build -p always -b "$BOARD" apps/receiver -d build/receiver
