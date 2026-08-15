/*
 * Copyright (c) 2026 Vinicius Pedrosa
 *
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
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

static const struct gpio_dt_spec actuator = GPIO_DT_SPEC_GET(DT_ALIAS(gate_actuator), gpios);
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(DT_ALIAS(gate_status), gpios);

/* Pacing for actuator_force_safe(), which must never become a busy loop. */
#define ACTUATOR_SAFE_RETRY_MS 100

static void set_status_led(bool enabled)
{
	int ret = gpio_pin_set_dt(&status_led, enabled ? 1 : 0);

	if (ret < 0) {
		LOG_ERR("RX status LED set failed: %d", ret);
	}
}

static int rx_io_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&actuator)) {
		LOG_ERR("RX actuator GPIO is not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&status_led)) {
		LOG_ERR("RX status LED GPIO is not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&actuator, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("RX actuator configure failed: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("RX status LED configure failed: %d", ret);
		return ret;
	}

	return 0;
}

/*
 * Drive the actuator back to its safe state, retrying until it gets there.
 *
 * Failing to energize the output only costs one missed command: no ACK is sent
 * and the transmitter retries. Failing to de-energize it is the worst failure
 * this firmware has, because the output stays active with nothing left to
 * lower it, and on the installed hardware that is a gate held open (D003).
 * There is nothing more useful a receiver can do in that state than keep
 * trying, so this mirrors radio_wait_ready(): retry forever, paced (D012).
 */
static void actuator_force_safe(void)
{
	for (;;) {
		int ret = gpio_pin_set_dt(&actuator, 0);

		if (ret == 0) {
			return;
		}

		LOG_ERR("RX actuator stuck active (%d), retrying in %u ms", ret,
			ACTUATOR_SAFE_RETRY_MS);
		k_sleep(K_MSEC(ACTUATOR_SAFE_RETRY_MS));
	}
}

static int actuator_trigger(void)
{
	int ret;

	ret = gpio_pin_set_dt(&actuator, 1);
	if (ret < 0) {
		LOG_ERR("RX actuator ON failed: %d", ret);
		return ret;
	}

	k_sleep(K_MSEC(CONFIG_GATE_RX_ACTUATOR_PULSE_MS));

	actuator_force_safe();

	return 0;
}

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

/*
 * Refresh the radio after each receive timeout.
 *
 * With the RFM95W powered off at runtime, lora_recv() can keep returning
 * -EAGAIN rather than a hard bus error. A timeout is still normal when the link
 * is idle, so the success path is intentionally quiet; the important behavior
 * is to notice a missing chip and to re-apply RX mode after power returns.
 */
static void refresh_radio_after_timeout(void)
{
	int ret = gate_radio_refresh_rx();

	if (ret == 0) {
		return;
	}

	LOG_WRN("RX radio refresh failed after timeout: %d", ret);
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

	/*
	 * Unlike a radio failure, this is not retried: an unbindable pin means
	 * the overlay does not match this board, and no retry creates it (D014).
	 */
	ret = rx_io_init();
	if (ret < 0) {
		LOG_ERR("RX I/O init failed: %d", ret);
		return 0;
	}

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
			refresh_radio_after_timeout();
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

		/*
		 * The status LED means "this packet was accepted and is being
		 * answered", so it is lit only on the paths that reach
		 * send_ack() and stays off for packets that are dropped.
		 */
		switch (gate_sequence_filter_command(sequence_trackers,
						     ARRAY_SIZE(sequence_trackers), &packet)) {
		case GATE_SEQUENCE_DECISION_EXECUTE:
			set_status_led(true);
			LOG_INF("RX actuator trigger seq=%u device=%u", packet.sequence,
				packet.device_id);
			ret = actuator_trigger();
			if (ret < 0) {
				LOG_ERR("RX actuator failed seq=%u ret=%d", packet.sequence, ret);
				set_status_led(false);
				continue;
			}
			/*
			 * Recording only happens once the pulse completed, so a
			 * failed attempt stays retriable. This guard cannot fire
			 * today, since the filter above already proved the
			 * packet is a valid COMMAND from a tracked identity, but
			 * an unrecorded sequence would re-trigger the actuator on
			 * every retransmission, so it refuses to ACK instead.
			 */
			if (!gate_sequence_accept_command(sequence_trackers,
							  ARRAY_SIZE(sequence_trackers), &packet)) {
				LOG_ERR("RX sequence accept failed seq=%u device=%u",
					packet.sequence, packet.device_id);
				set_status_led(false);
				continue;
			}
			break;
		case GATE_SEQUENCE_DECISION_DUPLICATE:
			set_status_led(true);
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
		set_status_led(false);
		if (ret < 0) {
			LOG_ERR("RX failed to acknowledge seq=%u ret=%d", packet.sequence, ret);
			note_radio_failure(&radio_failures);
		}
	}

	return 0;
}
