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

#include <auth/gate_auth.h>
#include <protocol/gate_protocol.h>
#include <radio/gate_radio.h>
#include <sequence/gate_replay_filter.h>
#if IS_ENABLED(CONFIG_GATE_COUNTER_STORE_NVS)
#include <storage/gate_counter_store.h>
#endif

LOG_MODULE_REGISTER(gate_rx, CONFIG_GATE_RX_LOG_LEVEL);

/*
 * Board overlays bind these aliases to GPIO handles. Asserting them now keeps
 * a missing or misnamed overlay a build error instead of a bench surprise.
 */
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(gate_actuator)),
	     "board overlay must define the gate-actuator alias");
BUILD_ASSERT(DT_NODE_EXISTS(DT_ALIAS(gate_status)),
	     "board overlay must define the gate-status alias");

/*
 * Authentication and replay resistance are configuration, and a missing
 * configuration is a build error for the same reason a missing alias is: no
 * retry produces a key or a storage partition that the build never bound
 * (D014). Catching it here keeps an unprovisioned image from being flashed and
 * then failing silently in the field.
 *
 * Only builds that can actually reach the radio are held to this. A build with
 * no lora0 alias idles on purpose and needs neither a key nor counters (D012).
 * Only the key length is checkable at build time; gate_auth_key_from_hex()
 * still validates the characters at startup.
 */
#if GATE_RADIO_PRESENT
BUILD_ASSERT(sizeof(CONFIG_GATE_AUTH_KEY_HEX) == GATE_AUTH_KEY_HEX_SIZE + 1u,
	     "CONFIG_GATE_AUTH_KEY_HEX must be exactly 64 hex characters; provision it through "
	     "a local unversioned EXTRA_CONF_FILE, never in the repository");
BUILD_ASSERT(IS_ENABLED(CONFIG_GATE_COUNTER_STORE_NVS),
	     "authenticated builds require CONFIG_GATE_COUNTER_STORE_NVS: without persisted "
	     "counters an accepted sequence would be forgotten on reset (D018)");
#endif

static const struct gpio_dt_spec actuator = GPIO_DT_SPEC_GET(DT_ALIAS(gate_actuator), gpios);
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(DT_ALIAS(gate_status), gpios);

static uint8_t auth_key[GATE_AUTH_KEY_SIZE];

#if IS_ENABLED(CONFIG_GATE_COUNTER_STORE_NVS)
static struct gate_counter_store replay_store;
#endif

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

static int auth_init(void)
{
	enum gate_auth_status status;

	status = gate_auth_key_from_hex(CONFIG_GATE_AUTH_KEY_HEX, auth_key, sizeof(auth_key));
	if (status != GATE_AUTH_OK) {
		LOG_ERR("RX authentication key invalid: %s", gate_auth_status_name(status));
		LOG_ERR("RX requires CONFIG_GATE_AUTH_KEY_HEX with 64 hex characters");
		return -EINVAL;
	}

	LOG_INF("RX authentication enabled tag=%u bytes", GATE_PROTOCOL_AUTH_TAG_SIZE);

	return 0;
}

static int replay_store_init(struct gate_replay_tracker *tracker)
{
#if IS_ENABLED(CONFIG_GATE_COUNTER_STORE_NVS)
	uint32_t last_sequence;
	bool found;
	int ret;

	ret = gate_counter_store_init(&replay_store);
	if (ret < 0) {
		LOG_ERR("RX replay store init failed: %d", ret);
		if (ret == -EDEADLK) {
			LOG_ERR("RX replay store is not a valid NVS area; erase storage during "
				"provisioning");
		}
		return ret;
	}

	ret = gate_counter_store_read(&replay_store, GATE_COUNTER_STORE_RX_LAST_SEQUENCE_ID,
				      &last_sequence, &found);
	if (ret < 0) {
		LOG_ERR("RX replay store read failed: %d", ret);
		return ret;
	}

	if (!found || last_sequence == GATE_SEQUENCE_NONE) {
		LOG_INF("RX replay store is empty");
		return 0;
	}

	gate_replay_tracker_set_last_sequence(tracker, last_sequence);
	LOG_INF("RX replay store loaded last=%u device=%u", last_sequence, tracker->device_id);

	return 0;
#else
	ARG_UNUSED(tracker);
	LOG_ERR("RX replay-resistant sequence storage is not enabled");
	return -ENOTSUP;
#endif
}

static int persist_accepted_sequence(uint32_t sequence)
{
#if IS_ENABLED(CONFIG_GATE_COUNTER_STORE_NVS)
	return gate_counter_store_write(&replay_store, GATE_COUNTER_STORE_RX_LAST_SEQUENCE_ID,
					sequence);
#else
	ARG_UNUSED(sequence);
	return -ENOTSUP;
#endif
}

