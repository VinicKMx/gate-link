#!/usr/bin/env sh
set -eu

BOARD="${BOARD:-native_sim}"

west build -p always -b "$BOARD" apps/transmitter -d build/transmitter
west build -p always -b "$BOARD" apps/receiver -d build/receiver

