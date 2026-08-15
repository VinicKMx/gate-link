/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <string.h>

#include <zephyr/ztest.h>

#include <auth/gate_auth.h>
#include <protocol/gate_protocol.h>

#define TEST_DEVICE_ID 0xa1b2c3d4u
#define TEST_SEQUENCE 0x00010203u

static const char TEST_KEY_HEX[] =
    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
static const uint8_t TEST_KEY[GATE_AUTH_KEY_SIZE] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

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

/* The auth_tag must survive a plain protocol roundtrip untouched. Signing and
 * verification happen in common/auth, not in encode/decode.
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

ZTEST(gate_protocol, test_auth_key_from_hex)
{
	uint8_t key[GATE_AUTH_KEY_SIZE];

	zassert_equal(gate_auth_key_from_hex(TEST_KEY_HEX, key, sizeof(key)), GATE_AUTH_OK);
	zassert_mem_equal(key, TEST_KEY, sizeof(key));

	zassert_equal(gate_auth_key_from_hex("", key, sizeof(key)), GATE_AUTH_ERR_KEY_LENGTH);
	zassert_equal(gate_auth_key_from_hex(TEST_KEY_HEX, key, sizeof(key) - 1u),
		      GATE_AUTH_ERR_KEY_LENGTH);

	zassert_equal(gate_auth_key_from_hex(
			  "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1x", key,
			  sizeof(key)),
		      GATE_AUTH_ERR_KEY_HEX);
}

ZTEST(gate_protocol, test_auth_sign_matches_known_hmac_sha256_vector)
{
	static const uint8_t expected_tag[GATE_PROTOCOL_AUTH_TAG_SIZE] = {
	    0xc1, 0xec, 0xc3, 0xd4, 0x69, 0xdd, 0x33, 0x38,
	};
	struct gate_packet packet;

	gate_protocol_init_command(&packet, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);

	zassert_equal(gate_auth_sign(&packet, TEST_KEY, sizeof(TEST_KEY)), GATE_AUTH_OK);
	zassert_mem_equal(packet.auth_tag, expected_tag, sizeof(expected_tag));
}

ZTEST(gate_protocol, test_auth_ack_uses_message_type_in_tag)
{
	static const uint8_t expected_tag[GATE_PROTOCOL_AUTH_TAG_SIZE] = {
	    0xe3, 0xb0, 0x01, 0x7b, 0xfe, 0x0d, 0x27, 0x2f,
	};
	struct gate_packet packet;

	gate_protocol_init_ack(&packet, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);

	zassert_equal(gate_auth_sign(&packet, TEST_KEY, sizeof(TEST_KEY)), GATE_AUTH_OK);
	zassert_mem_equal(packet.auth_tag, expected_tag, sizeof(expected_tag));
}

ZTEST(gate_protocol, test_auth_verify_accepts_signed_packet)
{
	struct gate_packet packet;

	gate_protocol_init_command(&packet, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);
	zassert_equal(gate_auth_sign(&packet, TEST_KEY, sizeof(TEST_KEY)), GATE_AUTH_OK);

	zassert_equal(gate_auth_verify(&packet, TEST_KEY, sizeof(TEST_KEY)), GATE_AUTH_OK);
}

ZTEST(gate_protocol, test_auth_verify_rejects_tampered_fields_and_tag)
{
	uint8_t wrong_key[GATE_AUTH_KEY_SIZE] = {0};
	struct gate_packet packet;

	gate_protocol_init_command(&packet, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);
	zassert_equal(gate_auth_sign(&packet, TEST_KEY, sizeof(TEST_KEY)), GATE_AUTH_OK);

	packet.sequence++;
	zassert_equal(gate_auth_verify(&packet, TEST_KEY, sizeof(TEST_KEY)), GATE_AUTH_ERR_TAG);

	packet.sequence--;
	packet.auth_tag[0] ^= 0x01u;
	zassert_equal(gate_auth_verify(&packet, TEST_KEY, sizeof(TEST_KEY)), GATE_AUTH_ERR_TAG);

	zassert_equal(gate_auth_verify(&packet, wrong_key, sizeof(wrong_key)), GATE_AUTH_ERR_TAG);
}

ZTEST(gate_protocol, test_auth_rejects_invalid_arguments)
{
	struct gate_packet packet;

	gate_protocol_init_command(&packet, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);

	zassert_equal(gate_auth_key_from_hex(NULL, packet.auth_tag, sizeof(packet.auth_tag)),
		      GATE_AUTH_ERR_ARG);
	zassert_equal(gate_auth_sign(NULL, TEST_KEY, sizeof(TEST_KEY)), GATE_AUTH_ERR_ARG);
	zassert_equal(gate_auth_sign(&packet, NULL, sizeof(TEST_KEY)), GATE_AUTH_ERR_ARG);
	zassert_equal(gate_auth_verify(NULL, TEST_KEY, sizeof(TEST_KEY)), GATE_AUTH_ERR_ARG);
	zassert_equal(gate_auth_verify(&packet, TEST_KEY, sizeof(TEST_KEY) - 1u),
		      GATE_AUTH_ERR_ARG);
	zassert_str_equal(gate_auth_status_name(GATE_AUTH_ERR_TAG), "ERR_TAG");
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
	zassert_false(
	    gate_protocol_ack_matches(NULL, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER));
}

/* D007: success is reported only for an ACK matching the command in progress. */
ZTEST(gate_protocol, test_ack_matching)
{
	struct gate_packet ack;

	gate_protocol_init_ack(&ack, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);
	zassert_true(
	    gate_protocol_ack_matches(&ack, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER));

	zassert_false(gate_protocol_ack_matches(&ack, TEST_DEVICE_ID, TEST_SEQUENCE + 1u,
						GATE_COMMAND_TRIGGER),
		      "stale sequence must not match");
	zassert_false(gate_protocol_ack_matches(&ack, TEST_DEVICE_ID + 1u, TEST_SEQUENCE,
						GATE_COMMAND_TRIGGER),
		      "other device must not match");
	zassert_false(gate_protocol_ack_matches(&ack, TEST_DEVICE_ID, GATE_SEQUENCE_NONE,
						GATE_COMMAND_TRIGGER),
		      "no command in progress must not match");
	zassert_false(gate_protocol_ack_matches(&ack, TEST_DEVICE_ID, TEST_SEQUENCE, 0xffu),
		      "ACK for a different command must not match");
}

ZTEST(gate_protocol, test_command_packet_is_not_an_ack)
{
	struct gate_packet command;

	gate_protocol_init_command(&command, TEST_DEVICE_ID, TEST_SEQUENCE, GATE_COMMAND_TRIGGER);

	zassert_false(gate_protocol_ack_matches(&command, TEST_DEVICE_ID, TEST_SEQUENCE,
						GATE_COMMAND_TRIGGER),
		      "a COMMAND echo must never be accepted as an ACK");
}

/*
 * The authenticated region must cover every field the receiver acts on, and
 * nothing else. This pins the constant the signer and the verifier both use.
 */
ZTEST(gate_protocol, test_authenticated_region_covers_all_fields_before_the_tag)
{
	zassert_equal(GATE_PROTOCOL_AUTH_DATA_SIZE,
		      GATE_PROTOCOL_PACKET_SIZE - GATE_PROTOCOL_AUTH_TAG_SIZE);
	zassert_equal(GATE_PROTOCOL_AUTH_DATA_SIZE, 11u,
		      "version, type, device_id, sequence, and command are 11 bytes");
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
