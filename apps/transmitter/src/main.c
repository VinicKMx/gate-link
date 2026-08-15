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

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(gate_button), gpios);
static const struct gpio_dt_spec status_ok = GPIO_DT_SPEC_GET(DT_ALIAS(gate_status_ok), gpios);
static const struct gpio_dt_spec status_error =
    GPIO_DT_SPEC_GET(DT_ALIAS(gate_status_error), gpios);

/*
 * Read the button as a logical level. Zephyr 3.7's ESP32 GPIO path returns the
 * physical input level here, so the devicetree active-low flag is applied at
 * this boundary and the rest of the transmitter sees "pressed" as true.
 *
 * A read error is reported to the caller instead of being folded into
 * "released": a button that cannot be read must not look like an idle one.
 */
static int button_read(bool *pressed)
{
	int level = gpio_pin_get_raw(button.port, button.pin);

	if (level < 0) {
		return level;
	}

	if ((button.dt_flags & GPIO_ACTIVE_LOW) != 0) {
		*pressed = (level == 0);
	} else {
		*pressed = (level != 0);
	}

	return 0;
}

static void set_status_outputs(bool ok, bool error)
{
	int ret;

	ret = gpio_pin_set_dt(&status_ok, ok ? 1 : 0);
	if (ret < 0) {
		LOG_ERR("TX success LED set failed: %d", ret);
	}

	ret = gpio_pin_set_dt(&status_error, error ? 1 : 0);
	if (ret < 0) {
		LOG_ERR("TX error LED set failed: %d", ret);
	}
}

static int tx_io_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("TX button GPIO is not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&status_ok)) {
		LOG_ERR("TX success LED GPIO is not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&status_error)) {
		LOG_ERR("TX error LED GPIO is not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("TX button configure failed: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&status_ok, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("TX success LED configure failed: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&status_error, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("TX error LED configure failed: %d", ret);
		return ret;
	}

	return 0;
}

/*
 * Block until the button holds @p wanted across the debounce interval.
 *
 * Read errors keep the loop waiting rather than reporting a state change, so a
 * broken button never fabricates a command. They are logged once per stretch of
 * failures: this loop runs every few milliseconds, and logging every read would
 * bury the console under one repeated line.
 */
static void wait_for_button_state(bool wanted)
{
	bool error_reported = false;

	for (;;) {
		bool state;
		int ret = button_read(&state);

		if (ret < 0) {
			if (!error_reported) {
				LOG_ERR("TX button read failed: %d", ret);
				error_reported = true;
			}
		} else {
			if (error_reported) {
				LOG_INF("TX button read recovered");
				error_reported = false;
			}

			if (state == wanted) {
				k_sleep(K_MSEC(CONFIG_GATE_TX_BUTTON_DEBOUNCE_MS));

				if (button_read(&state) == 0 && state == wanted) {
					return;
				}
			}
		}

		k_sleep(K_MSEC(CONFIG_GATE_TX_BUTTON_POLL_MS));
	}
}

static void indicate_success(void)
{
	set_status_outputs(true, false);
	k_sleep(K_MSEC(CONFIG_GATE_TX_SUCCESS_LED_MS));
	set_status_outputs(false, false);
}

static void indicate_error(void)
{
	set_status_outputs(false, true);
	k_sleep(K_MSEC(CONFIG_GATE_TX_ERROR_LED_MS));
	set_status_outputs(false, false);
}

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

	LOG_INF("TX sending %s seq=%u device=%u bytes=%u", gate_command_name(packet->command),
		packet->sequence, packet->device_id, (unsigned int)written);

	ret = gate_radio_send(buffer, written);
	if (ret < 0) {
		LOG_ERR("TX send failed seq=%u ret=%d", packet->sequence, ret);
		return ret;
	}

	LOG_INF("TX sent seq=%u", packet->sequence);

	return 0;
}

static int wait_for_ack(const struct gate_packet *command)
{
	int64_t deadline = k_uptime_get() + CONFIG_GATE_TX_ACK_TIMEOUT_MS;
	int ret;

	ret = gate_radio_configure_rx();
	if (ret < 0) {
		LOG_ERR("TX radio RX config failed: %d", ret);
		return -EIO;
	}

	LOG_INF("TX waiting for ACK seq=%u command=%s timeout=%u ms", command->sequence,
		gate_command_name(command->command), CONFIG_GATE_TX_ACK_TIMEOUT_MS);

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
			LOG_WRN("TX ACK timeout seq=%u", command->sequence);
			return -EAGAIN;
		}

		ret = gate_radio_receive(buffer, sizeof(buffer), K_MSEC(remaining), &rx);
		if (ret == -EAGAIN) {
			LOG_WRN("TX ACK timeout seq=%u", command->sequence);
			return ret;
		}

		if (ret == -EMSGSIZE) {
			LOG_WRN("TX ignored oversized frame while waiting seq=%u",
				command->sequence);
			continue;
		}

		if (ret < 0) {
			LOG_ERR("TX ACK receive failed seq=%u ret=%d", command->sequence, ret);
			return -EIO;
		}

		status = gate_protocol_decode(buffer, rx.length, &packet);
		if (status != GATE_PROTOCOL_OK) {
			LOG_WRN("TX ignored invalid packet len=%u rssi=%d snr=%d status=%s",
				(unsigned int)rx.length, rx.rssi, rx.snr,
				gate_protocol_status_name(status));
			continue;
		}

		if (!gate_protocol_ack_matches(&packet, command->device_id, command->sequence,
					       command->command)) {
			LOG_WRN("TX ignored packet type=%s command=%s seq=%u device=%u while "
				"waiting seq=%u command=%s",
				gate_message_type_name(packet.type),
				gate_command_name(packet.command), packet.sequence,
				packet.device_id, command->sequence,
				gate_command_name(command->command));
			continue;
		}

		LOG_INF("TX ACK received seq=%u rssi=%d snr=%d", packet.sequence, rx.rssi, rx.snr);
		return 0;
	}
}

