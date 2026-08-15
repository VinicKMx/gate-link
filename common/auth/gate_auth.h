/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#ifndef GATE_AUTH_H_
#define GATE_AUTH_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <protocol/gate_protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GATE_AUTH_KEY_SIZE 32u
#define GATE_AUTH_KEY_HEX_SIZE (GATE_AUTH_KEY_SIZE * 2u)

enum gate_auth_status {
	GATE_AUTH_OK = 0,
	GATE_AUTH_ERR_ARG,
	GATE_AUTH_ERR_KEY_LENGTH,
	GATE_AUTH_ERR_KEY_HEX,
	GATE_AUTH_ERR_PACKET,
	GATE_AUTH_ERR_CRYPTO,
	GATE_AUTH_ERR_TAG,
};

const char *gate_auth_status_name(enum gate_auth_status status);

enum gate_auth_status gate_auth_key_from_hex(const char *hex, uint8_t *key, size_t key_size);

enum gate_auth_status gate_auth_sign(struct gate_packet *packet, const uint8_t *key,
				     size_t key_size);

enum gate_auth_status gate_auth_verify(const struct gate_packet *packet, const uint8_t *key,
				       size_t key_size);

#ifdef __cplusplus
}
#endif

#endif /* GATE_AUTH_H_ */
