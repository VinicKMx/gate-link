/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#ifndef GATE_LINK_COMMON_RADIO_GATE_RADIO_H_
#define GATE_LINK_COMMON_RADIO_GATE_RADIO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

struct gate_radio_rx_result {
	size_t length;
	int16_t rssi;
	int8_t snr;
};

/**
 * Report whether this build has a LoRa radio wired up at all.
 *
 * False on host builds that have no lora0 alias, where every other call in this
 * header fails permanently. Callers use it to tell "no radio in this build",
 * which retrying cannot fix, from "radio not answering", which it can.
 */
bool gate_radio_is_present(void);

/**
 * Probe the radio and verify the module answers.
 *
 * Safe to call again at any time: it re-reads the chip version rather than
 * mutating state, so it doubles as the probe used to recover from repeated
 * radio errors. A caller that recovers must re-apply its direction config,
 * since a probe does not reprogram the modem.
 */
int gate_radio_init(void);

int gate_radio_configure_tx(void);
int gate_radio_configure_rx(void);
int gate_radio_send(uint8_t *data, size_t length);

/**
 * Receive one frame into @p data.
 *
 * @p capacity must be at least one byte larger than the largest frame the
 * caller is willing to accept. The underlying driver clamps the length it
 * reports to the capacity it was given, so a frame that fills the buffer
 * completely cannot be distinguished from a longer one that was truncated;
 * the spare byte turns that case into an explicit -EMSGSIZE instead of a
 * silently shortened frame that may still look well formed.
 *
 * @return the received length, -EAGAIN on timeout, -EMSGSIZE if the frame
 *         filled @p capacity, or another negative errno.
 */
int gate_radio_receive(uint8_t *data, size_t capacity, k_timeout_t timeout,
		       struct gate_radio_rx_result *result);

#endif /* GATE_LINK_COMMON_RADIO_GATE_RADIO_H_ */
