# Copyright (c) 2026 Vinicius Pedrosa
# SPDX-License-Identifier: MIT OR Apache-2.0

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/protocol/gate_protocol.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/sequence/gate_sequence_filter.c)

if(DEFINED CONFIG_GATE_RADIO_LOG_LEVEL)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/radio/gate_radio.c)
endif()
