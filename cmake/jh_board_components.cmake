include_guard(GLOBAL)

# Central CMake registry of controlled board/target component IDs.
#
# Mirrors COMPONENT_REGISTRY in scripts/generate_board_config.py; the board
# generator test asserts both registries stay in sync. Each component maps to
# its allowed build provider(s) and an exclusive slot. Source integration is
# owned by the build recipes (cmake/targets/*.cmake, stm32_lib, host build);
# this registry validates the resolved component list and exposes
# JH_BOARD_COMPONENT_<ID> flags for recipes and feature checks.

set(JH_BOARD_COMPONENT_IDS
    rp-native
    stm32g474-native
    host-mock
    cyw43-pico-pio
    cyw43-stm32-gspi
    cyw43-lwip)

set(JH_BOARD_COMPONENT_rp_native_PROVIDERS "pico-sdk")
set(JH_BOARD_COMPONENT_rp_native_SLOT "target-runtime")

set(JH_BOARD_COMPONENT_stm32g474_native_PROVIDERS "jh-stm32-baremetal")
set(JH_BOARD_COMPONENT_stm32g474_native_SLOT "target-runtime")

set(JH_BOARD_COMPONENT_host_mock_PROVIDERS "host")
set(JH_BOARD_COMPONENT_host_mock_SLOT "target-runtime")

set(JH_BOARD_COMPONENT_cyw43_pico_pio_PROVIDERS "pico-sdk")
set(JH_BOARD_COMPONENT_cyw43_pico_pio_SLOT "network-radio-transport")

set(JH_BOARD_COMPONENT_cyw43_stm32_gspi_PROVIDERS "jh-stm32-baremetal")
set(JH_BOARD_COMPONENT_cyw43_stm32_gspi_SLOT "network-radio-transport")

set(JH_BOARD_COMPONENT_cyw43_lwip_PROVIDERS
    "pico-sdk;jh-stm32-baremetal")
set(JH_BOARD_COMPONENT_cyw43_lwip_SLOT "network-stack")

# Validate the generated JH_BOARD_COMPONENTS list against the registry and
# expose one JH_BOARD_COMPONENT_<ID> flag per resolved component.
#
#   jh_apply_board_components(PROVIDER <build-provider>)
#
# Fails the configure step for an unknown component ID, a component that is
# not allowed for the active provider, or two components claiming the same
# exclusive slot.
function(jh_apply_board_components)
    set(options)
    set(one_value_args PROVIDER)
    set(multi_value_args)
    cmake_parse_arguments(JH_COMPONENTS
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT JH_COMPONENTS_PROVIDER)
        message(FATAL_ERROR "jh_apply_board_components requires PROVIDER")
    endif()

    set(_claimed_slots "")
    set(_claimed_owners "")
    foreach(_component IN LISTS JH_BOARD_COMPONENTS)
        list(FIND JH_BOARD_COMPONENT_IDS "${_component}" _component_index)
        if(_component_index EQUAL -1)
            message(FATAL_ERROR
                "Unknown board component '${_component}'; expected one of: "
                "${JH_BOARD_COMPONENT_IDS}")
        endif()
        string(REPLACE "-" "_" _component_key "${_component}")

        set(_providers "${JH_BOARD_COMPONENT_${_component_key}_PROVIDERS}")
        list(FIND _providers "${JH_COMPONENTS_PROVIDER}" _provider_index)
        if(_provider_index EQUAL -1)
            message(FATAL_ERROR
                "Board component '${_component}' is not available for "
                "provider '${JH_COMPONENTS_PROVIDER}' (allowed: ${_providers})")
        endif()

        set(_slot "${JH_BOARD_COMPONENT_${_component_key}_SLOT}")
        list(FIND _claimed_slots "${_slot}" _slot_index)
        if(NOT _slot_index EQUAL -1)
            list(GET _claimed_owners ${_slot_index} _slot_owner)
            message(FATAL_ERROR
                "Board components '${_slot_owner}' and '${_component}' both "
                "claim the exclusive slot '${_slot}'")
        endif()
        list(APPEND _claimed_slots "${_slot}")
        list(APPEND _claimed_owners "${_component}")

        string(TOUPPER "${_component_key}" _component_upper)
        set(JH_BOARD_COMPONENT_${_component_upper} ON PARENT_SCOPE)
    endforeach()
endfunction()
