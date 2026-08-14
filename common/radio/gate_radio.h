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
int gate_radio_receive(uint8_t *data, size_t capacity, k_timeout_t timeout,
		       struct gate_radio_rx_result *result);

#endif /* GATE_LINK_COMMON_RADIO_GATE_RADIO_H_ */
