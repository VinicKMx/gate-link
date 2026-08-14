/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <string.h>

#include <zephyr/ztest.h>

#include <protocol/gate_protocol.h>

#define TEST_DEVICE_ID 0xa1b2c3d4u
#define TEST_SEQUENCE 0x00010203u

static void fill_valid_frame(uint8_t *frame)
{
	struct gate_packet packet;

	gate_protocol_init_command(&packet, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);

	zassert_equal(gate_protocol_encode(&packet, frame, GATE_PROTOCOL_PACKET_SIZE, NULL),
		      GATE_PROTOCOL_OK, "reference frame must encode");
}

ZTEST_SUITE(gate_protocol, NULL, NULL, NULL, NULL, NULL);

/*
 * The wire layout is a compatibility contract between transmitter and
 * receiver. This test fails if the byte order or field offsets ever move.
 */
ZTEST(gate_protocol, test_wire_layout_is_stable)
{
	/* clang-format off */
	static const uint8_t expected[GATE_PROTOCOL_PACKET_SIZE] = {
		GATE_PROTOCOL_VERSION,			/* offset 0  version */
		GATE_MESSAGE_TYPE_COMMAND,		/* offset 1  type */
		0xd4, 0xc3, 0xb2, 0xa1,			/* offset 2  device_id, LE */
		0x03, 0x02, 0x01, 0x00,			/* offset 6  sequence, LE */
		GATE_COMMAND_TRIGGER,			/* offset 10 command */
		0x00, 0x00, 0x00, 0x00,			/* offset 11 auth_tag */
		0x00, 0x00, 0x00, 0x00,
	};
	/* clang-format on */
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	struct gate_packet packet;
	size_t written = 0;

	gate_protocol_init_command(&packet, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);

	zassert_equal(gate_protocol_encode(&packet, frame, sizeof(frame), &written),
		      GATE_PROTOCOL_OK);
	zassert_equal(written, GATE_PROTOCOL_PACKET_SIZE);
	zassert_mem_equal(frame, expected, sizeof(expected), "wire layout changed");
}

ZTEST(gate_protocol, test_command_roundtrip)
{
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	struct gate_packet sent;
	struct gate_packet received;

	gate_protocol_init_command(&sent, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);

	zassert_equal(gate_protocol_encode(&sent, frame, sizeof(frame), NULL), GATE_PROTOCOL_OK);
	zassert_equal(gate_protocol_decode(frame, sizeof(frame), &received), GATE_PROTOCOL_OK);

	zassert_equal(received.version, GATE_PROTOCOL_VERSION);
	zassert_equal(received.type, GATE_MESSAGE_TYPE_COMMAND);
	zassert_equal(received.device_id, TEST_DEVICE_ID);
	zassert_equal(received.sequence, TEST_SEQUENCE);
	zassert_equal(received.command, GATE_COMMAND_TRIGGER);
}

ZTEST(gate_protocol, test_ack_roundtrip)
{
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	struct gate_packet sent;
	struct gate_packet received;

	gate_protocol_init_ack(&sent, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);

	zassert_equal(gate_protocol_encode(&sent, frame, sizeof(frame), NULL), GATE_PROTOCOL_OK);
	zassert_equal(gate_protocol_decode(frame, sizeof(frame), &received), GATE_PROTOCOL_OK);

	zassert_equal(received.type, GATE_MESSAGE_TYPE_ACK);
	zassert_equal(received.sequence, TEST_SEQUENCE);
}

/* The reserved auth_tag must survive a roundtrip untouched, so phase 8 can
 * start carrying a real tag without changing the packet shape.
 */
ZTEST(gate_protocol, test_auth_tag_roundtrips)
{
	static const uint8_t tag[GATE_PROTOCOL_AUTH_TAG_SIZE] = {1, 2, 3, 4, 5, 6, 7, 8};
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	struct gate_packet sent;
	struct gate_packet received;

	gate_protocol_init_command(&sent, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);
	memcpy(sent.auth_tag, tag, sizeof(tag));

	zassert_equal(gate_protocol_encode(&sent, frame, sizeof(frame), NULL), GATE_PROTOCOL_OK);
	zassert_equal(gate_protocol_decode(frame, sizeof(frame), &received), GATE_PROTOCOL_OK);

	zassert_mem_equal(received.auth_tag, tag, sizeof(tag));
}