/*
 * Block until the radio answers again.
 *
 * There is nothing useful a remote trigger can do without a radio, so this
 * retries indefinitely instead of parking the board in a dead idle loop: a
 * module that was unpowered, unwired, or wedged heals without a power cycle.
 * The pacing keeps the console readable and stops it becoming a busy loop.
 */
static void radio_wait_ready(void)
{
	while (gate_radio_init() < 0) {
		LOG_ERR("TX radio unavailable, retrying in %u ms",
			CONFIG_GATE_RADIO_RECOVERY_INTERVAL_MS);
		k_sleep(K_MSEC(CONFIG_GATE_RADIO_RECOVERY_INTERVAL_MS));
	}
}

static int transmit_command_with_retries(const struct gate_packet *packet)
{
	const uint32_t max_retries = CONFIG_GATE_TX_MAX_RETRIES;
	const uint32_t total_attempts = max_retries + 1u;

	for (uint32_t attempt = 1u; attempt <= total_attempts; attempt++) {
		int ret;

		if (attempt == 1u) {
			LOG_INF("TX command attempt %u/%u seq=%u", attempt, total_attempts,
				packet->sequence);
		} else {
			LOG_WRN("TX retry %u/%u seq=%u", attempt - 1u, max_retries,
				packet->sequence);
		}

		/* send_command() already logged the specific failure. */
		ret = send_command(packet);
		if (ret < 0) {
			return ret == -EINVAL ? ret : -EIO;
		}

		ret = wait_for_ack(packet);
		if (ret == 0) {
			LOG_INF("TX command success seq=%u attempts=%u", packet->sequence, attempt);
			return 0;
		}

		if (ret != -EAGAIN) {
			LOG_ERR("TX command aborted seq=%u ret=%d", packet->sequence, ret);
			return ret == -EINVAL ? ret : -EIO;
		}
	}

	LOG_ERR("TX command final failure seq=%u attempts=%u retries=%u", packet->sequence,
		total_attempts, max_retries);

	/* The loop can only fall through after wait_for_ack() timed out. */
	return -EAGAIN;
}

static bool command_error_is_local_radio_failure(int ret)
{
	/* At this boundary, -EAGAIN only means "sent but no ACK arrived". */
	return ret != 0 && ret != -EAGAIN && ret != -EINVAL;
}

static void recover_radio_after_command_error(int ret)
{
	LOG_WRN("TX recovering radio after local command error: %d", ret);
	radio_wait_ready();
	LOG_INF("TX radio recovered after command error");
}

int main(void)
{
	uint32_t sequence = GATE_SEQUENCE_NONE;
	int ret;

	LOG_INF("TX boot");
	LOG_INF("TX protocol version=%u packet=%u bytes command=%s", gate_protocol_version(),
		GATE_PROTOCOL_PACKET_SIZE, gate_command_name(GATE_COMMAND_TRIGGER));

	/*
	 * Unlike a radio failure, this is not retried: an unbindable pin means
	 * the overlay does not match this board, and no retry creates it (D014).
	 */
	ret = tx_io_init();
	if (ret < 0) {
		LOG_ERR("TX I/O init failed: %d", ret);
		return 0;
	}

	if (!gate_radio_is_present()) {
		LOG_WRN("TX has no LoRa radio in this build, idling");
		k_sleep(K_FOREVER);
		return 0;
	}

	radio_wait_ready();

	for (;;) {
		struct gate_packet packet;

		LOG_INF("TX ready, waiting for button");
		wait_for_button_state(true);
		LOG_INF("TX button pressed");

		sequence = gate_protocol_next_sequence(sequence);
		gate_protocol_init_command(&packet, CONFIG_GATE_TX_DEVICE_ID, sequence,
					   GATE_COMMAND_TRIGGER);

		ret = transmit_command_with_retries(&packet);

		if (ret == 0) {
			indicate_success();
		} else {
			indicate_error();
		}

		/*
		 * -EAGAIN means the receiver never answered, which says nothing
		 * about the local radio. -EINVAL comes from local packet
		 * construction and retrying the radio cannot fix it. Other
		 * command errors are local radio failures and recover now, so a
		 * powered-down TX module while idle does not require multiple
		 * button presses to heal.
		 */
		if (command_error_is_local_radio_failure(ret)) {
			recover_radio_after_command_error(ret);
		}

		LOG_INF("TX waiting for button release");
		wait_for_button_state(false);
		LOG_INF("TX button released");
	}

	return 0;
}
