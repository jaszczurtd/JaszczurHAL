cmake_minimum_required(VERSION 3.22)
# The bootstrap builds jh from an exported tree with the managed checkouts of
# another checkout, reuses an unchanged result, rebuilds a damaged or
# interrupted one, keys every input and refuses a pin without its checkout.
# Arguments: JH_HAL_SOURCE (this checkout), JH_WORK (scratch directory).
if(NOT IS_DIRECTORY "${JH_HAL_SOURCE}" OR JH_WORK STREQUAL "")
  message(FATAL_ERROR "JH_HAL_SOURCE and JH_WORK are required")
endif()
get_filename_component(JH_HAL_SOURCE "${JH_HAL_SOURCE}" ABSOLUTE)
get_filename_component(JH_WORK "${JH_WORK}" ABSOLUTE)
file(REMOVE_RECURSE "${JH_WORK}")
set(copy "${JH_WORK}/exported tree ż")
set(output "${JH_WORK}/host-tools")
foreach(path tools/jh cmake/jh_jhbase.cmake cmake/jh_pcre2.cmake cmake/jh_verify_pcre2.cmake
    cmake/jh_build_dir_guard.cmake third_party/jhbase_version.conf third_party/pcre2_version.conf
    config/tooling/managed_components.json scripts/jh_bootstrap.cmake scripts/jh scripts/jh.ps1 LICENSE)
  get_filename_component(parent "${copy}/${path}" DIRECTORY)
  file(MAKE_DIRECTORY "${parent}")
  if(IS_DIRECTORY "${JH_HAL_SOURCE}/${path}")
    file(COPY "${JH_HAL_SOURCE}/${path}" DESTINATION "${parent}")
  else()
    file(COPY "${JH_HAL_SOURCE}/${path}" DESTINATION "${parent}" USE_SOURCE_PERMISSIONS)
  endif()
endforeach()
file(REMOVE_RECURSE "${copy}/tools/jh/.build")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(exe_name jh.exe)
else()
  set(exe_name jh)
endif()

# bootstrap(<expected exit> <result variable prefix> [extra -D arguments...]):
# runs the bootstrap on the copy with this checkout's managed checkouts.
function(bootstrap expected prefix)
  set(result "${JH_WORK}/${prefix}-result.txt")
  file(REMOVE "${result}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "JH_DEPENDENCY_ROOT=${JH_HAL_SOURCE}" --unset=JH_JHBASE_DIR
      "${CMAKE_COMMAND}" "-DJH_ROOT=${copy}" "-DJH_OUTPUT_ROOT=${output}" "-DJH_RESULT_FILE=${result}" ${ARGN}
      -P "${copy}/scripts/jh_bootstrap.cmake"
    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT code STREQUAL "${expected}")
    message(FATAL_ERROR "bootstrap ${prefix}: expected exit ${expected}, got ${code}\n${out}\n${err}")
  endif()
  set(${prefix}_output "${out}${err}" PARENT_SCOPE)
  if(EXISTS "${result}")
    file(STRINGS "${result}" lines LIMIT_COUNT 1)
    set(${prefix}_exe "${lines}" PARENT_SCOPE)
  else()
    set(${prefix}_exe "" PARENT_SCOPE)
  endif()
endfunction()

function(expect_runs exe)
  execute_process(COMMAND "${exe}" --version --json RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT code EQUAL 0 OR NOT out MATCHES "\"status\":\"HAL_OK\"")
    message(FATAL_ERROR "${exe} does not run: ${code} ${out} ${err}")
  endif()
endfunction()

# 1. Empty cache: the tree without checkouts builds with the dependency root.
bootstrap(0 first)
if(NOT first_exe MATCHES "^${output}/[a-z0-9-]+-[^/]+/[0-9a-f]+/bin/${exe_name}$")
  message(FATAL_ERROR "Unexpected executable location: ${first_exe}")
endif()
expect_runs("${first_exe}")
get_filename_component(first_bin "${first_exe}" DIRECTORY)
get_filename_component(first_key_dir "${first_bin}" DIRECTORY)
get_filename_component(variant_dir "${first_key_dir}" DIRECTORY)
file(READ "${first_bin}/manifest.json" manifest)
string(JSON recorded_root GET "${manifest}" root)
string(JSON recorded_dependency_root GET "${manifest}" dependencyRoot)
string(JSON recorded_inputs LENGTH "${manifest}" inputs)
string(JSON recorded_pointer GET "${manifest}" pointerSize)
if(NOT recorded_root STREQUAL copy OR NOT recorded_dependency_root STREQUAL JH_HAL_SOURCE
    OR recorded_inputs LESS 10 OR NOT recorded_pointer MATCHES "^(4|8)$")
  message(FATAL_ERROR "The manifest does not record the build: ${manifest}")
endif()
if(EXISTS "${first_key_dir}/work.inprogress")
  message(FATAL_ERROR "The in-progress marker survived a successful build")
