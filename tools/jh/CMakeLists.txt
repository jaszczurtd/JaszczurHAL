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
jh_add_jhbase()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/jh_pcre2.cmake")
jh_add_pcre2()
get_target_property(_jh_regex_source pcre2-8-static SOURCE_DIR)
add_library(jh_schema STATIC json/jh_json_schema.cpp)
target_include_directories(jh_schema PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/json")
target_link_libraries(jh_schema PUBLIC jb::host PRIVATE pcre2-8-static)
set_target_properties(jh_schema PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
add_executable(jh src/main.cpp)
target_link_libraries(jh PRIVATE jh_schema)
target_compile_features(jh PRIVATE cxx_std_17)
set_target_properties(jh PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_definitions(jh PRIVATE JH_TOOL_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(jh PRIVATE /W4 /WX /utf-8 /EHsc /GR-)
  target_compile_options(jh_schema PRIVATE /W4 /WX /utf-8 /EHsc /GR-)
else()
  target_compile_options(jh PRIVATE -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -fno-rtti)
  target_compile_options(jh_schema PRIVATE -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -fno-rtti)
endif()
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
  set_tests_properties(jh_schema jh_schema_compat PROPERTIES TIMEOUT 120)
  add_test(NAME jh_cli COMMAND "${CMAKE_COMMAND}" "-DJH=$<TARGET_FILE:jh>"
    "-DJH_BUILD=${CMAKE_CURRENT_BINARY_DIR}" "-DJH_CONFIG=$<CONFIG>"
    "-DJH_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
    "-DJH_HAL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/../.."
    "-DJH_GENERATOR=${CMAKE_GENERATOR}" "-DJH_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
    "-DJH_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
    "-DJH_JHBASE=${_jh_cli_jhbase}" "-DJH_PCRE2=${_jh_regex_source}"
    "-DJH_WORK=${CMAKE_CURRENT_BINARY_DIR}/cli-check #% ż" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.cmake")
  set_tests_properties(jh_cli PROPERTIES TIMEOUT 120)
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
