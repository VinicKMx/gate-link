/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <zephyr/ztest.h>

#include <protocol/gate_protocol.h>
#include <sequence/gate_sequence_filter.h>

#define DEVICE_A 11u
#define DEVICE_B 22u
#define UNKNOWN_DEVICE 99u

static struct gate_packet command(uint32_t device_id, uint32_t sequence)
{
	struct gate_packet packet;

	gate_protocol_init_command(&packet, device_id, sequence, GATE_COMMAND_TRIGGER);

	return packet;
}

static struct gate_packet ack(uint32_t device_id, uint32_t sequence)
{
	struct gate_packet packet;

	gate_protocol_init_ack(&packet, device_id, sequence, GATE_COMMAND_TRIGGER);

	return packet;
}

ZTEST_SUITE(gate_sequence_filter, NULL, NULL, NULL, NULL, NULL);

ZTEST(gate_sequence_filter, test_first_command_executes_and_same_sequence_is_duplicate)
{
	struct gate_sequence_tracker tracker;
	struct gate_packet packet = command(DEVICE_A, 42u);

	gate_sequence_tracker_init(&tracker, DEVICE_A);

	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &packet),
		      GATE_SEQUENCE_DECISION_EXECUTE);
	zassert_false(tracker.has_last_sequence);

	zassert_true(gate_sequence_accept_command(&tracker, 1u, &packet));
	zassert_true(tracker.has_last_sequence);
	zassert_equal(tracker.last_sequence, 42u);

	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &packet),
		      GATE_SEQUENCE_DECISION_DUPLICATE);
	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &packet),
		      GATE_SEQUENCE_DECISION_DUPLICATE);
}

ZTEST(gate_sequence_filter, test_rebooted_transmitter_sequence_is_not_treated_as_old)
{
	struct gate_sequence_tracker tracker;
	struct gate_packet high = command(DEVICE_A, 100u);
	struct gate_packet rebooted = command(DEVICE_A, 1u);

	gate_sequence_tracker_init(&tracker, DEVICE_A);

	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &high),
		      GATE_SEQUENCE_DECISION_EXECUTE);
	zassert_true(gate_sequence_accept_command(&tracker, 1u, &high));
	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &rebooted),
		      GATE_SEQUENCE_DECISION_EXECUTE,
		      "only equality with the last sequence is duplicate");
	zassert_true(gate_sequence_accept_command(&tracker, 1u, &rebooted));
	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &rebooted),
		      GATE_SEQUENCE_DECISION_DUPLICATE);
}

ZTEST(gate_sequence_filter, test_wraparound_uses_equality_only)
{
	struct gate_sequence_tracker tracker;
	struct gate_packet max = command(DEVICE_A, UINT32_MAX);
	struct gate_packet wrapped = command(DEVICE_A, 1u);

	gate_sequence_tracker_init(&tracker, DEVICE_A);

	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &max),
		      GATE_SEQUENCE_DECISION_EXECUTE);
	zassert_true(gate_sequence_accept_command(&tracker, 1u, &max));
	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &wrapped),
		      GATE_SEQUENCE_DECISION_EXECUTE);
	zassert_true(gate_sequence_accept_command(&tracker, 1u, &wrapped));
	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &wrapped),
		      GATE_SEQUENCE_DECISION_DUPLICATE);
}

ZTEST(gate_sequence_filter, test_state_is_per_device_identity)
{
	struct gate_sequence_tracker trackers[2];
	struct gate_packet from_a = command(DEVICE_A, 7u);
	struct gate_packet from_b = command(DEVICE_B, 7u);

	gate_sequence_tracker_init(&trackers[0], DEVICE_A);
	gate_sequence_tracker_init(&trackers[1], DEVICE_B);

	zassert_equal(gate_sequence_filter_command(trackers, 2u, &from_a),
		      GATE_SEQUENCE_DECISION_EXECUTE);
	zassert_true(gate_sequence_accept_command(trackers, 2u, &from_a));
	zassert_equal(gate_sequence_filter_command(trackers, 2u, &from_b),
		      GATE_SEQUENCE_DECISION_EXECUTE,
		      "same sequence from another accepted identity is independent");
	zassert_true(gate_sequence_accept_command(trackers, 2u, &from_b));
	zassert_equal(gate_sequence_filter_command(trackers, 2u, &from_a),
		      GATE_SEQUENCE_DECISION_DUPLICATE);
	zassert_equal(gate_sequence_filter_command(trackers, 2u, &from_b),
		      GATE_SEQUENCE_DECISION_DUPLICATE);
}

