/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#ifndef GATE_PROTOCOL_H_
#define GATE_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Wire format, little-endian, fixed size:
 *
 *   offset  size  field
 *   0       1     protocol_version
 *   1       1     message_type
 *   2       4     device_id
 *   6       4     sequence
 *   10      1     command
 *   11      8     auth_tag
 *                 ----
 *                 19 bytes
 *
 * Both COMMAND and ACK packets use this shape. An ACK echoes the command it
 * acknowledges, so validation is uniform for every message type.
 *
 * This module owns bytes and validation only. It must not include radio driver
 * headers or call send/receive APIs (see D004).
 */

/** Wire format version carried in every packet. */
#define GATE_PROTOCOL_VERSION 1u

/** Size of the authentication tag, in bytes. */
#define GATE_PROTOCOL_AUTH_TAG_SIZE 8u

/** Size of an encoded packet, in bytes. */
#define GATE_PROTOCOL_PACKET_SIZE 19u

/*
 * Number of packet bytes covered by the authentication tag: everything that
 * precedes the tag itself.
 *
 * Derived rather than written out, because signing and verification both read
 * this value. A literal that drifted from the real tag offset would shrink the
 * authenticated region on both sides at once, leaving trailing fields forgeable
 * with every test still passing. gate_protocol.c asserts it against the encoder
 * layout.
 */
#define GATE_PROTOCOL_AUTH_DATA_SIZE (GATE_PROTOCOL_PACKET_SIZE - GATE_PROTOCOL_AUTH_TAG_SIZE)

/** Reserved device id meaning "not assigned"; never valid on the wire. */
#define GATE_DEVICE_ID_UNASSIGNED 0u

/** Reserved sequence meaning "no command in progress"; never valid on the wire. */
#define GATE_SEQUENCE_NONE 0u

enum gate_message_type {
	GATE_MESSAGE_TYPE_COMMAND = 1,
	GATE_MESSAGE_TYPE_ACK = 2,
};

enum gate_command {
	GATE_COMMAND_TRIGGER = 1,
};

enum gate_protocol_status {
	GATE_PROTOCOL_OK = 0,
	GATE_PROTOCOL_ERR_ARG,
	GATE_PROTOCOL_ERR_SIZE,
	GATE_PROTOCOL_ERR_VERSION,
	GATE_PROTOCOL_ERR_TYPE,
	GATE_PROTOCOL_ERR_DEVICE_ID,
	GATE_PROTOCOL_ERR_SEQUENCE,
	GATE_PROTOCOL_ERR_COMMAND,
};

/*
 * Decoded packet. `type` and `command` hold raw wire values rather than enum
 * types because a decoded packet may carry a value outside any known
 * enumeration; gate_protocol_validate() is what rejects those.
 */
struct gate_packet {
	uint8_t version;
	uint8_t type;
	uint32_t device_id;
	uint32_t sequence;
	uint8_t command;
	uint8_t auth_tag[GATE_PROTOCOL_AUTH_TAG_SIZE];
};

uint8_t gate_protocol_version(void);

const char *gate_message_type_name(uint8_t type);
const char *gate_command_name(uint8_t command);
const char *gate_protocol_status_name(enum gate_protocol_status status);

/**
 * Fill @p packet as a COMMAND, zeroing the authentication tag.
 */
void gate_protocol_init_command(struct gate_packet *packet, uint32_t device_id, uint32_t sequence,
				uint8_t command);

/**
 * Fill @p packet as an ACK echoing @p command, zeroing the authentication tag.
 */
void gate_protocol_init_ack(struct gate_packet *packet, uint32_t device_id, uint32_t sequence,
			    uint8_t command);

/**
 * Check every field of a decoded or locally built packet.
 *
 * @return GATE_PROTOCOL_OK, or the first field error found.
 */
enum gate_protocol_status gate_protocol_validate(const struct gate_packet *packet);

/**
 * Serialize @p packet into @p buffer.
 *
 * The packet is validated first, so an invalid packet is never transmitted.
 *
 * @param written Set to the number of bytes produced. May be NULL.
 *
 * @return GATE_PROTOCOL_OK, GATE_PROTOCOL_ERR_ARG on NULL arguments,
 *         GATE_PROTOCOL_ERR_SIZE if @p buffer_size is too small, or a field
 *         error.
 */
enum gate_protocol_status gate_protocol_encode(const struct gate_packet *packet, uint8_t *buffer,
					       size_t buffer_size, size_t *written);

/**
 * Parse and validate @p length bytes from @p buffer into @p packet.
 *
 * @p packet is left untouched unless the frame is fully valid, so a caller
 * cannot act on a partially decoded packet.
 *
 * @return GATE_PROTOCOL_OK, GATE_PROTOCOL_ERR_ARG on NULL arguments,
 *         GATE_PROTOCOL_ERR_SIZE if @p length is not the exact packet size, or
 *         a field error.
 */
enum gate_protocol_status gate_protocol_decode(const uint8_t *buffer, size_t length,
					       struct gate_packet *packet);

/**
 * Report whether @p packet is the ACK the transmitter is waiting for (D007).
 *
 * The packet must already be valid. Stale, unknown, or unrelated ACKs return
 * false.
 */
bool gate_protocol_ack_matches(const struct gate_packet *packet, uint32_t expected_device_id,
			       uint32_t expected_sequence, uint8_t expected_command);

#ifdef __cplusplus
}
#endif

#endif /* GATE_PROTOCOL_H_ */