ZTEST(gate_protocol, test_decode_rejects_bad_version)
{
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	struct gate_packet packet;

	fill_valid_frame(frame);
	frame[0] = GATE_PROTOCOL_VERSION + 1u;

	zassert_equal(gate_protocol_decode(frame, sizeof(frame), &packet),
		      GATE_PROTOCOL_ERR_VERSION);
}

ZTEST(gate_protocol, test_decode_rejects_unknown_type)
{
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	struct gate_packet packet;

	fill_valid_frame(frame);
	frame[1] = 0x7fu;

	zassert_equal(gate_protocol_decode(frame, sizeof(frame), &packet), GATE_PROTOCOL_ERR_TYPE);
}

ZTEST(gate_protocol, test_decode_rejects_unknown_command)
{
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	struct gate_packet packet;

	fill_valid_frame(frame);
	frame[10] = 0x7fu;

	zassert_equal(gate_protocol_decode(frame, sizeof(frame), &packet),
		      GATE_PROTOCOL_ERR_COMMAND);
}

ZTEST(gate_protocol, test_decode_rejects_reserved_ids)
{
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	struct gate_packet packet;

	fill_valid_frame(frame);
	memset(&frame[2], 0, 4);
	zassert_equal(gate_protocol_decode(frame, sizeof(frame), &packet),
		      GATE_PROTOCOL_ERR_DEVICE_ID);

	fill_valid_frame(frame);
	memset(&frame[6], 0, 4);
	zassert_equal(gate_protocol_decode(frame, sizeof(frame), &packet),
		      GATE_PROTOCOL_ERR_SEQUENCE);
}

ZTEST(gate_protocol, test_decode_rejects_wrong_length)
{
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE + 1u];
	struct gate_packet packet;

	fill_valid_frame(frame);
	frame[GATE_PROTOCOL_PACKET_SIZE] = 0xffu;

	zassert_equal(gate_protocol_decode(frame, GATE_PROTOCOL_PACKET_SIZE - 1u, &packet),
		      GATE_PROTOCOL_ERR_SIZE, "short frame must be rejected");
	zassert_equal(gate_protocol_decode(frame, GATE_PROTOCOL_PACKET_SIZE + 1u, &packet),
		      GATE_PROTOCOL_ERR_SIZE, "long frame must be rejected");
	zassert_equal(gate_protocol_decode(frame, 0, &packet), GATE_PROTOCOL_ERR_SIZE);
}

/* A rejected frame must not leave the caller holding half-parsed fields. */
ZTEST(gate_protocol, test_decode_leaves_output_untouched_on_error)
{
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	struct gate_packet packet;

	gate_protocol_init_ack(&packet, 42u, 7u, GATE_COMMAND_TRIGGER);

	fill_valid_frame(frame);
	frame[0] = GATE_PROTOCOL_VERSION + 1u;

	zassert_equal(gate_protocol_decode(frame, sizeof(frame), &packet),
		      GATE_PROTOCOL_ERR_VERSION);

	zassert_equal(packet.device_id, 42u, "output was modified by a failed decode");
	zassert_equal(packet.sequence, 7u, "output was modified by a failed decode");
	zassert_equal(packet.type, GATE_MESSAGE_TYPE_ACK, "output was modified by a failed decode");
}

ZTEST(gate_protocol, test_encode_rejects_small_buffer)
{
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	struct gate_packet packet;

	gate_protocol_init_command(&packet, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);

	zassert_equal(gate_protocol_encode(&packet, frame, GATE_PROTOCOL_PACKET_SIZE - 1u, NULL),
		      GATE_PROTOCOL_ERR_SIZE);
}

