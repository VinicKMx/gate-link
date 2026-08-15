/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <zephyr/ztest.h>

#include <protocol/gate_protocol.h>
#include <sequence/gate_replay_filter.h>

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

ZTEST_SUITE(gate_replay_filter, NULL, NULL, NULL, NULL, NULL);

ZTEST(gate_replay_filter, test_replay_filter_accepts_first_command_and_duplicate_only)
{
	struct gate_replay_tracker tracker;
	struct gate_packet packet = command(DEVICE_A, 42u);

	gate_replay_tracker_init(&tracker, DEVICE_A);

	zassert_equal(gate_replay_filter_command(&tracker, 1u, &packet),
		      GATE_REPLAY_DECISION_EXECUTE);
	zassert_true(gate_replay_accept_command(&tracker, 1u, &packet));
	zassert_equal(tracker.last_sequence, 42u);

	zassert_equal(gate_replay_filter_command(&tracker, 1u, &packet),
		      GATE_REPLAY_DECISION_DUPLICATE);
	zassert_false(gate_replay_accept_command(&tracker, 1u, &packet));
}

ZTEST(gate_replay_filter, test_replay_filter_rejects_older_sequences)
{
	struct gate_replay_tracker tracker;
	struct gate_packet first = command(DEVICE_A, 100u);
	struct gate_packet older = command(DEVICE_A, 99u);
	struct gate_packet newer = command(DEVICE_A, 101u);

	gate_replay_tracker_init(&tracker, DEVICE_A);

	zassert_true(gate_replay_accept_command(&tracker, 1u, &first));
	zassert_equal(gate_replay_filter_command(&tracker, 1u, &older),
		      GATE_REPLAY_DECISION_REPLAY);
	zassert_false(gate_replay_accept_command(&tracker, 1u, &older));

	zassert_equal(gate_replay_filter_command(&tracker, 1u, &newer),
		      GATE_REPLAY_DECISION_EXECUTE);
	zassert_true(gate_replay_accept_command(&tracker, 1u, &newer));
	zassert_equal(tracker.last_sequence, 101u);
}

ZTEST(gate_replay_filter, test_replay_filter_uses_loaded_persistent_sequence)
{
	struct gate_replay_tracker tracker;
	struct gate_packet duplicate = command(DEVICE_A, 77u);
	struct gate_packet replay = command(DEVICE_A, 76u);
	struct gate_packet next = command(DEVICE_A, 78u);

	gate_replay_tracker_init(&tracker, DEVICE_A);
	gate_replay_tracker_set_last_sequence(&tracker, 77u);

	zassert_equal(gate_replay_filter_command(&tracker, 1u, &duplicate),
		      GATE_REPLAY_DECISION_DUPLICATE);
	zassert_equal(gate_replay_filter_command(&tracker, 1u, &replay),
		      GATE_REPLAY_DECISION_REPLAY);
	zassert_equal(gate_replay_filter_command(&tracker, 1u, &next),
		      GATE_REPLAY_DECISION_EXECUTE);
}

ZTEST(gate_replay_filter, test_replay_filter_state_is_per_device_identity)
{
	struct gate_replay_tracker trackers[2];
	struct gate_packet from_a = command(DEVICE_A, 9u);
	struct gate_packet from_b = command(DEVICE_B, 9u);

	gate_replay_tracker_init(&trackers[0], DEVICE_A);
	gate_replay_tracker_init(&trackers[1], DEVICE_B);

	zassert_true(gate_replay_accept_command(trackers, 2u, &from_a));
	zassert_equal(gate_replay_filter_command(trackers, 2u, &from_b),
		      GATE_REPLAY_DECISION_EXECUTE);
	zassert_true(gate_replay_accept_command(trackers, 2u, &from_b));
	zassert_equal(gate_replay_filter_command(trackers, 2u, &from_a),
		      GATE_REPLAY_DECISION_DUPLICATE);
	zassert_equal(gate_replay_filter_command(trackers, 2u, &from_b),
		      GATE_REPLAY_DECISION_DUPLICATE);
}

ZTEST(gate_replay_filter, test_replay_filter_rejects_wraparound_without_reprovision)
{
	struct gate_replay_tracker tracker;
	struct gate_packet max = command(DEVICE_A, UINT32_MAX);
	struct gate_packet wrapped = command(DEVICE_A, 1u);

	gate_replay_tracker_init(&tracker, DEVICE_A);

	zassert_true(gate_replay_accept_command(&tracker, 1u, &max));
	zassert_equal(gate_replay_filter_command(&tracker, 1u, &wrapped),
		      GATE_REPLAY_DECISION_REPLAY);
}

ZTEST(gate_replay_filter, test_replay_filter_ignores_unknown_and_invalid_packets)
{
	struct gate_replay_tracker tracker;
	struct gate_packet unknown = command(UNKNOWN_DEVICE, 5u);
	struct gate_packet non_command = ack(DEVICE_A, 5u);
	struct gate_packet invalid = command(DEVICE_A, GATE_SEQUENCE_NONE);

	gate_replay_tracker_init(&tracker, DEVICE_A);

	zassert_equal(gate_replay_filter_command(&tracker, 1u, &unknown),
		      GATE_REPLAY_DECISION_IGNORE);
	zassert_equal(gate_replay_filter_command(&tracker, 1u, &non_command),
		      GATE_REPLAY_DECISION_INVALID);
	zassert_equal(gate_replay_filter_command(&tracker, 1u, &invalid),
		      GATE_REPLAY_DECISION_INVALID);
	zassert_equal(gate_replay_filter_command(NULL, 1u, &unknown), GATE_REPLAY_DECISION_INVALID);
}

/*
 * The receiver decides with the filter, fires the actuator, and only then
 * records the sequence. If asking changed the state, a pulse that failed would
 * still be remembered, and the transmitter's retry would be answered as a
 * duplicate without ever having actuated.
 */
ZTEST(gate_replay_filter, test_replay_filter_does_not_record_until_accepted)
{
	struct gate_replay_tracker tracker;
	struct gate_packet packet = command(DEVICE_A, 42u);

	gate_replay_tracker_init(&tracker, DEVICE_A);

	zassert_equal(gate_replay_filter_command(&tracker, 1u, &packet),
		      GATE_REPLAY_DECISION_EXECUTE);
	zassert_equal(gate_replay_filter_command(&tracker, 1u, &packet),
		      GATE_REPLAY_DECISION_EXECUTE, "asking must not record the sequence");
	zassert_false(tracker.has_last_sequence);

	zassert_true(gate_replay_accept_command(&tracker, 1u, &packet));
	zassert_equal(gate_replay_filter_command(&tracker, 1u, &packet),
		      GATE_REPLAY_DECISION_DUPLICATE);
}
