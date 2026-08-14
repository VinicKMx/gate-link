# Copyright (c) 2026 Vinicius Pedrosa
# SPDX-License-Identifier: MIT OR Apache-2.0

target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/protocol/gate_protocol.c)