ZTEST(gate_sequence_filter, test_unknown_device_is_ignored_without_touching_state)
{
	struct gate_sequence_tracker tracker;
	struct gate_packet unknown = command(UNKNOWN_DEVICE, 5u);
	struct gate_packet accepted = command(DEVICE_A, 5u);

	gate_sequence_tracker_init(&tracker, DEVICE_A);

	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &unknown),
		      GATE_SEQUENCE_DECISION_IGNORE);
	zassert_false(tracker.has_last_sequence);
	zassert_false(gate_sequence_accept_command(&tracker, 1u, &unknown));
	zassert_false(tracker.has_last_sequence);
	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &accepted),
		      GATE_SEQUENCE_DECISION_EXECUTE);
}

ZTEST(gate_sequence_filter, test_receiver_reset_forgets_last_sequence)
{
	struct gate_sequence_tracker tracker;
	struct gate_packet packet = command(DEVICE_A, 12u);

	gate_sequence_tracker_init(&tracker, DEVICE_A);

	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &packet),
		      GATE_SEQUENCE_DECISION_EXECUTE);
	zassert_true(gate_sequence_accept_command(&tracker, 1u, &packet));
	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &packet),
		      GATE_SEQUENCE_DECISION_DUPLICATE);

	gate_sequence_tracker_init(&tracker, DEVICE_A);

	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &packet),
		      GATE_SEQUENCE_DECISION_EXECUTE,
		      "deduplication state is intentionally RAM-only for now");
}

ZTEST(gate_sequence_filter, test_invalid_inputs_do_not_change_state)
{
	struct gate_sequence_tracker tracker;
	struct gate_packet packet = command(DEVICE_A, 8u);
	struct gate_packet non_command = ack(DEVICE_A, 8u);

	gate_sequence_tracker_init(&tracker, DEVICE_A);

	zassert_equal(gate_sequence_filter_command(NULL, 1u, &packet),
		      GATE_SEQUENCE_DECISION_INVALID);
	zassert_equal(gate_sequence_filter_command(&tracker, 0u, &packet),
		      GATE_SEQUENCE_DECISION_INVALID);
	zassert_equal(gate_sequence_filter_command(&tracker, 1u, NULL),
		      GATE_SEQUENCE_DECISION_INVALID);
	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &non_command),
		      GATE_SEQUENCE_DECISION_INVALID);
	zassert_false(gate_sequence_accept_command(&tracker, 1u, &non_command));

	packet.sequence = GATE_SEQUENCE_NONE;
	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &packet),
		      GATE_SEQUENCE_DECISION_INVALID);
	zassert_false(gate_sequence_accept_command(&tracker, 1u, &packet));
	zassert_false(tracker.has_last_sequence);
}

ZTEST(gate_sequence_filter, test_filter_does_not_mark_sequence_until_accepted)
{
	struct gate_sequence_tracker tracker;
	struct gate_packet packet = command(DEVICE_A, 9u);

	gate_sequence_tracker_init(&tracker, DEVICE_A);

	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &packet),
		      GATE_SEQUENCE_DECISION_EXECUTE);
	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &packet),
		      GATE_SEQUENCE_DECISION_EXECUTE,
		      "a failed actuator attempt must be retriable");

	zassert_true(gate_sequence_accept_command(&tracker, 1u, &packet));
	zassert_equal(gate_sequence_filter_command(&tracker, 1u, &packet),
		      GATE_SEQUENCE_DECISION_DUPLICATE);
}
