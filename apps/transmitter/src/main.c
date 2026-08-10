#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <protocol/gate_protocol.h>

LOG_MODULE_REGISTER(gate_tx, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("TX boot");
	LOG_INF("TX protocol version=%u command=%s", gate_protocol_version(),
		gate_command_name(GATE_COMMAND_TRIGGER));
	LOG_INF("TX hardware interfaces are not configured in this build");

	for (;;) {
		k_sleep(K_SECONDS(30));
	}

	return 0;
}
