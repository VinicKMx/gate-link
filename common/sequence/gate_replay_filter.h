/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#ifndef GATE_REPLAY_FILTER_H_
#define GATE_REPLAY_FILTER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <protocol/gate_protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

enum gate_replay_decision {
	GATE_REPLAY_DECISION_EXECUTE = 0,
	GATE_REPLAY_DECISION_DUPLICATE,
	GATE_REPLAY_DECISION_REPLAY,
	GATE_REPLAY_DECISION_IGNORE,
	GATE_REPLAY_DECISION_INVALID,
};

struct gate_replay_tracker {
	uint32_t device_id;
	uint32_t last_sequence;
	bool has_last_sequence;
};

void gate_replay_tracker_init(struct gate_replay_tracker *tracker, uint32_t device_id);

void gate_replay_tracker_set_last_sequence(struct gate_replay_tracker *tracker,
					   uint32_t last_sequence);

enum gate_replay_decision gate_replay_filter_command(struct gate_replay_tracker *trackers,
						     size_t tracker_count,
						     const struct gate_packet *command);

bool gate_replay_accept_command(struct gate_replay_tracker *trackers, size_t tracker_count,
				const struct gate_packet *command);

#ifdef __cplusplus
}
#endif

#endif /* GATE_REPLAY_FILTER_H_ */
