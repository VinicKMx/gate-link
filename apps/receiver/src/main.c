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
#include <sequence/gate_sequence_filter.h>

LOG_MODULE_REGISTER(gate_rx, CONFIG_GATE_RX_LOG_LEVEL);

/*
 * Phase 1 binds these aliases to GPIO handles. Asserting them now keeps a
 * missing or misnamed board overlay a build error instead of a bench surprise.
 */
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(gate_actuator)),
	     "board overlay must define the gate-actuator alias");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(gate_status)),
	     "board overlay must define the gate-status alias");

static int send_ack(const struct gate_packet *command)
{
	struct gate_packet ack;
	uint8_t buffer[GATE_PROTOCOL_PACKET_SIZE];
	size_t written;
	enum gate_protocol_status status;
	int send_ret;
	int rx_ret;

	gate_protocol_init_ack(&ack, command->device_id, command->sequence, command->command);

	status = gate_protocol_encode(&ack, buffer, sizeof(buffer), &written);
	if (status != GATE_PROTOCOL_OK) {
		LOG_ERR("RX ACK encode failed seq=%u status=%s", command->sequence,
			gate_protocol_status_name(status));
		return -EINVAL;
	}

	send_ret = gate_radio_configure_tx();
	if (send_ret < 0) {
		LOG_ERR("RX radio TX config failed: %d", send_ret);
		return send_ret;
	}

	LOG_INF("RX sending ACK seq=%u device=%u bytes=%u", ack.sequence, ack.device_id,
		(unsigned int)written);

	send_ret = gate_radio_send(buffer, written);
	if (send_ret < 0) {
		LOG_ERR("RX ACK send failed seq=%u ret=%d", ack.sequence, send_ret);
	} else {
		LOG_INF("RX ACK sent seq=%u", ack.sequence);
	}

	rx_ret = gate_radio_configure_rx();
	if (rx_ret < 0) {
		LOG_ERR("RX radio RX config failed after ACK: %d", rx_ret);
		return rx_ret;
	}

	return send_ret;
}

/*
 * Block until the radio answers and is listening again.
 *
 * There is nothing useful a receiver can do without a radio, so this retries
 * indefinitely instead of parking the board in a dead idle loop: a module that
 * was unpowered, unwired, or wedged heals without a power cycle. The receiver
 * must always end up back in RX mode, so probing alone is not enough.
 */
static void radio_wait_ready(void)
{
	for (;;) {
		if (gate_radio_init() == 0 && gate_radio_configure_rx() == 0) {
			return;
		}

		LOG_ERR("RX radio unavailable, retrying in %u ms",
			CONFIG_GATE_RADIO_RECOVERY_INTERVAL_MS);
		k_sleep(K_MSEC(CONFIG_GATE_RADIO_RECOVERY_INTERVAL_MS));
	}
}

/*
 * Count one radio error and recover once the threshold is reached. Only genuine
 * radio errors may reach this function; a receive timeout means the radio
 * listened and heard nothing, which is normal.
 */
static void note_radio_failure(uint32_t *failures)
{
	if (++(*failures) < (uint32_t)CONFIG_GATE_RADIO_RECOVERY_THRESHOLD) {
		return;
	}

	LOG_WRN("RX recovering radio after %u consecutive failures", *failures);
	*failures = 0u;

	radio_wait_ready();

	LOG_INF("RX radio recovered");
}

int main(void)
{
	struct gate_sequence_tracker sequence_trackers[1];
	uint32_t radio_failures = 0u;
	int ret;

	LOG_INF("RX boot");
	LOG_INF("RX protocol version=%u packet=%u bytes command=%s", gate_protocol_version(),
		GATE_PROTOCOL_PACKET_SIZE, gate_command_name(GATE_COMMAND_TRIGGER));
	gate_sequence_tracker_init(&sequence_trackers[0], CONFIG_GATE_RX_ACCEPTED_DEVICE_ID);

	if (!gate_radio_is_present()) {
		LOG_WRN("RX has no LoRa radio in this build, idling");
		k_sleep(K_FOREVER);
		return 0;
	}

	radio_wait_ready();

	for (;;) {
		/* One byte above the packet size, so an oversized frame is
		 * reported instead of silently truncated (see gate_radio.h).
		 */
		uint8_t buffer[GATE_PROTOCOL_PACKET_SIZE + 1u];
		struct gate_radio_rx_result rx;
		struct gate_packet packet;
		enum gate_protocol_status status;

		ret = gate_radio_receive(buffer, sizeof(buffer),
					 K_MSEC(CONFIG_GATE_RX_RECEIVE_TIMEOUT_MS), &rx);

		/*
		 * A timeout and an oversized frame both prove the radio is
		 * working, so only unexpected errors count toward recovery.
		 */
		if (ret < 0 && ret != -EAGAIN && ret != -EMSGSIZE) {
			LOG_ERR("RX receive failed: %d", ret);
			note_radio_failure(&radio_failures);
			continue;
		}

		radio_failures = 0u;

		if (ret == -EAGAIN) {
			LOG_INF("RX waiting for packets");
			continue;
		}

		if (ret == -EMSGSIZE) {
			LOG_WRN("RX dropped oversized frame");
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

		switch (gate_sequence_filter_command(sequence_trackers,
						     ARRAY_SIZE(sequence_trackers), &packet)) {
		case GATE_SEQUENCE_DECISION_EXECUTE:
			LOG_INF("RX would trigger actuator seq=%u device=%u", packet.sequence,
				packet.device_id);
			break;
		case GATE_SEQUENCE_DECISION_DUPLICATE:
			LOG_WRN("RX duplicate seq=%u device=%u, actuator suppressed",
				packet.sequence, packet.device_id);
			break;
		case GATE_SEQUENCE_DECISION_IGNORE:
			LOG_WRN(
			    "RX ignored command from device=%u seq=%u, accepting only device=%u",
			    packet.device_id, packet.sequence,
			    (unsigned int)CONFIG_GATE_RX_ACCEPTED_DEVICE_ID);
			continue;
		case GATE_SEQUENCE_DECISION_INVALID:
		default:
			LOG_WRN("RX ignored non-command packet type=%s seq=%u device=%u",
				gate_message_type_name(packet.type), packet.sequence,
				packet.device_id);
			continue;
		}

		/* A failed ACK is a local radio problem, not a link problem. */
		ret = send_ack(&packet);
		if (ret < 0) {
			LOG_ERR("RX failed to acknowledge seq=%u ret=%d", packet.sequence, ret);
			note_radio_failure(&radio_failures);
		}
	}

	return 0;
}
