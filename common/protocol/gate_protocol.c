#include "protocol/gate_protocol.h"

uint8_t gate_protocol_version(void)
{
	return GATE_PROTOCOL_VERSION;
}

const char *gate_message_type_name(enum gate_message_type type)
{
	switch (type) {
	case GATE_MESSAGE_TYPE_COMMAND:
		return "COMMAND";
	case GATE_MESSAGE_TYPE_ACK:
		return "ACK";
	default:
		return "UNKNOWN";
	}
}

const char *gate_command_name(enum gate_command command)
{
	switch (command) {
	case GATE_COMMAND_TRIGGER:
		return "TRIGGER";
	default:
		return "UNKNOWN";
	}
}

