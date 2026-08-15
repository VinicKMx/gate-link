/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#ifndef GATE_COUNTER_STORE_H_
#define GATE_COUNTER_STORE_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/fs/nvs.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GATE_COUNTER_STORE_TX_SEQUENCE_ID 1u
#define GATE_COUNTER_STORE_RX_LAST_SEQUENCE_ID 2u

struct gate_counter_store {
	struct nvs_fs fs;
	bool mounted;
};

int gate_counter_store_init(struct gate_counter_store *store);
int gate_counter_store_read(struct gate_counter_store *store, uint16_t id, uint32_t *value,
			    bool *found);
int gate_counter_store_write(struct gate_counter_store *store, uint16_t id, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* GATE_COUNTER_STORE_H_ */
