cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/jh_build_dir_guard.cmake")
jh_require_managed_build_dir()
project(jh VERSION 0.1.0 LANGUAGES C CXX)
if(CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "jh requires a native host compiler; configure it separately from firmware")
endif()
# The same jhbase as every HAL build: the pinned third_party/jhbase checkout,
# or the one JH_JHBASE_DIR names.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/jh_jhbase.cmake")
jh_jhbase_dir(_jh_cli_jhbase)
if(NOT EXISTS "${_jh_cli_jhbase}/include/jb/file.h")
  message(FATAL_ERROR "jhbase at ${_jh_cli_jhbase} has no host API (jb/file.h)")
endif()
set(JHBASE_HOST ON CACHE BOOL "Build host file operations" FORCE)
# The type catalogue checks units as printable NFC text through jb::unicode;
# jh_add_jhbase() selects the utf8proc checkout of the component registry.
set(JHBASE_UNICODE ON CACHE BOOL "Build Unicode normalization" FORCE)
jh_add_jhbase()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/jh_pcre2.cmake")
jh_add_pcre2()
get_target_property(_jh_regex_source pcre2-8-static SOURCE_DIR)
function(jh_tool_target target)
  set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /EHsc /GR-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -fno-rtti)
  endif()
endfunction()
add_library(jh_schema STATIC json/jh_json_schema.cpp)
target_include_directories(jh_schema PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/json")
target_link_libraries(jh_schema PUBLIC jb::host PRIVATE pcre2-8-static)
jh_tool_target(jh_schema)
# Host modules of the commands: JSON files, the type catalogue, the device
# defaults generator and generated-artifact publishing.
add_library(jh_host STATIC json/jh_json_file.cpp hardware/jh_type_catalogue.cpp
  hardware/jh_device_defaults.cpp src/jh_generated_output.cpp)
target_include_directories(jh_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/json"
  "${CMAKE_CURRENT_SOURCE_DIR}/hardware" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(jh_host PUBLIC jh_schema jb::host jb::unicode)
jh_tool_target(jh_host)
add_executable(jh src/main.cpp)
target_link_libraries(jh PRIVATE jh_host)
target_compile_features(jh PRIVATE cxx_std_17)
jh_tool_target(jh)
target_compile_definitions(jh PRIVATE JH_TOOL_VERSION="${PROJECT_VERSION}")
include(CTest)
if(BUILD_TESTING)
  add_executable(jh_schema_test tests/test_schema.cpp tests/schema_callback.c)
  target_compile_features(jh_schema_test PRIVATE c_std_11)
  target_link_libraries(jh_schema_test PRIVATE jh_schema)
  set_target_properties(jh_schema_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  add_test(NAME jh_schema COMMAND jh_schema_test)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME jh_schema_compat COMMAND "${CMAKE_COMMAND}" -E env
      "JH_SCHEMA_TOOL=$<TARGET_FILE:jh>" "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_hardware_data.py")
  # The Python generator's regressions also drive the migrated generator.
  add_test(NAME jh_defaults_compat COMMAND "${CMAKE_COMMAND}" -E env
      "JH_SCHEMA_TOOL=$<TARGET_FILE:jh>" "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_device_defaults.py")
  set_tests_properties(jh_schema jh_schema_compat jh_defaults_compat PROPERTIES TIMEOUT 120)
  add_test(NAME jh_cli COMMAND "${CMAKE_COMMAND}" "-DJH=$<TARGET_FILE:jh>"
    "-DJH_BUILD=${CMAKE_CURRENT_BINARY_DIR}" "-DJH_CONFIG=$<CONFIG>"
    "-DJH_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
    "-DJH_HAL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../.."
    "-DJH_GENERATOR=${CMAKE_GENERATOR}" "-DJH_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
    "-DJH_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
    "-DJH_JHBASE=${_jh_cli_jhbase}" "-DJH_PCRE2=${_jh_regex_source}"
    "-DJH_WORK=${CMAKE_CURRENT_BINARY_DIR}/cli-check #% ż" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.cmake")
  set_tests_properties(jh_cli PROPERTIES TIMEOUT 120)
  # The launcher bootstrap: builds this tool from an exported tree with the
  # checkouts of this checkout, so it runs for several minutes.
  add_test(NAME jh_bootstrap COMMAND "${CMAKE_COMMAND}"
    "-DJH_HAL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../.."
    "-DJH_WORK=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-check"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_bootstrap.cmake")
  set_tests_properties(jh_bootstrap PROPERTIES TIMEOUT 1800)
endif()
install(TARGETS jh RUNTIME DESTINATION bin COMPONENT jh)
install(FILES "${_jh_regex_source}/LICENCE.md"
  DESTINATION share/jh/licenses/pcre2 COMPONENT jh)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE"
  DESTINATION share/jh/licenses/JaszczurHAL COMPONENT jh)
install(FILES "${_jh_cli_jhbase}/LICENSE"
  DESTINATION share/jh/licenses/jhbase COMPONENT jh)
install(FILES "${_jh_cli_jhbase}/third_party/ryu/LICENSE-Apache2"
  "${_jh_cli_jhbase}/third_party/ryu/LICENSE-Boost"
  DESTINATION share/jh/licenses/ryu COMPONENT jh)
install(FILES "${JHBASE_UTF8PROC_DIR}/LICENSE.md"
  DESTINATION share/jh/licenses/utf8proc COMPONENT jh)