endif()
file(SHA256 "${first_exe}" first_sha)

# 2. Unchanged inputs reuse the published executable without touching it.
file(TIMESTAMP "${first_exe}" before "%s")
file(READ "${first_bin}/manifest.json" manifest_before)
bootstrap(0 second)
file(TIMESTAMP "${first_exe}" after "%s")
file(READ "${first_bin}/manifest.json" manifest_after)
if(NOT second_exe STREQUAL first_exe OR NOT before STREQUAL after OR NOT manifest_before STREQUAL manifest_after
    OR second_output MATCHES "Building jh")
  message(FATAL_ERROR "A reusable executable was rebuilt or replaced: ${second_output}")
endif()

# 3. A damaged executable and an interrupted work tree are rebuilt cleanly.
file(APPEND "${first_exe}" "x")
file(WRITE "${first_key_dir}/work.inprogress" "stale\n")
file(WRITE "${first_key_dir}/work/junk.txt" "stale\n")
bootstrap(0 third)
file(SHA256 "${first_exe}" third_sha)
if(NOT third_exe STREQUAL first_exe OR NOT third_sha STREQUAL first_sha OR EXISTS "${first_key_dir}/work/junk.txt"
    OR NOT third_output MATCHES "Building jh")
  message(FATAL_ERROR "The damaged executable was not rebuilt from a clean tree: ${third_output}")
endif()
expect_runs("${third_exe}")

# 4. A changed source is a new key; the earlier result stays untouched.
file(APPEND "${copy}/tools/jh/src/main.cpp" "\n// bootstrap check: changed input\n")
bootstrap(0 fourth)
get_filename_component(fourth_bin "${fourth_exe}" DIRECTORY)
get_filename_component(fourth_key_dir "${fourth_bin}" DIRECTORY)
file(SHA256 "${first_exe}" first_sha_again)
if(fourth_key_dir STREQUAL first_key_dir OR NOT first_sha_again STREQUAL first_sha)
  message(FATAL_ERROR "A changed input did not produce a new key, or replaced the old result")
endif()
expect_runs("${fourth_exe}")

# 5. A pin whose checkout is missing or different is refused before any build.
file(READ "${copy}/third_party/pcre2_version.conf" pin)
string(REGEX REPLACE "PCRE2_REF=[0-9a-f]+" "PCRE2_REF=0123456789abcdef0123456789abcdef01234567" wrong_pin "${pin}")
file(WRITE "${copy}/third_party/pcre2_version.conf" "${wrong_pin}")
bootstrap(1 wrong_pin)
if(NOT wrong_pin_output MATCHES "Wrong revision" OR NOT wrong_pin_exe STREQUAL "")
  message(FATAL_ERROR "A wrong PCRE2 pin was not refused: ${wrong_pin_output}")
endif()
file(WRITE "${copy}/third_party/pcre2_version.conf" "${pin}")
file(MAKE_DIRECTORY "${JH_WORK}/empty dependency root")
set(ENV{JH_DEPENDENCY_ROOT} "${JH_WORK}/empty dependency root")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DJH_ROOT=${copy}" "-DJH_OUTPUT_ROOT=${output}"
    -P "${copy}/scripts/jh_bootstrap.cmake"
  RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
unset(ENV{JH_DEPENDENCY_ROOT})
if(code EQUAL 0 OR NOT "${out}${err}" MATCHES "jhbase is missing")
  message(FATAL_ERROR "Missing managed checkouts were not reported: ${out}${err}")
endif()

# 6. The launcher of this host runs the tool through the same bootstrap.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  find_program(powershell NAMES pwsh powershell REQUIRED)
  set(launcher "${powershell}" -NoProfile -ExecutionPolicy Bypass -File "${copy}/scripts/jh.ps1")
else()
  set(launcher "${copy}/scripts/jh")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "JH_DEPENDENCY_ROOT=${JH_HAL_SOURCE}" --unset=JH_JHBASE_DIR
    ${launcher} --version --json
  RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 0 OR NOT out MATCHES "^{\"formatVersion\":1,\"command\":\"version\",\"status\":\"HAL_OK\"")
  message(FATAL_ERROR "The launcher did not run jh with a clean stdout: ${code}\n${out}\n${err}")
endif()
# Command words and options reach jh unchanged; the exported tree has no
# type catalogue, so the generator reports the missing directory itself.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "JH_DEPENDENCY_ROOT=${JH_HAL_SOURCE}" --unset=JH_JHBASE_DIR
    ${launcher} generate device-defaults --root "${copy}" --check --json
  RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 3 OR NOT out MATCHES "\"command\":\"generate.device-defaults\",\"status\":\"HAL_ENOENT\"")
  message(FATAL_ERROR "The launcher changed the command words: ${code}\n${out}\n${err}")
endif()
message(STATUS "jh bootstrap: PASS")
