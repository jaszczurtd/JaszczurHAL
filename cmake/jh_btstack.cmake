include_guard(GLOBAL)

function(_jh_target_enable_btstack TARGET_NAME)
    cmake_parse_arguments(JH_BTSTACK
        "STAGE1;CLASSIC_HID;CLASSIC_HID_DEVICE_FIXTURE;BLE;BLE_STREAM;CLASSIC;HID_HOST;A2DP_SINK;AVRCP_TARGET"
        "" "" ${ARGN})
    if(JH_BTSTACK_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Unknown JaszczurHAL BTstack arguments: "
            "${JH_BTSTACK_UNPARSED_ARGUMENTS}")
    endif()

    set(_jh_private_mode_count 0)
    foreach(_jh_private_mode IN ITEMS
            STAGE1 CLASSIC_HID CLASSIC_HID_DEVICE_FIXTURE)
        if(JH_BTSTACK_${_jh_private_mode})
            math(EXPR _jh_private_mode_count "${_jh_private_mode_count} + 1")
        endif()
    endforeach()
    if(_jh_private_mode_count GREATER 1)
        message(FATAL_ERROR "BTstack private modes are mutually exclusive")
    endif()
    if(_jh_private_mode_count GREATER 0 AND
       (JH_BTSTACK_BLE OR JH_BTSTACK_BLE_STREAM OR JH_BTSTACK_CLASSIC OR
        JH_BTSTACK_HID_HOST OR JH_BTSTACK_A2DP_SINK OR
        JH_BTSTACK_AVRCP_TARGET))
        message(FATAL_ERROR
            "A BTstack private mode cannot use public profile arguments")
    endif()
    if(JH_BTSTACK_BLE_STREAM)
        set(JH_BTSTACK_BLE TRUE)
    endif()
    if(JH_BTSTACK_HID_HOST)
        set(JH_BTSTACK_CLASSIC TRUE)
    endif()
    if(JH_BTSTACK_AVRCP_TARGET)
        set(JH_BTSTACK_A2DP_SINK TRUE)
    endif()
    if(JH_BTSTACK_A2DP_SINK)
        set(JH_BTSTACK_CLASSIC TRUE)
    endif()
    if(_jh_private_mode_count EQUAL 0 AND
       NOT JH_BTSTACK_BLE AND NOT JH_BTSTACK_CLASSIC)
        message(FATAL_ERROR "BTstack requires at least one profile")
    endif()

    set(_jh_btstack_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/BTstack")
    set(_jh_bluetooth_root
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/hal/bluetooth")
    if(NOT EXISTS "${_jh_btstack_root}/src/bluetooth.h")
        message(FATAL_ERROR
            "Pinned BTstack is missing; run scripts/ensure_btstack.sh")
    endif()

    set(_jh_generated_dir
        "${CMAKE_CURRENT_BINARY_DIR}/jh_btstack/${TARGET_NAME}")

    set(_jh_btstack_base_sources
        "${_jh_btstack_root}/src/ad_parser.c"
        "${_jh_btstack_root}/src/btstack_linked_list.c"
        "${_jh_btstack_root}/src/btstack_memory.c"
        "${_jh_btstack_root}/src/btstack_memory_pool.c"
        "${_jh_btstack_root}/src/btstack_ring_buffer.c"
        "${_jh_btstack_root}/src/btstack_run_loop.c"
        "${_jh_btstack_root}/src/btstack_run_loop_base.c"
        "${_jh_btstack_root}/src/btstack_util.c"
        "${_jh_btstack_root}/src/hci.c"
        "${_jh_btstack_root}/src/hci_cmd.c"
        "${_jh_btstack_root}/src/hci_dump.c"
        "${_jh_btstack_root}/src/hci_event.c"
        "${_jh_btstack_root}/src/l2cap.c"
        "${_jh_btstack_root}/src/l2cap_signaling.c"
        "${_jh_btstack_root}/platform/embedded/btstack_run_loop_embedded.c")
    set(_jh_btstack_ble_sources
        "${_jh_btstack_root}/3rd-party/micro-ecc/uECC.c"
        "${_jh_btstack_root}/3rd-party/rijndael/rijndael.c"
        "${_jh_btstack_root}/src/btstack_crypto.c"
        "${_jh_btstack_root}/src/btstack_tlv.c"
        "${_jh_btstack_root}/src/btstack_tlv_none.c"
        "${_jh_btstack_root}/src/hci_event_builder.c"
        "${_jh_btstack_root}/src/ble/att_db.c"
        "${_jh_btstack_root}/src/ble/att_db_util.c"
        "${_jh_btstack_root}/src/ble/att_dispatch.c"
        "${_jh_btstack_root}/src/ble/att_server.c"
        "${_jh_btstack_root}/src/ble/gatt_client.c"
        "${_jh_btstack_root}/src/ble/le_device_db_memory.c"
        "${_jh_btstack_root}/src/ble/sm.c")
    set(_jh_btstack_classic_sources
        "${_jh_btstack_root}/src/classic/btstack_link_key_db_memory.c"
        "${_jh_btstack_root}/src/classic/sdp_client.c"
        "${_jh_btstack_root}/src/classic/sdp_util.c")
    set(_jh_btstack_hid_host_sources
        "${_jh_btstack_root}/src/btstack_hid.c"
        "${_jh_btstack_root}/src/classic/hid_host.c")
    set(_jh_btstack_hid_device_sources
        "${_jh_btstack_root}/src/btstack_hid.c"
        "${_jh_btstack_root}/src/btstack_hid_parser.c"
        "${_jh_btstack_root}/src/classic/hid_device.c"
        "${_jh_btstack_root}/src/classic/sdp_server.c")
    set(_jh_btstack_a2dp_sink_sources
        "${_jh_btstack_root}/src/classic/a2dp.c"
        "${_jh_btstack_root}/src/classic/a2dp_sink.c"
        "${_jh_btstack_root}/src/classic/avdtp.c"
        "${_jh_btstack_root}/src/classic/avdtp_acceptor.c"
        "${_jh_btstack_root}/src/classic/avdtp_initiator.c"
        "${_jh_btstack_root}/src/classic/avdtp_sink.c"
        "${_jh_btstack_root}/src/classic/avdtp_util.c"
        "${_jh_btstack_root}/src/classic/btstack_sbc_bluedroid.c"
        "${_jh_btstack_root}/src/classic/btstack_sbc_plc.c"
        "${_jh_btstack_root}/src/classic/sdp_server.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/alloc.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/bitalloc.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/bitalloc-sbc.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/bitstream-decode.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/decoder-oina.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/decoder-private.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/decoder-sbc.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/dequant.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/framing.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/framing-sbc.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/oi_codec_version.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/synthesis-sbc.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/synthesis-dct8.c"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/srce/synthesis-8-generated.c")
    set(_jh_btstack_avrcp_target_sources
        "${_jh_btstack_root}/src/classic/avrcp.c"
        "${_jh_btstack_root}/src/classic/avrcp_target.c")
    set(_jh_jh_base_sources
        "${_jh_bluetooth_root}/jh_bluetooth_controller_cyw43.c"
        "${_jh_bluetooth_root}/jh_bluetooth_host_runtime.c"
        "${_jh_bluetooth_root}/jh_bluetooth_hci_transport.c"
        "${_jh_bluetooth_root}/jh_btstack_host.c"
        "${_jh_bluetooth_root}/jh_btstack_port.c"
        "${_jh_bluetooth_root}/jh_btstack_chipset_cyw43.c"
        "${_jh_bluetooth_root}/jh_btstack_hci_transport_cyw43.c"
        "${_jh_bluetooth_root}/jh_btstack_run_loop.c")
    set(_jh_jh_classic_hid_sources
        "${_jh_bluetooth_root}/jh_bluetooth_gamepad_parser.c"
        "${_jh_bluetooth_root}/jh_bluetooth_classic_hid_lifecycle.c"
        "${_jh_bluetooth_root}/jh_bluetooth_classic_hid_memory_probe.c"
        "${_jh_bluetooth_root}/jh_bluetooth_classic_hid_probe_logic.c"
        "${_jh_bluetooth_root}/jh_bluetooth_classic_bond_codec.c"
        "${_jh_bluetooth_root}/jh_gamepad_bond_codec.c"
        "${_jh_bluetooth_root}/jh_bluetooth_classic_hid_probe.c")
    set(_jh_jh_public_classic_sources
        "${_jh_bluetooth_root}/jh_bluetooth_classic_btstack_backend.c")
    if(JH_BTSTACK_STAGE1)
        set(_jh_mode_upstream_sources ${_jh_btstack_ble_sources})
        set(_jh_mode_jh_sources
            "${_jh_bluetooth_root}/jh_bluetooth_stage1_probe.c")
        set(_jh_gatt_source "${_jh_bluetooth_root}/jh_stage1_probe.gatt")
        set(_jh_gatt_header "jh_stage1_probe_gatt.h")
        set(_jh_mode_definitions
            ENABLE_BLE=1
            JH_BLUETOOTH_STAGE1_PROBE=1)
    elseif(JH_BTSTACK_CLASSIC_HID)
        set(_jh_mode_upstream_sources
            ${_jh_btstack_classic_sources}
            ${_jh_btstack_hid_host_sources})
        set(_jh_mode_jh_sources ${_jh_jh_classic_hid_sources})
        set(_jh_mode_definitions
            ENABLE_CLASSIC=1
            ENABLE_SDP_EXTRA_QUERIES=1
            HAL_ENABLE_CRC=1
            JH_BLUETOOTH_CLASSIC_HID_PROBE=1)
    elseif(JH_BTSTACK_CLASSIC_HID_DEVICE_FIXTURE)
        set(_jh_mode_upstream_sources
            ${_jh_btstack_classic_sources}
            ${_jh_btstack_hid_device_sources})
        set(_jh_mode_definitions
            ENABLE_CLASSIC=1
            JH_BLUETOOTH_CLASSIC_HID_DEVICE_FIXTURE=1)
    else()
        if(JH_BTSTACK_BLE)
            list(APPEND _jh_mode_upstream_sources ${_jh_btstack_ble_sources})
            list(APPEND _jh_mode_jh_sources
                "${_jh_bluetooth_root}/jh_ble_btstack_backend.c")
            set(_jh_gatt_header "jh_ble_peripheral_gatt.h")
            list(APPEND _jh_mode_definitions
                ENABLE_BLE=1
                ENABLE_LE_CENTRAL=1
                JH_BLUETOOTH_PUBLIC_BLE=1)
            if(JH_BTSTACK_BLE_STREAM)
                list(APPEND _jh_mode_jh_sources
                    "${_jh_bluetooth_root}/jh_ble_stream_session.c")
                set(_jh_gatt_source
                    "${_jh_bluetooth_root}/jh_ble_stream.gatt")
                list(APPEND _jh_mode_definitions
                    JH_BLUETOOTH_BLE_STREAM=1)
            else()
                set(_jh_gatt_source
                    "${_jh_bluetooth_root}/jh_ble_peripheral.gatt")
            endif()
        endif()
        if(JH_BTSTACK_CLASSIC)
            list(APPEND _jh_mode_upstream_sources
                ${_jh_btstack_classic_sources})
            list(APPEND _jh_mode_jh_sources
                ${_jh_jh_public_classic_sources})
            list(APPEND _jh_mode_definitions
                ENABLE_CLASSIC=1
                ENABLE_SDP_EXTRA_QUERIES=1
                JH_BLUETOOTH_PUBLIC_CLASSIC=1)
        endif()
        if(JH_BTSTACK_HID_HOST)
            list(APPEND _jh_mode_upstream_sources
                ${_jh_btstack_hid_host_sources})
            if(NOT JH_BTSTACK_A2DP_SINK)
                list(APPEND _jh_mode_jh_sources
                    "${_jh_bluetooth_root}/jh_bluetooth_classic_hid_memory_probe.c")
            endif()
            list(APPEND _jh_mode_definitions
                JH_BLUETOOTH_PUBLIC_HID_HOST=1)
        endif()
        if(JH_BTSTACK_A2DP_SINK)
            list(APPEND _jh_mode_upstream_sources
                ${_jh_btstack_a2dp_sink_sources})
            list(APPEND _jh_mode_jh_sources
                "${_jh_bluetooth_root}/jh_bluetooth_a2dp_memory_probe.c")
            list(APPEND _jh_mode_definitions
                JH_BLUETOOTH_PUBLIC_A2DP_SINK=1)
        endif()
        if(JH_BTSTACK_AVRCP_TARGET)
            list(APPEND _jh_mode_upstream_sources
                ${_jh_btstack_avrcp_target_sources})
            list(APPEND _jh_mode_definitions
                JH_BLUETOOTH_PUBLIC_AVRCP_TARGET=1)
        endif()
    endif()
    set(_jh_btstack_upstream_sources
        ${_jh_btstack_base_sources}
        ${_jh_mode_upstream_sources})
    set(_jh_btstack_sources
        ${_jh_btstack_upstream_sources}
        ${_jh_jh_base_sources}
        ${_jh_mode_jh_sources})

    foreach(_jh_source IN LISTS _jh_btstack_sources)
        if(NOT EXISTS "${_jh_source}")
            message(FATAL_ERROR "Pinned BTstack source is missing: ${_jh_source}")
        endif()
    endforeach()

    file(MAKE_DIRECTORY "${_jh_generated_dir}")
    if(_jh_gatt_source)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
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
    endif()

    target_sources(${TARGET_NAME} PRIVATE ${_jh_btstack_sources})
    target_include_directories(${TARGET_NAME} BEFORE PRIVATE
        "${_jh_generated_dir}"
        "${_jh_bluetooth_root}"
        "${_jh_btstack_root}"
        "${_jh_btstack_root}/src"
        "${_jh_btstack_root}/platform/embedded"
        "${_jh_btstack_root}/3rd-party/bluedroid/decoder/include"
        "${_jh_btstack_root}/3rd-party/bluedroid/encoder/include"
        "${_jh_btstack_root}/3rd-party/micro-ecc"
        "${_jh_btstack_root}/3rd-party/rijndael")
    target_compile_definitions(${TARGET_NAME} PRIVATE
        HAVE_BTSTACK_CONFIG_H=1
        JH_BLUETOOTH_BTSTACK=1
        ${_jh_mode_definitions})
    if(JH_BTSTACK_CLASSIC_HID)
        # The BTstack sources are compiled into the static HAL target, while
        # GNU --wrap is evaluated only by the final firmware link. Propagate
        # these options to that link so the probe sees real pool activity.
        target_link_options(${TARGET_NAME} PUBLIC
            "-Wl,--wrap=btstack_memory_hci_connection_get"
            "-Wl,--wrap=btstack_memory_hci_connection_free"
            "-Wl,--wrap=btstack_memory_l2cap_service_get"
            "-Wl,--wrap=btstack_memory_l2cap_service_free"
            "-Wl,--wrap=btstack_memory_l2cap_channel_get"
            "-Wl,--wrap=btstack_memory_l2cap_channel_free"
            "-Wl,--wrap=btstack_memory_btstack_link_key_db_memory_entry_get"
            "-Wl,--wrap=btstack_memory_btstack_link_key_db_memory_entry_free"
            "-Wl,--wrap=btstack_memory_hid_host_connection_get"
            "-Wl,--wrap=btstack_memory_hid_host_connection_free")
    endif()
    if(JH_BTSTACK_HID_HOST AND NOT JH_BTSTACK_A2DP_SINK)
        target_link_options(${TARGET_NAME} PUBLIC
            "-Wl,--wrap=btstack_memory_hci_connection_get"
            "-Wl,--wrap=btstack_memory_hci_connection_free"
            "-Wl,--wrap=btstack_memory_l2cap_service_get"
            "-Wl,--wrap=btstack_memory_l2cap_service_free"
            "-Wl,--wrap=btstack_memory_l2cap_channel_get"
            "-Wl,--wrap=btstack_memory_l2cap_channel_free"
            "-Wl,--wrap=btstack_memory_btstack_link_key_db_memory_entry_get"
            "-Wl,--wrap=btstack_memory_btstack_link_key_db_memory_entry_free"
            "-Wl,--wrap=btstack_memory_hid_host_connection_get"
            "-Wl,--wrap=btstack_memory_hid_host_connection_free")
    endif()
    if(JH_BTSTACK_A2DP_SINK)
        target_link_options(${TARGET_NAME} PUBLIC
            "-Wl,--wrap=btstack_memory_hci_connection_get"
            "-Wl,--wrap=btstack_memory_hci_connection_free"
            "-Wl,--wrap=btstack_memory_l2cap_service_get"
            "-Wl,--wrap=btstack_memory_l2cap_service_free"
            "-Wl,--wrap=btstack_memory_l2cap_channel_get"
            "-Wl,--wrap=btstack_memory_l2cap_channel_free"
            "-Wl,--wrap=btstack_memory_btstack_link_key_db_memory_entry_get"
            "-Wl,--wrap=btstack_memory_btstack_link_key_db_memory_entry_free"
            "-Wl,--wrap=btstack_memory_service_record_item_get"
            "-Wl,--wrap=btstack_memory_service_record_item_free"
            "-Wl,--wrap=btstack_memory_avdtp_stream_endpoint_get"
            "-Wl,--wrap=btstack_memory_avdtp_stream_endpoint_free"
            "-Wl,--wrap=btstack_memory_avdtp_connection_get"
            "-Wl,--wrap=btstack_memory_avdtp_connection_free"
            "-Wl,--wrap=btstack_memory_avrcp_connection_get"
            "-Wl,--wrap=btstack_memory_avrcp_connection_free")
    endif()
    # Keep compatibility suppressions scoped to pinned upstream code.  The JH
    # port and probe remain subject to the target's complete warning policy.
    set_source_files_properties(${_jh_btstack_upstream_sources} PROPERTIES
        COMPILE_OPTIONS
            "-Wno-unused-parameter;-Wno-unused-function;-Wno-unused-variable;-Wno-sign-compare;-Wno-missing-field-initializers")
endfunction()

function(jh_target_enable_btstack_stage1 TARGET_NAME)
    _jh_target_enable_btstack(${TARGET_NAME} STAGE1)
endfunction()

function(jh_target_enable_btstack_classic_hid TARGET_NAME)
    _jh_target_enable_btstack(${TARGET_NAME} CLASSIC_HID)
endfunction()

function(jh_target_enable_btstack_classic_hid_device_fixture TARGET_NAME)
    _jh_target_enable_btstack(${TARGET_NAME} CLASSIC_HID_DEVICE_FIXTURE)
endfunction()

function(jh_target_enable_btstack_profiles TARGET_NAME)
    _jh_target_enable_btstack(${TARGET_NAME} ${ARGN})
endfunction()
