/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#ifndef GATE_LINK_COMMON_RADIO_GATE_RADIO_H_
#define GATE_LINK_COMMON_RADIO_GATE_RADIO_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

struct gate_radio_rx_result {
	size_t length;
	int16_t rssi;
	int8_t snr;
};

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
