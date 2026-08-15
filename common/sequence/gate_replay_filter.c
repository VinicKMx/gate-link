/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <sequence/gate_replay_filter.h>

void gate_replay_tracker_init(struct gate_replay_tracker *tracker, uint32_t device_id)
{
	if (tracker == NULL) {
		return;
	}

	tracker->device_id = device_id;
	tracker->last_sequence = GATE_SEQUENCE_NONE;
	tracker->has_last_sequence = false;
}

void gate_replay_tracker_set_last_sequence(struct gate_replay_tracker *tracker,
					   uint32_t last_sequence)
{
	if (tracker == NULL) {
		return;
	}

	if (last_sequence == GATE_SEQUENCE_NONE) {
		tracker->last_sequence = GATE_SEQUENCE_NONE;
		tracker->has_last_sequence = false;
		return;
	}

	tracker->last_sequence = last_sequence;
	tracker->has_last_sequence = true;
}

static struct gate_replay_tracker *find_tracker(struct gate_replay_tracker *trackers,
						size_t tracker_count, uint32_t device_id)
{
	for (size_t i = 0u; i < tracker_count; i++) {
		if (trackers[i].device_id == device_id) {
			return &trackers[i];
		}
	}

	return NULL;
}

static enum gate_replay_decision resolve_tracker(struct gate_replay_tracker *trackers,
						 size_t tracker_count,
						 const struct gate_packet *command,
						 struct gate_replay_tracker **tracker)
{
	if (trackers == NULL || tracker_count == 0u || command == NULL || tracker == NULL) {
		return GATE_REPLAY_DECISION_INVALID;
	}

	if (gate_protocol_validate(command) != GATE_PROTOCOL_OK) {
		return GATE_REPLAY_DECISION_INVALID;
	}

	if (command->type != GATE_MESSAGE_TYPE_COMMAND) {
		return GATE_REPLAY_DECISION_INVALID;
	}

	*tracker = find_tracker(trackers, tracker_count, command->device_id);
	if (*tracker == NULL) {
		return GATE_REPLAY_DECISION_IGNORE;
	}

	return GATE_REPLAY_DECISION_EXECUTE;
}

enum gate_replay_decision gate_replay_filter_command(struct gate_replay_tracker *trackers,
						     size_t tracker_count,
						     const struct gate_packet *command)
{
	struct gate_replay_tracker *tracker;
	enum gate_replay_decision decision;

	decision = resolve_tracker(trackers, tracker_count, command, &tracker);
	if (decision != GATE_REPLAY_DECISION_EXECUTE) {
		return decision;
	}

	if (!tracker->has_last_sequence) {
		return GATE_REPLAY_DECISION_EXECUTE;
	}

	if (command->sequence == tracker->last_sequence) {
		return GATE_REPLAY_DECISION_DUPLICATE;
	}

	if (command->sequence > tracker->last_sequence) {
		return GATE_REPLAY_DECISION_EXECUTE;
	}

	return GATE_REPLAY_DECISION_REPLAY;
}

bool gate_replay_accept_command(struct gate_replay_tracker *trackers, size_t tracker_count,
				const struct gate_packet *command)
{
	struct gate_replay_tracker *tracker;

	if (gate_replay_filter_command(trackers, tracker_count, command) !=
	    GATE_REPLAY_DECISION_EXECUTE) {
		return false;
	}

	if (resolve_tracker(trackers, tracker_count, command, &tracker) !=
	    GATE_REPLAY_DECISION_EXECUTE) {
		return false;
	}

	tracker->last_sequence = command->sequence;
	tracker->has_last_sequence = true;

	return true;
}
