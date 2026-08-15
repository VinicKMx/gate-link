/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <string.h>

#include <protocol/gate_protocol.h>

#define GATE_OFFSET_VERSION 0u
#define GATE_OFFSET_TYPE 1u
#define GATE_OFFSET_DEVICE_ID 2u
#define GATE_OFFSET_SEQUENCE 6u
#define GATE_OFFSET_COMMAND 10u
#define GATE_OFFSET_AUTH_TAG 11u

/*
 * The authenticated region must end exactly where the tag begins. Adding a
 * field without moving one of the two would leave those bytes outside the MAC
 * on both the signing and the verifying side, so no test could catch it.
 *
 * Spelled without BUILD_ASSERT to keep this module free of Zephyr headers: it
 * is pure bytes and validation, and stays buildable and fuzzable on its own.
 */
typedef char gate_protocol_auth_region_ends_at_tag
    [(GATE_PROTOCOL_AUTH_DATA_SIZE == GATE_OFFSET_AUTH_TAG) ? 1 : -1];

static void put_le32(uint8_t *buffer, uint32_t value)
{
	buffer[0] = (uint8_t)(value & 0xffu);
	buffer[1] = (uint8_t)((value >> 8) & 0xffu);
	buffer[2] = (uint8_t)((value >> 16) & 0xffu);
	buffer[3] = (uint8_t)((value >> 24) & 0xffu);
}

static uint32_t get_le32(const uint8_t *buffer)
{
	return (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2] << 16) |
	       ((uint32_t)buffer[3] << 24);
}

static bool is_known_type(uint8_t type)
{
	return type == GATE_MESSAGE_TYPE_COMMAND || type == GATE_MESSAGE_TYPE_ACK;
}

static bool is_known_command(uint8_t command) { return command == GATE_COMMAND_TRIGGER; }

uint8_t gate_protocol_version(void) { return GATE_PROTOCOL_VERSION; }

const char *gate_message_type_name(uint8_t type)
{
	switch (type) {
	case GATE_MESSAGE_TYPE_COMMAND:
		return "COMMAND";
	case GATE_MESSAGE_TYPE_ACK:
		return "ACK";
	default:
		return "UNKNOWN";
	}
}

const char *gate_command_name(uint8_t command)
{
	switch (command) {
	case GATE_COMMAND_TRIGGER:
		return "TRIGGER";
	default:
		return "UNKNOWN";
	}
}

const char *gate_protocol_status_name(enum gate_protocol_status status)
{
	switch (status) {
	case GATE_PROTOCOL_OK:
		return "OK";
	case GATE_PROTOCOL_ERR_ARG:
		return "ERR_ARG";
	case GATE_PROTOCOL_ERR_SIZE:
		return "ERR_SIZE";
	case GATE_PROTOCOL_ERR_VERSION:
		return "ERR_VERSION";
	case GATE_PROTOCOL_ERR_TYPE:
		return "ERR_TYPE";
	case GATE_PROTOCOL_ERR_DEVICE_ID:
		return "ERR_DEVICE_ID";
	case GATE_PROTOCOL_ERR_SEQUENCE:
		return "ERR_SEQUENCE";
	case GATE_PROTOCOL_ERR_COMMAND:
		return "ERR_COMMAND";
	default:
		return "UNKNOWN";
	}
}

static void init_packet(struct gate_packet *packet, uint8_t type, uint32_t device_id,
			uint32_t sequence, uint8_t command)
{
	if (packet == NULL) {
		return;
	}

	memset(packet, 0, sizeof(*packet));

	packet->version = GATE_PROTOCOL_VERSION;
	packet->type = type;
	packet->device_id = device_id;
	packet->sequence = sequence;
	packet->command = command;
}

void gate_protocol_init_command(struct gate_packet *packet, uint32_t device_id, uint32_t sequence,
				uint8_t command)
{
	init_packet(packet, GATE_MESSAGE_TYPE_COMMAND, device_id, sequence, command);
}

void gate_protocol_init_ack(struct gate_packet *packet, uint32_t device_id, uint32_t sequence,
			    uint8_t command)
{
	init_packet(packet, GATE_MESSAGE_TYPE_ACK, device_id, sequence, command);
}

