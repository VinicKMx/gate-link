/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#ifndef GATE_SEQUENCE_FILTER_H_
#define GATE_SEQUENCE_FILTER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <protocol/gate_protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

enum gate_sequence_decision {
	GATE_SEQUENCE_DECISION_EXECUTE = 0,
	GATE_SEQUENCE_DECISION_DUPLICATE,
	GATE_SEQUENCE_DECISION_IGNORE,
	GATE_SEQUENCE_DECISION_INVALID,
};

struct gate_sequence_tracker {
	uint32_t device_id;
	uint32_t last_sequence;
	bool has_last_sequence;
};

void gate_sequence_tracker_init(struct gate_sequence_tracker *tracker, uint32_t device_id);

/**
 * Decide whether a valid command should cross the actuator boundary.
 *
 * State is tracked per accepted transmitter identity. Duplicate detection is
 * equality-only: a sequence is duplicate only when it matches the last sequence
 * already accepted for that same device id.
 */
enum gate_sequence_decision gate_sequence_filter_command(struct gate_sequence_tracker *trackers,
							 size_t tracker_count,
							 const struct gate_packet *command);

#ifdef __cplusplus
}
#endif

#endif /* GATE_SEQUENCE_FILTER_H_ */