/* An invalid packet must never reach the radio. */
ZTEST(gate_protocol, test_encode_rejects_invalid_packet)
{
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	struct gate_packet packet;

	gate_protocol_init_command(&packet, GATE_DEVICE_ID_UNASSIGNED, TEST_SEQUENCE,
				   GATE_COMMAND_TRIGGER);
	zassert_equal(gate_protocol_encode(&packet, frame, sizeof(frame), NULL),
		      GATE_PROTOCOL_ERR_DEVICE_ID);

	gate_protocol_init_command(&packet, TEST_DEVICE_ID, GATE_SEQUENCE_NONE,
				   GATE_COMMAND_TRIGGER);
	zassert_equal(gate_protocol_encode(&packet, frame, sizeof(frame), NULL),
		      GATE_PROTOCOL_ERR_SEQUENCE);
}

ZTEST(gate_protocol, test_null_arguments_are_rejected)
{
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	struct gate_packet packet;

	gate_protocol_init_command(&packet, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);

	zassert_equal(gate_protocol_encode(NULL, frame, sizeof(frame), NULL),
		      GATE_PROTOCOL_ERR_ARG);
	zassert_equal(gate_protocol_encode(&packet, NULL, sizeof(frame), NULL),
		      GATE_PROTOCOL_ERR_ARG);
	zassert_equal(gate_protocol_decode(NULL, GATE_PROTOCOL_PACKET_SIZE, &packet),
		      GATE_PROTOCOL_ERR_ARG);
	zassert_equal(gate_protocol_decode(frame, GATE_PROTOCOL_PACKET_SIZE, NULL),
		      GATE_PROTOCOL_ERR_ARG);
	zassert_equal(gate_protocol_validate(NULL), GATE_PROTOCOL_ERR_ARG);
	zassert_false(gate_protocol_ack_matches(NULL, TEST_DEVICE_ID, TEST_SEQUENCE));
}

/* D007: success is reported only for an ACK matching the command in progress. */
ZTEST(gate_protocol, test_ack_matching)
{
	struct gate_packet ack;

	gate_protocol_init_ack(&ack, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);
	zassert_true(gate_protocol_ack_matches(&ack, TEST_DEVICE_ID, TEST_SEQUENCE));

	zassert_false(gate_protocol_ack_matches(&ack, TEST_DEVICE_ID, TEST_SEQUENCE + 1u),
		      "stale sequence must not match");
	zassert_false(gate_protocol_ack_matches(&ack, TEST_DEVICE_ID + 1u, TEST_SEQUENCE),
		      "other device must not match");
	zassert_false(gate_protocol_ack_matches(&ack, TEST_DEVICE_ID, GATE_SEQUENCE_NONE),
		      "no command in progress must not match");
}

ZTEST(gate_protocol, test_command_packet_is_not_an_ack)
{
	struct gate_packet command;

	gate_protocol_init_command(&command, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);

	zassert_false(gate_protocol_ack_matches(&command, TEST_DEVICE_ID, TEST_SEQUENCE),
		      "a COMMAND echo must never be accepted as an ACK");
}

ZTEST(gate_protocol, test_next_sequence_skips_reserved_value)
{
	zassert_equal(gate_protocol_next_sequence(1u), 2u);
	zassert_equal(gate_protocol_next_sequence(UINT32_MAX), 1u, "wraparound must skip 0");
	zassert_not_equal(gate_protocol_next_sequence(UINT32_MAX), GATE_SEQUENCE_NONE);
}

ZTEST(gate_protocol, test_names_cover_unknown_values)
{
	zassert_str_equal(gate_message_type_name(GATE_MESSAGE_TYPE_COMMAND), "COMMAND");
	zassert_str_equal(gate_message_type_name(GATE_MESSAGE_TYPE_ACK), "ACK");
	zassert_str_equal(gate_message_type_name(0xffu), "UNKNOWN");
	zassert_str_equal(gate_command_name(GATE_COMMAND_TRIGGER), "TRIGGER");
	zassert_str_equal(gate_command_name(0xffu), "UNKNOWN");
	zassert_str_equal(gate_protocol_status_name(GATE_PROTOCOL_OK), "OK");
	zassert_str_equal(gate_protocol_status_name(GATE_PROTOCOL_ERR_VERSION), "ERR_VERSION");
}