enum gate_protocol_status gate_protocol_validate(const struct gate_packet *packet)
{
	if (packet == NULL) {
		return GATE_PROTOCOL_ERR_ARG;
	}

	if (packet->version != GATE_PROTOCOL_VERSION) {
		return GATE_PROTOCOL_ERR_VERSION;
	}

	if (!is_known_type(packet->type)) {
		return GATE_PROTOCOL_ERR_TYPE;
	}

	if (packet->device_id == GATE_DEVICE_ID_UNASSIGNED) {
		return GATE_PROTOCOL_ERR_DEVICE_ID;
	}

	if (packet->sequence == GATE_SEQUENCE_NONE) {
		return GATE_PROTOCOL_ERR_SEQUENCE;
	}

	if (!is_known_command(packet->command)) {
		return GATE_PROTOCOL_ERR_COMMAND;
	}

	return GATE_PROTOCOL_OK;
}

enum gate_protocol_status gate_protocol_encode(const struct gate_packet *packet, uint8_t *buffer,
					       size_t buffer_size, size_t *written)
{
	enum gate_protocol_status status;

	if (packet == NULL || buffer == NULL) {
		return GATE_PROTOCOL_ERR_ARG;
	}

	if (buffer_size < GATE_PROTOCOL_PACKET_SIZE) {
		return GATE_PROTOCOL_ERR_SIZE;
	}

	status = gate_protocol_validate(packet);
	if (status != GATE_PROTOCOL_OK) {
		return status;
	}

	buffer[GATE_OFFSET_VERSION] = packet->version;
	buffer[GATE_OFFSET_TYPE] = packet->type;
	put_le32(&buffer[GATE_OFFSET_DEVICE_ID], packet->device_id);
	put_le32(&buffer[GATE_OFFSET_SEQUENCE], packet->sequence);
	buffer[GATE_OFFSET_COMMAND] = packet->command;
	memcpy(&buffer[GATE_OFFSET_AUTH_TAG], packet->auth_tag, GATE_PROTOCOL_AUTH_TAG_SIZE);

	if (written != NULL) {
		*written = GATE_PROTOCOL_PACKET_SIZE;
	}

	return GATE_PROTOCOL_OK;
}

enum gate_protocol_status gate_protocol_decode(const uint8_t *buffer, size_t length,
					       struct gate_packet *packet)
{
	struct gate_packet decoded;
	enum gate_protocol_status status;

	if (buffer == NULL || packet == NULL) {
		return GATE_PROTOCOL_ERR_ARG;
	}

	if (length != GATE_PROTOCOL_PACKET_SIZE) {
		return GATE_PROTOCOL_ERR_SIZE;
	}

	decoded.version = buffer[GATE_OFFSET_VERSION];
	decoded.type = buffer[GATE_OFFSET_TYPE];
	decoded.device_id = get_le32(&buffer[GATE_OFFSET_DEVICE_ID]);
	decoded.sequence = get_le32(&buffer[GATE_OFFSET_SEQUENCE]);
	decoded.command = buffer[GATE_OFFSET_COMMAND];
	memcpy(decoded.auth_tag, &buffer[GATE_OFFSET_AUTH_TAG], GATE_PROTOCOL_AUTH_TAG_SIZE);

	status = gate_protocol_validate(&decoded);
	if (status != GATE_PROTOCOL_OK) {
		return status;
	}

	*packet = decoded;

	return GATE_PROTOCOL_OK;
}

bool gate_protocol_ack_matches(const struct gate_packet *packet, uint32_t expected_device_id,
			       uint32_t expected_sequence, uint8_t expected_command)
{
	if (packet == NULL) {
		return false;
	}

	if (gate_protocol_validate(packet) != GATE_PROTOCOL_OK) {
		return false;
	}

	if (expected_sequence == GATE_SEQUENCE_NONE) {
		return false;
	}

	return packet->type == GATE_MESSAGE_TYPE_ACK && packet->device_id == expected_device_id &&
	       packet->sequence == expected_sequence && packet->command == expected_command;
}
