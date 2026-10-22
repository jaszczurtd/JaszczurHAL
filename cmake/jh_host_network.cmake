include_guard(GLOBAL)

# Static HAL library for host programs and tools: the mock target with
# HAL_NETWORK_BACKEND_HOST, so hal_tcp, hal_udp, the resolver, the HTTP server,
# HTTP file serving and the WebSocket server run over the operating system's
# sockets (POSIX or Winsock), and HAL_HOST_REAL_TIME, so hal_millis() follows
# the host's monotonic clock and hal_delay_ms() sleeps. Link it instead of
# hal_mock; other peripherals keep the mock target's behaviour.
#
# Requires the jb::status target (jh_add_jhbase() from cmake/jh_jhbase.cmake).
function(jh_add_host_network_library TARGET_NAME)
    if(NOT TARGET jb::status)
        message(FATAL_ERROR
            "jh_add_host_network_library requires jb::status; call "
            "jh_add_jhbase() first")
    endif()
    set(_jh_src "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src")
    add_library(${TARGET_NAME} STATIC
        "${_jh_src}/hal/impl/.mock/host_network_backend.cpp"
        "${_jh_src}/hal/impl/.mock/hal_serial.cpp"
        "${_jh_src}/hal/impl/.mock/hal_sync.cpp"
        "${_jh_src}/hal/impl/.mock/hal_system.cpp"
        "${_jh_src}/hal/core/hal_assert.cpp"
        "${_jh_src}/hal/core/hal_config.cpp"
        "${_jh_src}/hal/core/hal_text.cpp"
        "${_jh_src}/hal/core/jh_endian.cpp"
        "${_jh_src}/hal/core/jh_handle_pool.cpp"
        "${_jh_src}/hal/debug/hal_debug_format.cpp"
        "${_jh_src}/hal/serial/hal_serial.cpp"
        "${_jh_src}/hal/system/hal_board.cpp"
        "${_jh_src}/hal/network/hal_net.cpp"
        "${_jh_src}/hal/network/hal_network_utils.cpp"
        "${_jh_src}/hal/network/hal_tcp.cpp"
        "${_jh_src}/hal/network/hal_udp.cpp"
        "${_jh_src}/hal/network/hal_wifi.cpp"
        "${_jh_src}/hal/network/jh_network_backend.cpp"
        "${_jh_src}/hal/network/jh_network_handle_pool.cpp"
        "${_jh_src}/hal/network/native_sockets/jh_native_sockets.cpp"
        "${_jh_src}/hal/network/http/hal_http_server.cpp"
        "${_jh_src}/hal/network/http/hal_http_files.cpp"
        "${_jh_src}/hal/network/websocket/hal_websocket.cpp"
    )
    # Consumers compile HAL headers too; C11 and C++17 are what they need.
    target_compile_features(${TARGET_NAME} PUBLIC c_std_11 cxx_std_17)
    target_include_directories(${TARGET_NAME} PUBLIC "${_jh_src}")
    target_link_libraries(${TARGET_NAME} PUBLIC jb::status)
    target_compile_definitions(${TARGET_NAME} PUBLIC
        HAL_TARGET_MOCK
        HAL_NETWORK_BACKEND_HOST
        HAL_HOST_REAL_TIME
        HAL_ENABLE_TCP
        HAL_ENABLE_UDP
        HAL_ENABLE_HTTP_SERVER
        HAL_ENABLE_HTTP_FILES
        HAL_ENABLE_WEBSOCKET
    )
    find_package(Threads REQUIRED)
    target_link_libraries(${TARGET_NAME} PUBLIC Threads::Threads)
    if(WIN32)
        target_link_libraries(${TARGET_NAME} PUBLIC ws2_32)
    endif()
endfunction()
