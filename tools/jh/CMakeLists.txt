cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/jh_build_dir_guard.cmake")
jh_require_managed_build_dir()
project(jh VERSION 0.1.0 LANGUAGES C CXX)
if(CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "jh requires a native host compiler; configure it separately from firmware")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/jh_sanitizers.cmake")
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
# Language levels and warnings of every jh target: C11 for its C sources, as
# jhbase builds them, and C++17 without RTTI for its C++ sources.
function(jh_tool_target target)
  set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
      $<$<COMPILE_LANGUAGE:CXX>:/GR->)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow
      $<$<COMPILE_LANGUAGE:C>:-Wvla> $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
      $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
  endif()
endfunction()
# The failure record of the C modules and repr() of Python strings, which
# every command quotes in its diagnostics. C11.
add_library(jh_core STATIC base/jh_failure.c json/jh_python_repr.c)
target_include_directories(jh_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/base"
  "${CMAKE_CURRENT_SOURCE_DIR}/json")
target_link_libraries(jh_core PUBLIC jb::host PRIVATE jb::unicode)
jh_tool_target(jh_core)
# The schema validator, C++ behind its C API.
add_library(jh_schema STATIC json/jh_json_schema.cpp)
target_include_directories(jh_schema PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/json")
target_link_libraries(jh_schema PUBLIC jb::host PRIVATE jh_core pcre2-8-static jb::unicode)
jh_tool_target(jh_schema)
# JSON files, VS Code settings, schema documents, the validation failures of
# the registry readers and generated-artifact publishing. C11.
add_library(jh_files STATIC json/jh_json_file.c json/jh_json_check.c
  src/jh_generated_output.c)
target_include_directories(jh_files PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(jh_files PUBLIC jh_core jh_schema jb::host)
jh_tool_target(jh_files)
# The SHA-256 core of HAL (hal_sha256_* delegates to it), which needs no HAL
# configuration, and the feature registry generator. C11.
add_library(jh_sha256 STATIC "${CMAKE_CURRENT_SOURCE_DIR}/../../src/hal/security/jh_sha256.c")
target_include_directories(jh_sha256 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
# hal/core/hal_layout_check.h, which the header includes, needs jb/abi.h.
target_link_libraries(jh_sha256 PUBLIC jb::status)
jh_tool_target(jh_sha256)
add_library(jh_registry STATIC config/jh_feature_registry.c config/jh_board_outputs.c
  config/jh_feature_lint.c config/jh_board_registry.c config/jh_clock_registry.c
  config/jh_project_build.c config/jh_config_expression.c
  config/jh_project_config.c)
target_include_directories(jh_registry PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/config")
target_link_libraries(jh_registry PUBLIC jh_files PRIVATE jh_sha256 pcre2-8-static jb::unicode)
jh_tool_target(jh_registry)
# The type catalogue and the device defaults generator. C11.
add_library(jh_host STATIC hardware/jh_type_catalogue.c hardware/jh_catalogue_nodes.c
  hardware/jh_catalogue_check.c hardware/jh_device_defaults.c)
target_include_directories(jh_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/hardware"
  "${CMAKE_CURRENT_SOURCE_DIR}/config")
target_link_libraries(jh_host PUBLIC jh_registry jb::host jb::unicode)
jh_tool_target(jh_host)
add_executable(jh src/main.c src/jh_cli_result.c src/jh_cli_json.c
  src/jh_cli_repository.c src/jh_cli_features.c src/jh_cli_boards.c
  src/jh_cli_config.c)
target_link_libraries(jh PRIVATE jh_host)
target_compile_features(jh PRIVATE c_std_11)
jh_tool_target(jh)
if(MSVC)
  # The same inputs give the same executable: the linker derives the PE and
  # debug directory timestamps from the content instead of the clock, so the
  # bootstrap's SHA-256 of a rebuilt tool stays comparable.
  target_link_options(jh PRIVATE /Brepro)
endif()
target_compile_definitions(jh PRIVATE JH_TOOL_VERSION="${PROJECT_VERSION}")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/jh_memcheck.cmake")
include(CTest)
option(JH_TEST_VALGRIND "Run CLI subprocess suites under Valgrind" OFF)
if(JH_TEST_VALGRIND AND (NOT UNIX OR JH_ENABLE_SANITIZERS OR JH_ENABLE_THREAD_SANITIZER))
  message(FATAL_ERROR "JH_TEST_VALGRIND needs a native Unix build without sanitizers")
endif()
if(BUILD_TESTING)
  set(_jh_test_tool "$<TARGET_FILE:jh>")
  if(UNIX AND (JH_TEST_VALGRIND OR JH_ENABLE_SANITIZERS))
    add_executable(jh_cli_memory_launcher tests/cli_memory_launcher.c)
    target_link_libraries(jh_cli_memory_launcher PRIVATE jb::base)
    jh_tool_target(jh_cli_memory_launcher)
    set(_jh_test_tool "$<TARGET_FILE:jh_cli_memory_launcher>")
  endif()
  add_executable(jh_schema_test tests/test_schema.cpp tests/schema_callback.c)
  target_compile_features(jh_schema_test PRIVATE c_std_11)
  target_link_libraries(jh_schema_test PRIVATE jh_schema)
  set_target_properties(jh_schema_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  add_test(NAME jh_schema COMMAND jh_schema_test)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME jh_schema_compat COMMAND "${CMAKE_COMMAND}" -E env
      "JH_TEST_PROGRAM=$<TARGET_FILE:jh>" "JH_SCHEMA_TOOL=${_jh_test_tool}" "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_hardware_data.py")
  # The regressions of the device defaults generator run this tool; the
  # host build of tests/ has no jh, so they live here.
  add_test(NAME jh_device_defaults COMMAND "${CMAKE_COMMAND}" -E env
      "JH_TEST_PROGRAM=$<TARGET_FILE:jh>" "JH_SCHEMA_TOOL=${_jh_test_tool}" "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_device_defaults.py")
  # VS Code files and manifests: jh_settings checks the reader itself,
  # jh_settings_compat compares it with load_json_file of the Python tools on
  # the files of this checkout outside tests/hardware and on edge cases.
  add_executable(jh_settings_test tests/test_settings.c)
  target_link_libraries(jh_settings_test PRIVATE jh_host)
  jh_tool_target(jh_settings_test)
  add_test(NAME jh_settings COMMAND jh_settings_test "${CMAKE_CURRENT_BINARY_DIR}")
  add_executable(jh_settings_probe tests/settings_probe.c)
  target_link_libraries(jh_settings_probe PRIVATE jh_host)
  jh_tool_target(jh_settings_probe)
  add_test(NAME jh_settings_compat COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/settings_compat.py"
      "$<TARGET_FILE:jh_settings_probe>" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
  # The hal_project_config.h reader: jh_config checks it in-process;
  # jh_config_preprocessor also compares it with the preprocessor of a GCC or
  # Clang compiler on fixed headers and on every hal_project_config.h of this
  # checkout outside tests/hardware (MSVC has no -E -dM).
  add_executable(jh_config_test tests/test_project_config.c)
  target_link_libraries(jh_config_test PRIVATE jh_registry)
  jh_tool_target(jh_config_test)
  add_test(NAME jh_config COMMAND jh_config_test "${CMAKE_CURRENT_BINARY_DIR}")
  # The C modules' diagnostics, aliasing, paths, representations and diffs.
  add_executable(jh_c_layer_test tests/c_layer_consumer.c)
  target_link_libraries(jh_c_layer_test PRIVATE jh_host)
  jh_tool_target(jh_c_layer_test)
  add_test(NAME jh_c_layer COMMAND jh_c_layer_test "${CMAKE_CURRENT_BINARY_DIR}")
  # The clock tree registry module on edited copies of the registry and on
  # the clock source cases of tests/fixtures/device_tree.
  get_filename_component(_jh_hal_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
  add_executable(jh_clocks_test tests/test_clocks.c)
  target_link_libraries(jh_clocks_test PRIVATE jh_registry)
  jh_tool_target(jh_clocks_test)
  add_test(NAME jh_clocks COMMAND jh_clocks_test "${_jh_hal_root}"
    "${CMAKE_CURRENT_BINARY_DIR}")
  # Exercise errors that real allocations and metadata reads rarely produce.
  # Only the test objects redirect calls; the production libraries are shared.
  get_target_property(_jh_failure_sources jh SOURCES)
  list(REMOVE_ITEM _jh_failure_sources src/main.c)
  add_library(jh_failure_objects OBJECT ${_jh_failure_sources} json/jh_json_file.c)
  target_link_libraries(jh_failure_objects PRIVATE jh_host)
  target_compile_definitions(jh_failure_objects PRIVATE
    jb_fs_status=jh_test_fs_status jb_fs_mkdir=jh_test_fs_mkdir
    jh_project_config_read=jh_test_config_read)
  jh_tool_target(jh_failure_objects)
  add_executable(jh_cli_failures_test tests/test_cli_failures.c
    $<TARGET_OBJECTS:jh_failure_objects>)
  target_link_libraries(jh_cli_failures_test PRIVATE jh_host)
  target_compile_definitions(jh_cli_failures_test PRIVATE JH_TOOL_VERSION="${PROJECT_VERSION}")
  jh_tool_target(jh_cli_failures_test)
  add_test(NAME jh_cli_failures COMMAND jh_cli_failures_test "${_jh_hal_root}"
    "${CMAKE_CURRENT_BINARY_DIR}")
  # The native test programs run under the memory ceiling of the host tests.
  include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/jh_test_memory_limit.cmake")
  jh_limit_test_memory(jh_schema_test jh_settings_test jh_settings_probe
    jh_config_test jh_c_layer_test jh_clocks_test jh_cli_failures_test)
  set_tests_properties(jh_schema jh_schema_compat jh_device_defaults jh_settings
    jh_settings_compat jh_config jh_c_layer jh_cli_failures PROPERTIES TIMEOUT 120)
  # jh_clocks under Valgrind in the gate took 117.7 s of the former 120 s.
  set_tests_properties(jh_clocks PROPERTIES TIMEOUT 300)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_test(NAME jh_config_preprocessor COMMAND jh_config_test
      "${CMAKE_CURRENT_BINARY_DIR}/preprocessor" "${CMAKE_C_COMPILER}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    set_tests_properties(jh_config_preprocessor PROPERTIES TIMEOUT 300)
  endif()
  # The native test programs; the gate runs them under Valgrind (memcheck)
  # and the sanitizers (sanitizer-fuzz).
  set_tests_properties(jh_schema jh_settings jh_config jh_c_layer jh_clocks jh_cli_failures
    PROPERTIES LABELS memcheck)
  add_test(NAME jh_cli COMMAND "${CMAKE_COMMAND}" "-DJH=$<TARGET_FILE:jh>"
    "-DJH_BUILD=${CMAKE_CURRENT_BINARY_DIR}" "-DJH_CONFIG=$<CONFIG>"
    "-DJH_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
    "-DJH_HAL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../.."
    "-DJH_GENERATOR=${CMAKE_GENERATOR}" "-DJH_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
    "-DJH_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
    "-DJH_JHBASE=${_jh_cli_jhbase}" "-DJH_PCRE2=${_jh_regex_source}"
    "-DJH_WORK=${CMAKE_CURRENT_BINARY_DIR}/cli-check #% ż" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.cmake")
  set_tests_properties(jh_cli PROPERTIES TIMEOUT 120)
  # The feature registry commands on edited copies of the HAL configuration.
  add_test(NAME jh_features COMMAND "${CMAKE_COMMAND}" "-DJH=$<TARGET_FILE:jh>"
    "-DJH_HAL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../.."
    "-DJH_WORK=${CMAKE_CURRENT_BINARY_DIR}/features-check ż"
    "-DJH_CC=${CMAKE_C_COMPILER}" "-DJH_CC_ID=${CMAKE_C_COMPILER_ID}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_features.cmake")
  set_tests_properties(jh_features PROPERTIES TIMEOUT 300)
  # The board commands on edited copies of boards/, and the generated board
  # configurations with a compiler: GCC or Clang here, the recorded gnu-arm
  # toolchain on Windows.
  add_test(NAME jh_boards COMMAND "${CMAKE_COMMAND}" "-DJH=$<TARGET_FILE:jh>"
    "-DJH_HAL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../.."
    "-DJH_WORK=${CMAKE_CURRENT_BINARY_DIR}/boards check"
    "-DJH_CC=${CMAKE_C_COMPILER}" "-DJH_CC_ID=${CMAKE_C_COMPILER_ID}" "-DJH_AR=${CMAKE_AR}"
    "-DJH_JHBASE=${_jh_cli_jhbase}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boards.cmake")
  set_tests_properties(jh_boards PROPERTIES TIMEOUT 300)
  # The launcher bootstrap: builds this tool from an exported tree with the
  # checkouts of this checkout, so it runs for several minutes.
  add_test(NAME jh_bootstrap COMMAND "${CMAKE_COMMAND}"
    "-DJH_HAL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../.."
    "-DJH_WORK=${CMAKE_CURRENT_BINARY_DIR}/bootstrap-check"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_bootstrap.cmake")
  set_tests_properties(jh_bootstrap PROPERTIES TIMEOUT 1800)
  set_tests_properties(jh_cli jh_features jh_boards jh_schema_compat jh_device_defaults
    PROPERTIES LABELS cli)
  if(TARGET jh_cli_memory_launcher)
    jh_limit_test_memory(jh_cli_memory_launcher)
    set(_jh_valgrind "")
    if(JH_TEST_VALGRIND)
      find_program(_jh_valgrind_program NAMES valgrind REQUIRED)
      set(_jh_valgrind "${_jh_valgrind_program}")
      # Valgrind repeats startup for each CLI invocation. The feature suite
      # took 1167 s locally; leave room for slower CI hosts.
      set_tests_properties(jh_cli jh_features jh_boards jh_schema_compat jh_device_defaults
        PROPERTIES TIMEOUT 3600)
    endif()
    add_test(NAME jh_cli_memory COMMAND "${CMAKE_COMMAND}"
      "-DJH_BUILD=${CMAKE_CURRENT_BINARY_DIR}" "-DJH_CONFIG=$<CONFIG>"
      "-DJH_WRAPPER=$<TARGET_FILE:jh_cli_memory_launcher>"
      "-DJH_CTEST=${CMAKE_CTEST_COMMAND}" "-DJH_VALGRIND=${_jh_valgrind}"
      "-DJH_REPORTS=${CMAKE_CURRENT_BINARY_DIR}/cli-memory-reports"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli_memory.cmake")
    set_tests_properties(jh_cli_memory PROPERTIES LABELS cli-memory TIMEOUT 7500 RUN_SERIAL TRUE)
  endif()
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
