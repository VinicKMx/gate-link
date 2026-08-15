/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <string.h>

#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>

#include <auth/gate_auth.h>

#define GATE_AUTH_HMAC_SIZE 32u

const char *gate_auth_status_name(enum gate_auth_status status)
{
	switch (status) {
	case GATE_AUTH_OK:
		return "OK";
	case GATE_AUTH_ERR_ARG:
		return "ERR_ARG";
	case GATE_AUTH_ERR_KEY_LENGTH:
		return "ERR_KEY_LENGTH";
	case GATE_AUTH_ERR_KEY_HEX:
		return "ERR_KEY_HEX";
	case GATE_AUTH_ERR_PACKET:
		return "ERR_PACKET";
	case GATE_AUTH_ERR_CRYPTO:
		return "ERR_CRYPTO";
	case GATE_AUTH_ERR_TAG:
		return "ERR_TAG";
	default:
		return "UNKNOWN";
	}
}

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}

	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}

	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}

	return -1;
}

enum gate_auth_status gate_auth_key_from_hex(const char *hex, uint8_t *key, size_t key_size)
{
	if (hex == NULL || key == NULL) {
		return GATE_AUTH_ERR_ARG;
	}

	if (key_size != GATE_AUTH_KEY_SIZE || strlen(hex) != GATE_AUTH_KEY_HEX_SIZE) {
		return GATE_AUTH_ERR_KEY_LENGTH;
	}

	for (size_t i = 0u; i < GATE_AUTH_KEY_SIZE; i++) {
		int hi = hex_nibble(hex[i * 2u]);
		int lo = hex_nibble(hex[(i * 2u) + 1u]);

		if (hi < 0 || lo < 0) {
			memset(key, 0, key_size);
			return GATE_AUTH_ERR_KEY_HEX;
		}

		key[i] = (uint8_t)(((uint8_t)hi << 4) | (uint8_t)lo);
	}

	return GATE_AUTH_OK;
}

static enum gate_auth_status calculate_tag(const struct gate_packet *packet, const uint8_t *key,
					   size_t key_size, uint8_t *tag, size_t tag_size)
{
	const mbedtls_md_info_t *md_info;
	uint8_t frame[GATE_PROTOCOL_PACKET_SIZE];
	uint8_t hmac[GATE_AUTH_HMAC_SIZE];
	struct gate_packet signed_data;
	enum gate_protocol_status protocol_status;
	int ret;

	if (packet == NULL || key == NULL || tag == NULL || key_size != GATE_AUTH_KEY_SIZE ||
	    tag_size != GATE_PROTOCOL_AUTH_TAG_SIZE) {
		return GATE_AUTH_ERR_ARG;
	}

	signed_data = *packet;
	memset(signed_data.auth_tag, 0, sizeof(signed_data.auth_tag));

	protocol_status = gate_protocol_encode(&signed_data, frame, sizeof(frame), NULL);
	if (protocol_status != GATE_PROTOCOL_OK) {
		return GATE_AUTH_ERR_PACKET;
	}

	md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	if (md_info == NULL) {
		return GATE_AUTH_ERR_CRYPTO;
	}

	ret = mbedtls_md_hmac(md_info, key, key_size, frame, GATE_PROTOCOL_AUTH_DATA_SIZE, hmac);
	if (ret != 0) {
		return GATE_AUTH_ERR_CRYPTO;
	}

	memcpy(tag, hmac, tag_size);
	mbedtls_platform_zeroize(hmac, sizeof(hmac));

	return GATE_AUTH_OK;
}

enum gate_auth_status gate_auth_sign(struct gate_packet *packet, const uint8_t *key,
				     size_t key_size)
{
	if (packet == NULL) {
		return GATE_AUTH_ERR_ARG;
	}

	return calculate_tag(packet, key, key_size, packet->auth_tag, sizeof(packet->auth_tag));
}

static bool constant_time_equal(const uint8_t *left, const uint8_t *right, size_t length)
{
	uint8_t diff = 0u;

	for (size_t i = 0u; i < length; i++) {
		diff |= left[i] ^ right[i];
	}

	return diff == 0u;
}

enum gate_auth_status gate_auth_verify(const struct gate_packet *packet, const uint8_t *key,
				       size_t key_size)
{
	uint8_t expected[GATE_PROTOCOL_AUTH_TAG_SIZE];
	enum gate_auth_status status;
	bool match;

	status = calculate_tag(packet, key, key_size, expected, sizeof(expected));
	if (status != GATE_AUTH_OK) {
		return status;
	}

	match = constant_time_equal(packet->auth_tag, expected, sizeof(expected));
	mbedtls_platform_zeroize(expected, sizeof(expected));

	if (!match) {
		return GATE_AUTH_ERR_TAG;
	}

	return GATE_AUTH_OK;
}
