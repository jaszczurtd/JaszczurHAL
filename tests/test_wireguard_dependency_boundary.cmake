set(_wireguard
    "${JH_ROOT}/src/hal/impl/shared/frameworks/wireguard")
set(_legacy
    "${JH_ROOT}/src/hal/impl/rp2040/frameworks/arduino-wireguard-pico-w")

if(EXISTS "${_legacy}")
    message(FATAL_ERROR
        "Legacy target-owned WireGuard package still exists: ${_legacy}")
endif()
if(NOT EXISTS "${_wireguard}/jh_wireguard_client.cpp"
   OR NOT EXISTS "${_wireguard}/wireguardif.c"
   OR NOT EXISTS "${_wireguard}/crypto/crypto.c")
    message(FATAL_ERROR "Shared WireGuard import is incomplete")
endif()
if(EXISTS "${_wireguard}/library.properties")
    message(FATAL_ERROR
        "Shared WireGuard must not be discovered as an Arduino library")
endif()

file(GLOB_RECURSE _sources
    "${_wireguard}/*.c"
    "${_wireguard}/*.cpp"
    "${_wireguard}/*.h")
foreach(_source IN LISTS _sources)
    file(READ "${_source}" _contents)
    if(_contents MATCHES
       "#[ \t]*include[ \t]*[<\"](Arduino|WiFi|IPAddress|ClientContext|pico/|hardware/)")
        message(FATAL_ERROR
            "Target/Arduino dependency leaked into shared WireGuard: ${_source}")
    endif()
endforeach()
