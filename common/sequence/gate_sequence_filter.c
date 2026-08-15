/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <sequence/gate_sequence_filter.h>

void gate_sequence_tracker_init(struct gate_sequence_tracker *tracker, uint32_t device_id)
{
	if (tracker == NULL) {
		return;
	}

	tracker->device_id = device_id;
	tracker->last_sequence = GATE_SEQUENCE_NONE;
	tracker->has_last_sequence = false;
}

static struct gate_sequence_tracker *find_tracker(struct gate_sequence_tracker *trackers,
						  size_t tracker_count, uint32_t device_id)
{
	for (size_t i = 0u; i < tracker_count; i++) {
		if (trackers[i].device_id == device_id) {
			return &trackers[i];
		}
	}

	return NULL;
}

/*
 * Shared entry check for both public functions, so a command can never be
 * recorded under rules looser than the ones that let it through the filter.
 *
 * Returns GATE_SEQUENCE_DECISION_INVALID or GATE_SEQUENCE_DECISION_IGNORE when
 * the command must not be acted on, leaving @p tracker untouched. Any other
 * value means @p tracker points at the state for this transmitter identity.
 */
static enum gate_sequence_decision resolve_tracker(struct gate_sequence_tracker *trackers,
						   size_t tracker_count,
						   const struct gate_packet *command,
						   struct gate_sequence_tracker **tracker)
{
	if (trackers == NULL || tracker_count == 0u || command == NULL) {
		return GATE_SEQUENCE_DECISION_INVALID;
	}

	if (gate_protocol_validate(command) != GATE_PROTOCOL_OK) {
		return GATE_SEQUENCE_DECISION_INVALID;
	}

	if (command->type != GATE_MESSAGE_TYPE_COMMAND) {
		return GATE_SEQUENCE_DECISION_INVALID;
	}

	*tracker = find_tracker(trackers, tracker_count, command->device_id);
	if (*tracker == NULL) {
		return GATE_SEQUENCE_DECISION_IGNORE;
	}

	return GATE_SEQUENCE_DECISION_EXECUTE;
}

enum gate_sequence_decision gate_sequence_filter_command(struct gate_sequence_tracker *trackers,
							 size_t tracker_count,
							 const struct gate_packet *command)
{
	struct gate_sequence_tracker *tracker;
	enum gate_sequence_decision decision;

	decision = resolve_tracker(trackers, tracker_count, command, &tracker);
	if (decision != GATE_SEQUENCE_DECISION_EXECUTE) {
		return decision;
	}

	if (tracker->has_last_sequence && tracker->last_sequence == command->sequence) {
		return GATE_SEQUENCE_DECISION_DUPLICATE;
	}

	return GATE_SEQUENCE_DECISION_EXECUTE;
}

bool gate_sequence_accept_command(struct gate_sequence_tracker *trackers, size_t tracker_count,
				  const struct gate_packet *command)
{
	struct gate_sequence_tracker *tracker;

	if (resolve_tracker(trackers, tracker_count, command, &tracker) !=
	    GATE_SEQUENCE_DECISION_EXECUTE) {
		return false;
	}

	tracker->last_sequence = command->sequence;
	tracker->has_last_sequence = true;

	return true;
}
