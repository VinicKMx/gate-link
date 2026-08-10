#ifndef GATE_PROTOCOL_H_
#define GATE_PROTOCOL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GATE_PROTOCOL_VERSION 1u

enum gate_message_type {
	GATE_MESSAGE_TYPE_COMMAND = 1,
	GATE_MESSAGE_TYPE_ACK = 2,
};

enum gate_command {
	GATE_COMMAND_TRIGGER = 1,
};

uint8_t gate_protocol_version(void);
const char *gate_message_type_name(enum gate_message_type type);
const char *gate_command_name(enum gate_command command);

#ifdef __cplusplus
}
#endif

#endif /* GATE_PROTOCOL_H_ */

