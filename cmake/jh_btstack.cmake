include_guard(GLOBAL)

function(_jh_target_enable_btstack TARGET_NAME MODE)
    set(_jh_btstack_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/BTstack")
    set(_jh_bluetooth_root
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/hal/bluetooth")
    if(NOT EXISTS "${_jh_btstack_root}/src/bluetooth.h")
        message(FATAL_ERROR
            "Pinned BTstack is missing; run scripts/ensure_btstack.sh")
    endif()

    set(_jh_btstack_upstream_sources
        "${_jh_btstack_root}/3rd-party/micro-ecc/uECC.c"
        "${_jh_btstack_root}/3rd-party/rijndael/rijndael.c"
        "${_jh_btstack_root}/src/ad_parser.c"
        "${_jh_btstack_root}/src/btstack_crypto.c"
        "${_jh_btstack_root}/src/btstack_linked_list.c"
        "${_jh_btstack_root}/src/btstack_memory.c"
        "${_jh_btstack_root}/src/btstack_memory_pool.c"
        "${_jh_btstack_root}/src/btstack_ring_buffer.c"
        "${_jh_btstack_root}/src/btstack_run_loop.c"
        "${_jh_btstack_root}/src/btstack_run_loop_base.c"
        "${_jh_btstack_root}/src/btstack_tlv.c"
        "${_jh_btstack_root}/src/btstack_tlv_none.c"
        "${_jh_btstack_root}/src/btstack_util.c"
        "${_jh_btstack_root}/src/hci.c"
        "${_jh_btstack_root}/src/hci_cmd.c"
        "${_jh_btstack_root}/src/hci_dump.c"
        "${_jh_btstack_root}/src/hci_event.c"
        "${_jh_btstack_root}/src/hci_event_builder.c"
        "${_jh_btstack_root}/src/l2cap.c"
        "${_jh_btstack_root}/src/l2cap_signaling.c"
        "${_jh_btstack_root}/src/ble/att_db.c"
        "${_jh_btstack_root}/src/ble/att_db_util.c"
        "${_jh_btstack_root}/src/ble/att_dispatch.c"
        "${_jh_btstack_root}/src/ble/att_server.c"
        "${_jh_btstack_root}/src/ble/gatt_client.c"
        "${_jh_btstack_root}/src/ble/le_device_db_memory.c"
        "${_jh_btstack_root}/src/ble/sm.c"
        "${_jh_btstack_root}/platform/embedded/btstack_run_loop_embedded.c")
    set(_jh_jh_sources
        "${_jh_bluetooth_root}/jh_bluetooth_controller_cyw43.c"
        "${_jh_bluetooth_root}/jh_bluetooth_host_runtime.c"
        "${_jh_bluetooth_root}/jh_bluetooth_hci_transport.c"
        "${_jh_bluetooth_root}/jh_btstack_host.c"
        "${_jh_bluetooth_root}/jh_btstack_port.c"
        "${_jh_bluetooth_root}/jh_btstack_chipset_cyw43.c"
        "${_jh_bluetooth_root}/jh_btstack_hci_transport_cyw43.c"
        "${_jh_bluetooth_root}/jh_btstack_run_loop.c")
    if(MODE STREQUAL "STAGE1")
        list(APPEND _jh_jh_sources
            "${_jh_bluetooth_root}/jh_bluetooth_stage1_probe.c")
        set(_jh_gatt_source "${_jh_bluetooth_root}/jh_stage1_probe.gatt")
        set(_jh_gatt_header "jh_stage1_probe_gatt.h")
        set(_jh_mode_definitions JH_BLUETOOTH_STAGE1_PROBE=1)
    elseif(MODE STREQUAL "PUBLIC" OR MODE STREQUAL "PUBLIC_STREAM")
        list(APPEND _jh_jh_sources
            "${_jh_bluetooth_root}/jh_ble_btstack_backend.c")
        set(_jh_gatt_header "jh_ble_peripheral_gatt.h")
        set(_jh_mode_definitions
            JH_BLUETOOTH_PUBLIC_BLE=1
            ENABLE_LE_CENTRAL=1)
        if(MODE STREQUAL "PUBLIC_STREAM")
            list(APPEND _jh_jh_sources
                "${_jh_bluetooth_root}/jh_ble_stream_session.c")
            set(_jh_gatt_source "${_jh_bluetooth_root}/jh_ble_stream.gatt")
            list(APPEND _jh_mode_definitions JH_BLUETOOTH_BLE_STREAM=1)
        else()
            set(_jh_gatt_source "${_jh_bluetooth_root}/jh_ble_peripheral.gatt")
        endif()
    else()
        message(FATAL_ERROR "Unknown JaszczurHAL BTstack mode: ${MODE}")
    endif()
    set(_jh_btstack_sources
        ${_jh_btstack_upstream_sources}
        ${_jh_jh_sources})

    foreach(_jh_source IN LISTS _jh_btstack_sources)
        if(NOT EXISTS "${_jh_source}")
            message(FATAL_ERROR "Pinned BTstack source is missing: ${_jh_source}")
        endif()
    endforeach()

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(_jh_generated_dir
        "${CMAKE_CURRENT_BINARY_DIR}/jh_btstack/${TARGET_NAME}")
    file(MAKE_DIRECTORY "${_jh_generated_dir}")
    set(_jh_generated_gatt "${_jh_generated_dir}/${_jh_gatt_header}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}"
            "${_jh_btstack_root}/tool/compile_gatt.py"
            "${_jh_gatt_source}"
            "${_jh_generated_gatt}"
        RESULT_VARIABLE _jh_gatt_result
        OUTPUT_VARIABLE _jh_gatt_stdout
        ERROR_VARIABLE _jh_gatt_stderr)
    if(NOT _jh_gatt_result EQUAL 0)
        message(FATAL_ERROR
            "BTstack GATT generation failed (${_jh_gatt_result}):\n"
            "${_jh_gatt_stdout}${_jh_gatt_stderr}")
    endif()

    target_sources(${TARGET_NAME} PRIVATE ${_jh_btstack_sources})
    target_include_directories(${TARGET_NAME} BEFORE PRIVATE
        "${_jh_generated_dir}"
        "${_jh_bluetooth_root}"
        "${_jh_btstack_root}"
        "${_jh_btstack_root}/src"
        "${_jh_btstack_root}/platform/embedded"
        "${_jh_btstack_root}/3rd-party/micro-ecc"
        "${_jh_btstack_root}/3rd-party/rijndael")
    target_compile_definitions(${TARGET_NAME} PRIVATE
        ENABLE_BLE=1
        HAVE_BTSTACK_CONFIG_H=1
        JH_BLUETOOTH_BTSTACK=1
        ${_jh_mode_definitions})
    # Keep compatibility suppressions scoped to pinned upstream code.  The JH
    # port and probe remain subject to the target's complete warning policy.
    set_source_files_properties(${_jh_btstack_upstream_sources} PROPERTIES
        COMPILE_OPTIONS
            "-Wno-unused-parameter;-Wno-unused-function;-Wno-unused-variable;-Wno-sign-compare;-Wno-missing-field-initializers")
endfunction()

function(jh_target_enable_btstack_stage1 TARGET_NAME)
    _jh_target_enable_btstack(${TARGET_NAME} STAGE1)
endfunction()

function(jh_target_enable_btstack_ble TARGET_NAME)
    _jh_target_enable_btstack(${TARGET_NAME} PUBLIC)
endfunction()

function(jh_target_enable_btstack_ble_stream TARGET_NAME)
    _jh_target_enable_btstack(${TARGET_NAME} PUBLIC_STREAM)
endfunction()
