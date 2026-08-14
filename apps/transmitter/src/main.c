/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <protocol/gate_protocol.h>
#include <radio/gate_radio.h>

LOG_MODULE_REGISTER(gate_tx, CONFIG_GATE_TX_LOG_LEVEL);

/*
 * Phase 1 binds these aliases to GPIO handles. Asserting them now keeps a
 * missing or misnamed board overlay a build error instead of a bench surprise.
 */
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(gate_button)),
	     "board overlay must define the gate-button alias");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(gate_status_ok)),
	     "board overlay must define the gate-status-ok alias");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(gate_status_error)),
	     "board overlay must define the gate-status-error alias");

int main(void)
{
	int ret;
	uint32_t sequence = GATE_SEQUENCE_NONE;

	LOG_INF("TX boot");
	LOG_INF("TX protocol version=%u packet=%u bytes command=%s", gate_protocol_version(),
		GATE_PROTOCOL_PACKET_SIZE, gate_command_name(GATE_COMMAND_TRIGGER));

	ret = gate_radio_init();
	if (ret < 0) {
		LOG_WRN("TX radio unavailable: %d", ret);
		goto idle;
	}

	ret = gate_radio_configure_tx();
	if (ret < 0) {
		LOG_ERR("TX radio config failed: %d", ret);
		goto idle;
	}

	for (;;) {
		struct gate_packet packet;
		uint8_t buffer[GATE_PROTOCOL_PACKET_SIZE];
		size_t written;
		enum gate_protocol_status status;

		sequence = gate_protocol_next_sequence(sequence);
		gate_protocol_init_command(&packet, CONFIG_GATE_TX_DEVICE_ID, sequence,
					   GATE_COMMAND_TRIGGER);

		status = gate_protocol_encode(&packet, buffer, sizeof(buffer), &written);
		if (status != GATE_PROTOCOL_OK) {
			LOG_ERR("TX encode failed seq=%u status=%s", sequence,
				gate_protocol_status_name(status));
			k_sleep(K_MSEC(CONFIG_GATE_TX_SEND_INTERVAL_MS));
			continue;
		}

		LOG_INF("TX sending %s seq=%u device=%u bytes=%u",
			gate_command_name(packet.command), packet.sequence, packet.device_id,
			(unsigned int)written);

		ret = gate_radio_send(buffer, written);
		if (ret < 0) {
			LOG_ERR("TX send failed seq=%u ret=%d", sequence, ret);
		} else {
			LOG_INF("TX sent seq=%u", sequence);
		}

		k_sleep(K_MSEC(CONFIG_GATE_TX_SEND_INTERVAL_MS));
	}

idle:
	for (;;) {
		k_sleep(K_SECONDS(30));
	}

	return 0;
}
