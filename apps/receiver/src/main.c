/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <protocol/gate_protocol.h>
#include <radio/gate_radio.h>

LOG_MODULE_REGISTER(gate_rx, CONFIG_GATE_RX_LOG_LEVEL);

/*
 * Phase 1 binds these aliases to GPIO handles. Asserting them now keeps a
 * missing or misnamed board overlay a build error instead of a bench surprise.
 */
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(gate_actuator)),
	     "board overlay must define the gate-actuator alias");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(gate_status)),
	     "board overlay must define the gate-status alias");

int main(void)
{
	int ret;

	LOG_INF("RX boot");
	LOG_INF("RX protocol version=%u packet=%u bytes command=%s", gate_protocol_version(),
		GATE_PROTOCOL_PACKET_SIZE, gate_command_name(GATE_COMMAND_TRIGGER));

	ret = gate_radio_init();
	if (ret < 0) {
		LOG_WRN("RX radio unavailable: %d", ret);
		goto idle;
	}

	ret = gate_radio_configure_rx();
	if (ret < 0) {
		LOG_ERR("RX radio config failed: %d", ret);
		goto idle;
	}

	for (;;) {
		uint8_t buffer[GATE_PROTOCOL_PACKET_SIZE];
		struct gate_radio_rx_result rx;
		struct gate_packet packet;
		enum gate_protocol_status status;

		ret = gate_radio_receive(buffer, sizeof(buffer),
					 K_MSEC(CONFIG_GATE_RX_RECEIVE_TIMEOUT_MS), &rx);
		if (ret == -EAGAIN) {
			LOG_INF("RX waiting for packets");
			continue;
		}

		if (ret < 0) {
			LOG_ERR("RX receive failed: %d", ret);
			k_sleep(K_SECONDS(1));
			continue;
		}

		status = gate_protocol_decode(buffer, rx.length, &packet);
		if (status != GATE_PROTOCOL_OK) {
			LOG_WRN("RX invalid packet len=%u rssi=%d snr=%d status=%s",
				(unsigned int)rx.length, rx.rssi, rx.snr,
				gate_protocol_status_name(status));
			continue;
		}

		LOG_INF("RX packet type=%s command=%s seq=%u device=%u rssi=%d snr=%d",
			gate_message_type_name(packet.type), gate_command_name(packet.command),
			packet.sequence, packet.device_id, rx.rssi, rx.snr);
	}

idle:
	for (;;) {
		k_sleep(K_SECONDS(30));
	}

	return 0;
}
