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

static int send_command(const struct gate_packet *packet)
{
	uint8_t buffer[GATE_PROTOCOL_PACKET_SIZE];
	size_t written;
	enum gate_protocol_status status;
	int ret;

	status = gate_protocol_encode(packet, buffer, sizeof(buffer), &written);
	if (status != GATE_PROTOCOL_OK) {
		LOG_ERR("TX encode failed seq=%u status=%s", packet->sequence,
			gate_protocol_status_name(status));
		return -EINVAL;
	}

	ret = gate_radio_configure_tx();
	if (ret < 0) {
		LOG_ERR("TX radio config failed: %d", ret);
		return ret;
	}

	LOG_INF("TX sending %s seq=%u device=%u bytes=%u",
		gate_command_name(packet->command), packet->sequence, packet->device_id,
		(unsigned int)written);

	ret = gate_radio_send(buffer, written);
	if (ret < 0) {
		LOG_ERR("TX send failed seq=%u ret=%d", packet->sequence, ret);
		return ret;
	}

	LOG_INF("TX sent seq=%u", packet->sequence);

	return 0;
}

static int wait_for_ack(uint32_t sequence)
{
	int64_t deadline = k_uptime_get() + CONFIG_GATE_TX_ACK_TIMEOUT_MS;
	int ret;

	ret = gate_radio_configure_rx();
	if (ret < 0) {
		LOG_ERR("TX radio RX config failed: %d", ret);
		return ret;
	}

	LOG_INF("TX waiting for ACK seq=%u timeout=%u ms", sequence,
		CONFIG_GATE_TX_ACK_TIMEOUT_MS);

	for (;;) {
		/* One byte above the packet size, so an oversized frame is
		 * reported instead of silently truncated (see gate_radio.h).
		 */
		uint8_t buffer[GATE_PROTOCOL_PACKET_SIZE + 1u];
		struct gate_radio_rx_result rx;
		struct gate_packet packet;
		enum gate_protocol_status status;
		int64_t remaining = deadline - k_uptime_get();

		if (remaining <= 0) {
			LOG_WRN("TX ACK timeout seq=%u", sequence);
			return -EAGAIN;
		}

		ret = gate_radio_receive(buffer, sizeof(buffer), K_MSEC(remaining), &rx);
		if (ret == -EAGAIN) {
			LOG_WRN("TX ACK timeout seq=%u", sequence);
			return ret;
		}

		if (ret == -EMSGSIZE) {
			LOG_WRN("TX ignored oversized frame while waiting seq=%u", sequence);
			continue;
		}

		if (ret < 0) {
			LOG_ERR("TX ACK receive failed seq=%u ret=%d", sequence, ret);
			return ret;
		}

		status = gate_protocol_decode(buffer, rx.length, &packet);
		if (status != GATE_PROTOCOL_OK) {
			LOG_WRN("TX ignored invalid packet len=%u rssi=%d snr=%d status=%s",
				(unsigned int)rx.length, rx.rssi, rx.snr,
				gate_protocol_status_name(status));
			continue;
		}

		if (!gate_protocol_ack_matches(&packet, CONFIG_GATE_TX_DEVICE_ID, sequence)) {
			LOG_WRN("TX ignored packet type=%s seq=%u device=%u while waiting seq=%u",
				gate_message_type_name(packet.type), packet.sequence,
				packet.device_id, sequence);
			continue;
		}

		LOG_INF("TX ACK received seq=%u rssi=%d snr=%d", packet.sequence, rx.rssi,
			rx.snr);
		return 0;
	}
}

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

	for (;;) {
		struct gate_packet packet;

		sequence = gate_protocol_next_sequence(sequence);
		gate_protocol_init_command(&packet, CONFIG_GATE_TX_DEVICE_ID, sequence,
					   GATE_COMMAND_TRIGGER);

		ret = send_command(&packet);
		if (ret == 0) {
			ret = wait_for_ack(sequence);
			if (ret == 0) {
				LOG_INF("TX command success seq=%u", sequence);
			}
		}

		k_sleep(K_MSEC(CONFIG_GATE_TX_SEND_INTERVAL_MS));
	}

idle:
	for (;;) {
		k_sleep(K_SECONDS(30));
	}

	return 0;
}
