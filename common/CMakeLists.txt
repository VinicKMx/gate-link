target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/protocol/gate_protocol.c)

