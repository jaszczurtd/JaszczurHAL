include_guard(GLOBAL)

function(jh_target_enable_stack_protector TARGET_NAME VISIBILITY)
    if(NOT TARGET "${TARGET_NAME}")
        message(FATAL_ERROR
            "jh_target_enable_stack_protector: target '${TARGET_NAME}' does not exist")
    endif()
    if(NOT VISIBILITY MATCHES "^(PRIVATE|PUBLIC|INTERFACE)$")
        message(FATAL_ERROR
            "jh_target_enable_stack_protector(${TARGET_NAME}): visibility must be PRIVATE, PUBLIC, or INTERFACE")
    endif()

    foreach(_jh_language IN ITEMS C CXX)
        if(CMAKE_${_jh_language}_COMPILER_LOADED AND
           NOT CMAKE_${_jh_language}_COMPILER_ID MATCHES
               "^(GNU|Clang|AppleClang)$")
            message(FATAL_ERROR
                "HAL_ENABLE_STACK_PROTECTOR requires GCC or Clang for ${_jh_language}; got '${CMAKE_${_jh_language}_COMPILER_ID}'")
        endif()
    endforeach()

    target_compile_options("${TARGET_NAME}" ${VISIBILITY}
        "$<$<COMPILE_LANGUAGE:C,CXX>:-fstack-protector-strong>")
    target_compile_definitions("${TARGET_NAME}" ${VISIBILITY}
        JH_STACK_PROTECTOR_STRONG_COMPILE_CONTRACT=1)
endfunction()

function(jh_stack_protector_disable_sources)
    foreach(_jh_source IN LISTS ARGN)
        set_property(SOURCE "${_jh_source}" APPEND PROPERTY COMPILE_OPTIONS
            "$<$<COMPILE_LANGUAGE:C,CXX>:-fno-stack-protector>")
    endforeach()
endfunction()