/*
 * Once the actuator pulse completed, the command has happened. The receiver
 * must not ACK that command until the accepted sequence is persistent; otherwise
 * a reboot could make the same signed packet executable again. Retrying here
 * blocks normal receive handling on purpose: no new command is safer than a
 * replayable accepted command.
 */
static void persist_accepted_sequence_or_wait(uint32_t sequence)
{
	for (;;) {
		int ret = persist_accepted_sequence(sequence);

		if (ret == 0) {
			return;
		}

		LOG_ERR("RX replay store write failed seq=%u ret=%d; ACK withheld", sequence, ret);
		set_status_led(false);
		k_sleep(K_MSEC(CONFIG_GATE_RADIO_RECOVERY_INTERVAL_MS));
	}
}

static int send_ack(const struct gate_packet *command)
{
	struct gate_packet ack;
	uint8_t buffer[GATE_PROTOCOL_PACKET_SIZE];
	size_t written;
	enum gate_protocol_status status;
	enum gate_auth_status auth_status;
	int send_ret;
	int rx_ret;

	gate_protocol_init_ack(&ack, command->device_id, command->sequence, command->command);

	auth_status = gate_auth_sign(&ack, auth_key, sizeof(auth_key));
	if (auth_status != GATE_AUTH_OK) {
		LOG_ERR("RX ACK auth sign failed seq=%u status=%s", command->sequence,
			gate_auth_status_name(auth_status));
		return -EINVAL;
	}

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

	LOG_INF("RX radio recovered after runtime failure");
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
	LOG_INF("RX radio recovered after idle timeout");
}

int main(void)
{
	struct gate_replay_tracker replay_trackers[1];
	uint32_t radio_failures = 0u;
	int ret;

	LOG_INF("RX boot");
	LOG_INF("RX protocol version=%u packet=%u bytes command=%s", gate_protocol_version(),
		GATE_PROTOCOL_PACKET_SIZE, gate_command_name(GATE_COMMAND_TRIGGER));
	gate_replay_tracker_init(&replay_trackers[0], CONFIG_GATE_RX_ACCEPTED_DEVICE_ID);

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

	ret = auth_init();
	if (ret < 0) {
		return 0;
	}

	ret = replay_store_init(&replay_trackers[0]);
	if (ret < 0) {
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
		enum gate_auth_status auth_status;

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

		auth_status = gate_auth_verify(&packet, auth_key, sizeof(auth_key));
		if (auth_status != GATE_AUTH_OK) {
			LOG_WRN("RX authentication failed type=%s command=%s seq=%u device=%u "
				"rssi=%d snr=%d status=%s",
				gate_message_type_name(packet.type),
				gate_command_name(packet.command), packet.sequence,
				packet.device_id, rx.rssi, rx.snr,
				gate_auth_status_name(auth_status));
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
		switch (gate_replay_filter_command(replay_trackers, ARRAY_SIZE(replay_trackers),
						   &packet)) {
		case GATE_REPLAY_DECISION_EXECUTE:
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
			 * today, since the replay filter already proved the
			 * packet is a valid COMMAND from a tracked identity, but
			 * an unrecorded sequence would re-trigger the actuator on
			 * every retransmission, so it refuses to ACK instead.
			 */
			if (!gate_replay_accept_command(replay_trackers,
							ARRAY_SIZE(replay_trackers), &packet)) {
				LOG_ERR("RX sequence accept failed seq=%u device=%u",
					packet.sequence, packet.device_id);
				set_status_led(false);
				continue;
			}
			persist_accepted_sequence_or_wait(packet.sequence);
			break;
		case GATE_REPLAY_DECISION_DUPLICATE:
			set_status_led(true);
			LOG_WRN("RX duplicate seq=%u device=%u, actuator suppressed",
				packet.sequence, packet.device_id);
			break;
		case GATE_REPLAY_DECISION_REPLAY:
			LOG_WRN("RX rejected replay seq=%u device=%u last=%u", packet.sequence,
				packet.device_id, replay_trackers[0].last_sequence);
			continue;
		case GATE_REPLAY_DECISION_IGNORE:
			LOG_WRN(
			    "RX ignored command from device=%u seq=%u, accepting only device=%u",
			    packet.device_id, packet.sequence,
			    (unsigned int)CONFIG_GATE_RX_ACCEPTED_DEVICE_ID);
			continue;
		case GATE_REPLAY_DECISION_INVALID:
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
