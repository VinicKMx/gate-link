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
	}

	for (;;) {
		k_sleep(K_SECONDS(30));
	}

	return 0;
}
