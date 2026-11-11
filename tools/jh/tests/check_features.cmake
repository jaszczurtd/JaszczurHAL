cmake_minimum_required(VERSION 3.22)
# The feature registry commands of jh on edited copies of the HAL
# configuration: what generation depends on, the registries it rejects, the
# rules requires and conflicts add to the artifacts, resolution and the raw
# lint of project headers and manifests. The facts of the tracked registry are
# checked by tests/test_feature_registry.cmake. At the end, the pin-set bound,
# the unique capability bits and the USB identities of the board registry on
# edited copies of boards/. Last, the clock commands: their arguments, a rejected registry and
# the listing the hardware resolver reads; the module itself is checked by
# jh_clocks and the checkout by tests/test_clock_registry.cmake.
if(NOT EXISTS "${JH}" OR NOT IS_DIRECTORY "${JH_HAL_SOURCE}/config/features")
  message(FATAL_ERROR "jh_features needs JH and JH_HAL_SOURCE")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/cli_helpers.cmake")
file(REMOVE_RECURSE "${JH_WORK}")
file(MAKE_DIRECTORY "${JH_WORK}/cwd")

# The two build cases formerly checked by tests/test_project_config.py.
# SDK values remain unresolved; the compiler-independent value is an integer.
set(project_probe "${JH_WORK}/project-build")
file(MAKE_DIRECTORY "${project_probe}")
file(WRITE "${project_probe}/hal_project_config.h"
  "#define FLASH_BYTES PICO_FLASH_SIZE_BYTES\n"
  "#define HAL_RP_FLASH_EEPROM_SIZE (FLASH_BYTES / 256)\n"
  "#define HAL_RP_CORE1_STACK_SIZE (2u * 1024u)\n")
invoke(0 config cmake --root "${JH_HAL_SOURCE}" --target rp2040
  --config-dir "${project_probe}" --output "${project_probe}/generated.cmake")
file(READ "${project_probe}/generated.cmake" project_values)
contains("${project_values}"
  "set(JH_PROJECT_UNRESOLVED_HAL_RP_FLASH_EEPROM_SIZE [=[PICO_FLASH_SIZE_BYTES]=])"
  "SDK name in the CMake output")
lacks("${project_values}" "JH_PROJECT_INT_HAL_RP_FLASH_EEPROM_SIZE" "Unresolved value")
contains("${project_values}" "set(JH_PROJECT_INT_HAL_RP_CORE1_STACK_SIZE [=[2048]=])"
  "Computed stack size")
file(READ "${project_probe}/generated.cmake" project_bytes HEX)
if(WIN32)
  set(project_newline "0d0a")
else()
  set(project_newline "0a")
endif()
if(NOT project_bytes MATCHES "2e${project_newline}73657428")
  message(FATAL_ERROR "CMake output changed the Python command's native line endings")
endif()
invoke(0 config cmake --root "${JH_HAL_SOURCE}" --target rp2040
  --config-dir "${project_probe}" --output "${project_probe}/generated.cmake" --json)
string(JSON project_changed GET "${stdout}" changed)
if(project_changed)
  message(FATAL_ERROR "An unchanged CMake output was rewritten")
endif()
if(UNIX)
  # New CMake files follow the caller's umask; a later publication must keep
  # the existing mode even when the invoking process uses another umask.
  set(project_mode_file "${project_probe}/permissions.cmake")
  execute_process(COMMAND sh -c "umask 027 && exec \"$0\" \"$@\"" "${JH}"
    config cmake --root "${JH_HAL_SOURCE}" --target rp2040 --output "${project_mode_file}"
    RESULT_VARIABLE project_mode_result)
  if(NOT project_mode_result EQUAL 0)
    message(FATAL_ERROR "Cannot create CMake file with umask 027")
  endif()
  execute_process(COMMAND stat -c %a "${project_mode_file}" OUTPUT_VARIABLE project_mode
    COMMAND_ERROR_IS_FATAL ANY OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT project_mode STREQUAL "640")
    message(FATAL_ERROR "CMake file ignored umask: ${project_mode}")
  endif()
  execute_process(COMMAND sh -c "umask 077 && exec \"$0\" \"$@\"" "${JH}"
    config cmake --root "${JH_HAL_SOURCE}" --target rp2040 --define HAL_ENABLE_CRC
    --output "${project_mode_file}" RESULT_VARIABLE project_mode_result)
  if(NOT project_mode_result EQUAL 0)
    message(FATAL_ERROR "Cannot update CMake file with umask 077")
  endif()
  execute_process(COMMAND stat -c %a "${project_mode_file}" OUTPUT_VARIABLE project_mode
    COMMAND_ERROR_IS_FATAL ANY OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT project_mode STREQUAL "640")
    message(FATAL_ERROR "CMake publication changed the existing mode: ${project_mode}")
  endif()
endif()

# A variant's feature removed by the header must not return in the requests
# or conflict with its replacement when config cmake validates the build.
file(WRITE "${project_probe}/hal_project_config.h"
  "#define JH_PROJECT_VARIANTS(X) X(NEW_RADIO, \"SX126X\", HAL_ENABLE_SX127X, NEW_RADIO=1)\n"
  "#if defined(NEW_RADIO)\n#undef HAL_ENABLE_SX127X\n#define HAL_ENABLE_SX126X\n#endif\n")
invoke(0 config build --root "${JH_HAL_SOURCE}" --target rp2040
  --config-dir "${project_probe}" --variant NEW_RADIO --json)
json_equals("${stdout}" "[\"HAL_ENABLE_SX126X\"]" "Final header feature requests" requestedFeatures)
invoke(0 config cmake --root "${JH_HAL_SOURCE}" --target rp2040
  --config-dir "${project_probe}" --variant NEW_RADIO --output "${project_probe}/generated.cmake")
file(READ "${project_probe}/generated.cmake" project_values)
contains("${project_values}" "set(JH_PROJECT_FEATURES [=[HAL_ENABLE_SX126X]=])"
  "Validated replacement feature")
file(MAKE_DIRECTORY "${project_probe}/inner")
invoke(0 config build --selector HAL_TARGET_RP2040 --target rp2040
  --header "../project-build/inner/../hal_project_config.h" --variant NEW_RADIO --json)
string(JSON project_origin GET "${stdout}" definitionSources 0 source)
contains("${project_origin}" "../project-build/inner/../hal_project_config.h:JH_PROJECT_VARIANTS"
  "The Python caller's header spelling in definition sources")

# Importers omit builds only for #error. A diagnostic that merely contains
# its marker (for example an unknown variant name) must not hide that build.
invoke(2 config build --root "${JH_HAL_SOURCE}" --target rp2040
  --config-dir "${project_probe}" --variant "[JH-CFG-ERROR]" --json)
string(JSON project_rejected GET "${stdout}" configurationRejected)
if(project_rejected)
  message(FATAL_ERROR "A variant error was mistaken for a header rejection")
endif()
file(WRITE "${project_probe}/hal_project_config.h" "#error unavailable on this target\n")
invoke(2 config build --root "${JH_HAL_SOURCE}" --target rp2040
  --config-dir "${project_probe}" --json)
string(JSON project_rejected GET "${stdout}" configurationRejected)
if(NOT project_rejected)
  message(FATAL_ERROR "A header #error was not reported as a rejection")
endif()

# A registry permits target aliases using the same selector. The Python
# selector-to-ID dictionary retains the last ID, rather than emitting both.
file(COPY "${JH_HAL_SOURCE}/boards" "${JH_HAL_SOURCE}/config"
  DESTINATION "${project_probe}/alias-registry")
set(project_alias "${project_probe}/alias-registry/boards/targets/mock.json")
file(READ "${project_alias}" project_alias_text)
string(JSON project_alias_text SET "${project_alias_text}" hal targetSelector "\"HAL_TARGET_RP2040\"")
file(WRITE "${project_alias}" "${project_alias_text}")
file(WRITE "${project_probe}/hal_project_config.h"
  "#define JH_PROJECT_TARGETS(X) X(HAL_TARGET_RP2040)\n")
invoke(0 config cmake --root "${project_probe}/alias-registry" --target rp2040
  --config-dir "${project_probe}" --output "${project_probe}/alias.cmake")
file(READ "${project_probe}/alias.cmake" project_values)
contains("${project_values}" "set(JH_PROJECT_TARGETS [=[rp2040]=])" "Selector-to-ID mapping")

file(READ "${JH_HAL_SOURCE}/config/tooling/artifacts.json" artifacts)
string(JSON features_header GET "${artifacts}" generatedArtifacts featureHeader)
string(JSON features_cmake GET "${artifacts}" generatedArtifacts featureCmake)

# A copy of the registry configuration (schema and fragments) at JH_WORK/name.
function(copy_config name)
  file(COPY "${JH_HAL_SOURCE}/config/features" "${JH_HAL_SOURCE}/config/features.schema.json"
    DESTINATION "${JH_WORK}/${name}")
endfunction()

# Replace the member at the JSON path ARGN of a fragment with value.
function(set_member config fragment value)
  set(path "${JH_WORK}/${config}/features/${fragment}.json")
  file(READ "${path}" text)
  string(JSON text SET "${text}" ${ARGN} "${value}")
  file(WRITE "${path}" "${text}")
endfunction()

# Generate the artifacts of a configuration below JH_WORK/out/name.
function(generate config name)
  invoke(0 generate features --root "${JH_HAL_SOURCE}" --config-root "${JH_WORK}/${config}"
    --output-root "${JH_WORK}/out/${name}" --write)
endfunction()

function(read_artifact name artifact variable)
  file(READ "${JH_WORK}/out/${name}/${artifact}" text)
  set(${variable} "${text}" PARENT_SCOPE)
endfunction()

# A JSON string literal of text.
function(json_quoted variable text)
  string(REPLACE "\\" "\\\\" text "${text}")
  string(REPLACE "\"" "\\\"" text "${text}")
  string(REPLACE "\n" "\\n" text "${text}")
  string(REPLACE "\t" "\\t" text "${text}")
  set(${variable} "\"${text}\"" PARENT_SCOPE)
endfunction()

# value with the items of every array in reverse order and the members of
# every object in reverse name order (string(JSON MEMBER) lists them sorted),
# written as text: string(JSON SET) would sort them again.
function(json_reversed variable value)
  string(JSON type TYPE "${value}")
  string(JSON count LENGTH "${value}")
  set(items "")
  if(count GREATER 0)
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last} 0 -1)
      set(key ${index})
      set(prefix "")
      if(type STREQUAL "OBJECT")
        string(JSON key MEMBER "${value}" ${index})
        json_quoted(prefix "${key}")
        string(APPEND prefix ": ")
      endif()
      string(JSON item_type TYPE "${value}" "${key}")
      string(JSON item GET "${value}" "${key}")
      if(item_type STREQUAL "OBJECT" OR item_type STREQUAL "ARRAY")
        json_reversed(item "${item}")
      elseif(item_type STREQUAL "STRING")
        json_quoted(item "${item}")
      elseif(item_type STREQUAL "BOOLEAN")
        if(item)
          set(item true)
        else()
          set(item false)
        endif()
      elseif(item_type STREQUAL "NULL")
        set(item null)
      endif()
      if(NOT items STREQUAL "")
        string(APPEND items ", ")
      endif()
      string(APPEND items "${prefix}${item}")
    endforeach()
  endif()
  if(type STREQUAL "OBJECT")
    set(${variable} "{${items}}" PARENT_SCOPE)
  else()
    set(${variable} "[${items}]" PARENT_SCOPE)
  endif()
endfunction()

# ---- Generation ----

# The tracked artifacts are current, with the summary of --check.
invoke(0 generate features --root "${JH_HAL_SOURCE}" --check)
if(NOT stdout STREQUAL "verified 2 generated feature artifacts\n")
  message(FATAL_ERROR "Unexpected --check summary: ${stdout}")
endif()
copy_config(config)
generate(config first)
# Neither the order of symbols, fields and relations nor of any other JSON
# member or item changes an artifact.
copy_config(reordered)
file(GLOB fragments "${JH_WORK}/reordered/features/*.json")
foreach(fragment IN LISTS fragments)
  file(READ "${fragment}" text)
  json_reversed(text "${text}")
  file(WRITE "${fragment}" "${text}\n")
endforeach()
generate(reordered second)
foreach(artifact "${features_header}" "${features_cmake}")
  read_artifact(first "${artifact}" first_text)
  read_artifact(second "${artifact}" second_text)
  if(NOT first_text STREQUAL second_text)
    message(FATAL_ERROR "${artifact} depends on JSON member or item order")
  endif()
endforeach()
# The schema digest is frozen in the artifacts: any schema change shows.
copy_config(schema-digest)
file(READ "${JH_WORK}/schema-digest/features.schema.json" schema)
string(JSON schema SET "${schema}" title "\"Changed feature schema title\"")
file(WRITE "${JH_WORK}/schema-digest/features.schema.json" "${schema}")
generate(schema-digest schema-digest)
read_artifact(first "${features_header}" first_text)
read_artifact(schema-digest "${features_header}" changed_text)
if(first_text STREQUAL changed_text)
  message(FATAL_ERROR "A schema change did not change the generated header")
endif()

# ---- Registries the generator rejects ----

function(rejected name expected)
  invoke(2 generate features --root "${JH_HAL_SOURCE}" --config-root "${JH_WORK}/${name}"
    --output-root "${JH_WORK}/out/${name}" --write)
  if(NOT stderr MATCHES "${expected}" OR EXISTS "${JH_WORK}/out/${name}")
    message(FATAL_ERROR "${name}: the registry was not rejected with '${expected}' before writing: ${stderr}")
  endif()
endfunction()
copy_config(unknown)
file(READ "${JH_WORK}/unknown/features/connectivity.json" text)
string(JSON implies GET "${text}" symbols HAL_ENABLE_MQTT implies)
string(JSON count LENGTH "${text}" symbols HAL_ENABLE_MQTT implies)
string(JSON text SET "${text}" symbols HAL_ENABLE_MQTT implies ${count} "\"HAL_ENABLE_UNKNOWN\"")
file(WRITE "${JH_WORK}/unknown/features/connectivity.json" "${text}")
rejected(unknown "unknown symbol 'HAL_ENABLE_UNKNOWN'")
copy_config(self)
set_member(self core "[\"HAL_ENABLE_APP_TASK1\"]" symbols HAL_ENABLE_APP_TASK1 implies)
rejected(self "cannot reference itself")
copy_config(cycle)
set_member(cycle core "[\"HAL_ENABLE_CRC\"]" symbols HAL_ENABLE_APP_TASK1 implies)
set_member(cycle core "[\"HAL_ENABLE_APP_TASK1\"]" symbols HAL_ENABLE_CRC implies)
rejected(cycle "implies cycle")
copy_config(duplicate)
set_member(duplicate buses "{}" symbols HAL_ENABLE_APP_TASK1)
rejected(duplicate "duplicate symbol")
copy_config(field)
set_member(field core "\"boolean\"" symbols HAL_ENABLE_APP_TASK1 type)
rejected(field "only fields")
copy_config(boolean-version)
set_member(boolean-version core "true" schemaVersion)
rejected(boolean-version "the integer 1")
copy_config(asymmetric)
set_member(asymmetric core "[\"HAL_ENABLE_CRC\"]" symbols HAL_ENABLE_APP_TASK1 conflicts)
rejected(asymmetric "symmetric conflict")
copy_config(closure-conflict)
set_member(closure-conflict core "[\"HAL_ENABLE_CRC\"]" symbols HAL_ENABLE_APP_TASK1 implies)
set_member(closure-conflict core "[\"HAL_ENABLE_CRC\"]" symbols HAL_ENABLE_APP_TASK1 conflicts)
set_member(closure-conflict core "[\"HAL_ENABLE_APP_TASK1\"]" symbols HAL_ENABLE_CRC conflicts)
rejected(closure-conflict "resolves to conflicting symbols")

# ---- requires and conflicts ----

copy_config(constraints)
set_member(constraints core "[\"HAL_ENABLE_CRC\"]" symbols HAL_ENABLE_APP_TASK1 requires)
set_member(constraints core "[\"HAL_ENABLE_PNG\"]" symbols HAL_ENABLE_JPEG conflicts)
set_member(constraints core "[\"HAL_ENABLE_JPEG\"]" symbols HAL_ENABLE_PNG conflicts)
generate(constraints constraints)
read_artifact(constraints "${features_header}" constraints_header)
if(NOT constraints_header MATCHES "\\[JH-CFG-REQUIRES\\]" OR NOT constraints_header MATCHES "\\[JH-CFG-CONFLICT\\]")
  message(FATAL_ERROR "The generated header lacks the requires or conflict rule")
endif()
invoke(1 feature resolve --config-root "${JH_WORK}/constraints" --context test:requires
  --request HAL_ENABLE_APP_TASK1 --json)
json_strings(findings findings)
if(NOT findings MATCHES "test:requires: \\[JH-CFG-REQUIRES\\] HAL_ENABLE_APP_TASK1 requires HAL_ENABLE_CRC")
  message(FATAL_ERROR "resolve did not enforce requires after the closure: ${stdout}")
endif()
invoke(0 feature resolve --config-root "${JH_WORK}/constraints" --request HAL_ENABLE_APP_TASK1
  --request HAL_ENABLE_CRC)
invoke(1 feature resolve --config-root "${JH_WORK}/constraints" --context test:conflict
  --request HAL_ENABLE_JPEG --request HAL_ENABLE_PNG --json)
json_strings(findings findings)
if(NOT findings MATCHES "test:conflict: \\[JH-CFG-CONFLICT\\] HAL_ENABLE_JPEG conflicts with HAL_ENABLE_PNG")
  message(FATAL_ERROR "resolve did not enforce conflicts after the closure: ${stdout}")
endif()
# The CMake functions of the generated data enforce the same rules.
foreach(case "HAL_ENABLE_APP_TASK1|JH-CFG-REQUIRES" "HAL_ENABLE_JPEG;HAL_ENABLE_PNG|JH-CFG-CONFLICT")
  string(REPLACE "|" ";" parts "${case}")
  list(POP_BACK parts code)
  string(REPLACE ";" " " requests "${parts}")
  file(WRITE "${JH_WORK}/resolve-${code}.cmake"
    "include(\"${JH_WORK}/out/constraints/${features_cmake}\")\n"
    "jh_hal_resolve_features(requested resolved ${requests})\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" -P "${JH_WORK}/resolve-${code}.cmake"
    RESULT_VARIABLE code_result OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(code_result EQUAL 0 OR NOT "${out}${err}" MATCHES "\\[${code}\\]")
    message(FATAL_ERROR "The generated CMake resolver accepted ${requests}: ${out}${err}")
  endif()
endforeach()

# The generated header enforces them in the compiler (GCC or Clang; the probe
# reads the preprocessor's diagnostics), and rejects a derived request.
function(preprocess_header expected header)
  list(JOIN ARGN "\n" lines)
  file(WRITE "${JH_WORK}/header-probe.c" "${lines}\n#include \"${header}\"\n")
  execute_process(COMMAND "${JH_CC}" -std=c11 -E -x c "${JH_WORK}/header-probe.c"
    RESULT_VARIABLE code OUTPUT_QUIET ERROR_VARIABLE err)
  if(expected STREQUAL "" AND NOT code EQUAL 0)
    message(FATAL_ERROR "${header} rejected ${ARGN}:\n${err}")
  elseif(NOT expected STREQUAL "" AND (code EQUAL 0 OR NOT err MATCHES "\\[${expected}\\]"))
    message(FATAL_ERROR "${header} did not report ${expected} for ${ARGN}:\n${err}")
  endif()
endfunction()
if(JH_CC AND NOT JH_CC_ID STREQUAL "MSVC")
  set(constraints_header_path "${JH_WORK}/out/constraints/${features_header}")
  preprocess_header(JH-CFG-REQUIRES "${constraints_header_path}" "#define HAL_ENABLE_APP_TASK1 1")
  preprocess_header("" "${constraints_header_path}" "#define HAL_ENABLE_APP_TASK1 1"
    "#define HAL_ENABLE_CRC 1")
  preprocess_header(JH-CFG-CONFLICT "${constraints_header_path}" "#define HAL_ENABLE_JPEG 1"
    "#define HAL_ENABLE_PNG 1")
  preprocess_header(JH-CFG-DERIVED "${JH_WORK}/out/first/${features_header}"
    "#define HAL_ENABLE_NETWORK_CORE 1")
endif()

# ---- Resolution ----

# A request the closure already holds changes neither the closure nor its
# digest.
invoke(0 feature resolve --root "${JH_HAL_SOURCE}" --context test:minimal
  --request HAL_ENABLE_MQTT --source test:minimal --json)
string(JSON minimal_resolved GET "${stdout}" resolvedFeatures)
string(JSON minimal_digest GET "${stdout}" resolvedFeaturesDigest)
invoke(0 feature resolve --root "${JH_HAL_SOURCE}" --context test:redundant
  --request HAL_ENABLE_MQTT --source test:mqtt --request HAL_ENABLE_TCP=1 --source test:tcp --json)
string(JSON redundant_resolved GET "${stdout}" resolvedFeatures)
string(JSON redundant_digest GET "${stdout}" resolvedFeaturesDigest)
if(NOT minimal_resolved STREQUAL redundant_resolved OR NOT minimal_digest STREQUAL redundant_digest)
  message(FATAL_ERROR "A redundant request changed the closure or its digest")
endif()

# ---- Raw lint ----

set(consumer "${JH_WORK}/consumer")
set(consumer_header "${consumer}/hal_project_config.h")
set(consumer_manifest "${consumer}/.vscode/jaszczurhal.project.json")
function(lint expected)
  invoke(${expected} feature lint --root "${JH_HAL_SOURCE}" ${ARGN})
  set(stderr "${stderr}" PARENT_SCOPE)
endfunction()
file(WRITE "${consumer_header}" "#pragma once\n#define HAL_ENABLE_MQTT\n#define HAL_ENABLE_BLE_STREAM 1\n")
# A symbol in a manifest field the lint does not read is no request.
file(WRITE "${consumer_manifest}" "{\"target\": \"rp2040\", \"board\": \"pico\", \"note\": \"HAL_ENABLE_UNKNOWN\"}")
lint(0 --input-root "${consumer}")
# A manifest holds no project configuration, wherever it would sit.
foreach(case
    "{\"cmake\": {\"cache\": {\"JH_EXTRA_DEFINES\": \"\"}}}|cmake.cache.JH_EXTRA_DEFINES"
    "{\"cmake\": {\"cache\": {\"EXTRA_HAL_DEFINES\": \"HAL_ENABLE_I2C\"}}}|cmake.cache.EXTRA_HAL_DEFINES"
    "{\"cmake\": {\"cache\": {\"HAL_ENABLE_WIFI\": 1}}}|cmake.cache.HAL_ENABLE_WIFI"
    "{\"targetProfiles\": {\"stm32g474\": {\"cmake\": {\"cache\": {\"JH_PROJECT_SOURCES\": \"app.c\"}}}}}|targetProfiles.stm32g474.cmake.cache.JH_PROJECT_SOURCES"
    "{\"variants\": [{\"id\": \"bench\"}]}|'variants' is not a manifest field")
  string(FIND "${case}" "|" bar)
  string(SUBSTRING "${case}" 0 ${bar} manifest)
  math(EXPR rest "${bar} + 1")
  string(SUBSTRING "${case}" ${rest} -1 location)
  file(WRITE "${consumer_manifest}" "${manifest}")
  lint(1 --input-root "${consumer}")
  string(FIND "${stderr}" "${location}" found)
  if(found EQUAL -1 OR NOT stderr MATCHES "\\[JH-CFG-MANIFEST\\]")
    message(FATAL_ERROR "The lint accepted a manifest carrying ${location}: ${stderr}")
  endif()
endforeach()
file(WRITE "${consumer_manifest}" "{\"target\": \"rp2040\", \"board\": \"pico\"}")
# Every --input-root is read.
set(second_consumer "${JH_WORK}/consumer-second")
file(WRITE "${second_consumer}/hal_project_config.h" "#define HAL_ENABLE_UNKNOWN\n")
lint(1 --input-root "${consumer}" --input-root "${second_consumer}")
slashed(lint_text "${stderr}")
string(FIND "${lint_text}" "${second_consumer}" found)
if(found EQUAL -1)
  message(FATAL_ERROR "A repeated --input-root was not read: ${stderr}")
endif()
function(header_findings text expected code)
  file(WRITE "${consumer_header}" "${text}")
  lint(${expected} --input-root "${consumer}")
  if(NOT code STREQUAL "" AND NOT stderr MATCHES "${code}")
    message(FATAL_ERROR "The lint did not report ${code} for:\n${text}\n${stderr}")
  endif()
  set(stderr "${stderr}" PARENT_SCOPE)
endfunction()
header_findings("#define HAL_ENABLE_UNKNOWN\n" 1 "\\[JH-CFG-UNKNOWN\\]")
# =0 behind comments that look like they open one, and in a define spliced
# over two lines; --report-only turns findings into warnings.
header_findings("#define HAL_COMMENT_MARKER \"/*\"\n// another marker /*\n#define HAL_ENABLE_WIFI /* value follows\nthe multiline comment */ 0\n"
  1 "\\[JH-CFG-VALUE\\]")
lint(0 --report-only --input-root "${consumer}")
if(NOT stderr MATCHES "warning:")
  message(FATAL_ERROR "--report-only did not warn: ${stderr}")
endif()
header_findings("#define \\\n HAL_ENABLE_WIFI 0\n" 1 "\\[JH-CFG-VALUE\\]")
# A line comment continued over the next line hides the define.
header_findings("// hidden by a continued line comment \\\n#define HAL_ENABLE_WIFI 0\n" 0 "")
header_findings("#define HAL_ENABLE_NETWORK_CORE\n" 1 "\\[JH-CFG-DERIVED\\]")
# Every branch and every variant, whatever a build selects.
header_findings("#define JH_PROJECT_VARIANTS(X) X(PROBE, \"Probe\", HAL_ENABLE_WIFI=0)\n#if 0\n#define HAL_ENABLE_UNKNOWN\n#endif\n"
  1 "\\[JH-CFG-UNKNOWN\\]")
if(NOT stderr MATCHES "JH_PROJECT_VARIANTS\\(PROBE\\): \\[JH-CFG-VALUE\\]")
  message(FATAL_ERROR "The lint skipped a variant definition: ${stderr}")
endif()
# A condition on a name no build passes is no raw-lint finding.
header_findings("#if defined(SDK_ONLY_SWITCH)\n#define HAL_ENABLE_WIFI\n#endif\n" 0 "")
# The checkout lints clean.
lint(0)

# The board registry bounds the ranges of one pin set to 65536 pins, so a
# huge last fails instead of being expanded (the Python validator ran out of
# memory there). 2^32 wraps a 32-bit size_t, where only the span check holds.
file(COPY "${JH_HAL_SOURCE}/boards" DESTINATION "${JH_WORK}/board-limit")
set(board_target "${JH_WORK}/board-limit/boards/targets/rp2040.json")
file(READ "${board_target}" board_text)
function(valid_pins ranges expected)
  string(JSON text SET "${board_text}" gpio validPins ranges "${ranges}")
  file(WRITE "${board_target}" "${text}")
  invoke(${expected} board validate --root "${JH_HAL_SOURCE}" --boards-root "${JH_WORK}/board-limit/boards")
  if(expected EQUAL 2 AND NOT stderr MATCHES "expected a range within 65536 pins")
    message(FATAL_ERROR "Pin set ${ranges} failed for another reason: ${stderr}")
  endif()
endfunction()
valid_pins("[{\"first\": 0, \"last\": 65535}]" 0)
valid_pins("[{\"first\": 0, \"last\": 65536}]" 2)
valid_pins("[{\"first\": 0, \"last\": 32767}, {\"first\": 32768, \"last\": 65536}]" 2)
valid_pins("[{\"first\": 0, \"last\": 4294967296}]" 2)
# Two capabilities on one bit: the later one fails. string(JSON SET) writes
# the members in name order, so usb-device follows cyw43.
file(WRITE "${board_target}" "${board_text}")
set(board_capabilities "${JH_WORK}/board-limit/boards/capabilities.json")
file(READ "${board_capabilities}" text)
string(JSON text SET "${text}" capabilities cyw43 bit 0)
file(WRITE "${board_capabilities}" "${text}")
invoke(2 board validate --root "${JH_HAL_SOURCE}" --boards-root "${JH_WORK}/board-limit/boards")
if(NOT stderr MATCHES "capabilities\\.usb-device\\.bit: got 0; expected a unique capability bit")
  message(FATAL_ERROR "A repeated capability bit passed or gave another reason: ${stderr}")
endif()

# A USB identity is an integer or 0x with hexadecimal digits of either case
# up to 0xFFFF; leading zeros do not count towards the range, and nothing may
# follow the digits, not even a line feed. board.schema.json, which only the
# editor reads, gives the same verdict, except for 1.0: JSON Schema counts it
# as an integer. A descriptor lists exactly as the same file with decimal IDs,
# for a board's programmer and for a target's application defaults, so each
# ID becomes its number in its own place. The files are edited as text, since
# string(JSON SET) and string(JSON MEMBER) put the members in name order.
file(COPY "${JH_HAL_SOURCE}/boards" DESTINATION "${JH_WORK}/board-usb")
set(usb_boards "${JH_WORK}/board-usb/boards")
# Write text to file with the USB object usb, which text must contain,
# replaced by replacement.
function(write_usb file text usb replacement)
  string(FIND "${text}" "${usb}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "${file} has no ${usb}")
  endif()
  string(REPLACE "${usb}" "${replacement}" text "${text}")
  file(WRITE "${file}" "${text}")
endfunction()
# The JSON listing of the copy, in variable.
function(usb_listing variable)
  invoke(0 board list --root "${JH_HAL_SOURCE}" --boards-root "${usb_boards}" --json)
  set(${variable} "${stdout}" PARENT_SCOPE)
endfunction()
# The listing of the copy must be the text decimal, the listing with the
# same IDs in decimal.
function(usb_listed what decimal)
  usb_listing(listing)
  if(NOT listing STREQUAL decimal)
    message(FATAL_ERROR "${what} is listed differently from decimal IDs:\n${listing}\n${decimal}")
  endif()
endfunction()
set(usb_profile "${usb_boards}/profiles/waveshare-esp32-s3-zero.json")
file(READ "${usb_profile}" usb_profile_text)
set(usb_profile_ids "\"usb\": { \"vid\": \"0x303A\", \"pid\": \"0x1001\" }")
write_usb("${usb_profile}" "${usb_profile_text}" "${usb_profile_ids}"
  "\"usb\": { \"vid\": 12346, \"pid\": 4097 }")
usb_listing(usb_decimal)
# jh board validate and the schema must exit with expected and schema. pid
# stays the integer 4097, so a converted vid listed after it would show.
function(usb_vid value expected schema)
  write_usb("${usb_profile}" "${usb_profile_text}" "${usb_profile_ids}"
    "\"usb\": { \"vid\": ${value}, \"pid\": 4097 }")
  invoke(${expected} board validate --root "${JH_HAL_SOURCE}" --boards-root "${usb_boards}")
  if(expected EQUAL 2 AND NOT stderr MATCHES
     "waveshare-esp32-s3-zero\\.json: \\$\\.programming\\.usb\\.vid: got [^\n]*; expected a 16-bit USB identifier")
    message(FATAL_ERROR "USB VID ${value} failed for another reason: ${stderr}")
  endif()
  invoke(${schema} json validate --file "${usb_profile}" --schema "${JH_HAL_SOURCE}/boards/board.schema.json")
endfunction()
foreach(value IN ITEMS "\"0x303A\"" "\"0x303a\"" "\"0x0303A\"" "\"0x00000000303A\"" "12346")
  usb_vid("${value}" 0 0)
  usb_listed("USB VID ${value}" "${usb_decimal}")
endforeach()
usb_vid("\"0x0\"" 0 0)
usb_vid("\"0x0000FFFF\"" 0 0)
foreach(value IN ITEMS "\"0x\"" "\"0x303G\"" "\"0X303A\"" "\"303A\"" "\" 0x303A\"" "\"0x303A\\n\""
              "\"0x10000\"" "\"0x000010000\"" "65536" "-1" "true")
  usb_vid("${value}" 2 2)
endforeach()
usb_vid("1.0" 2 0)
file(WRITE "${usb_profile}" "${usb_profile_text}")
usb_listed("${usb_profile_ids}" "${usb_decimal}")
set(usb_target "${usb_boards}/targets/rp2040.json")
file(READ "${usb_target}" usb_target_text)
set(usb_target_ids "\"usbDevice\": { \"vid\": \"0x2E8A\", \"pid\": \"0x000A\" }")
write_usb("${usb_target}" "${usb_target_text}" "${usb_target_ids}"
  "\"usbDevice\": { \"vid\": 11914, \"pid\": 10 }")
usb_listing(usb_decimal)
write_usb("${usb_target}" "${usb_target_text}" "${usb_target_ids}"
  "\"usbDevice\": { \"vid\": \"0x2E8A\", \"pid\": \"0x000a\" }")
usb_listed("rp2040 usbDevice" "${usb_decimal}")

# Clock registry commands. A TARGET=PATH without either half is refused
# before anything is read.
foreach(item "esp32s3" "=x" "esp32s3=")
  invoke(2 clock check --root "${JH_HAL_SOURCE}" "--esp-sdkconfig=${item}" --json)
  status(HAL_EINVAL)
endforeach()
# A registry check failure has exit code 1 and the message of the check.
file(COPY "${JH_HAL_SOURCE}/config/hardware/clocks" DESTINATION "${JH_WORK}/clock-copy")
set(clock_file "${JH_WORK}/clock-copy/clocks/esp32s3.json")
file(READ "${clock_file}" text)
string(JSON text SET "${text}" trees default frequenciesHz cpu 240000000)
file(WRITE "${clock_file}" "${text}")
invoke(1 clock check --root "${JH_HAL_SOURCE}" --clocks "${JH_WORK}/clock-copy/clocks" --json)
status(HAL_ECONFIG)
string(JSON message GET "${stdout}" message)
if(NOT message STREQUAL "esp32s3/default: frequencies {'cpu': 240000000} do not follow from the parameters {'cpu': 160000000}")
  message(FATAL_ERROR "Clock check gave another message: ${message}")
endif()
# The listing does not check the trees: it reads them, names the board
# component of each STM32G474 tree and lists the source problems of every
# board, compatible target and tree.
invoke(0 clock list --root "${JH_HAL_SOURCE}" --clocks "${JH_WORK}/clock-copy/clocks" --json)
status(HAL_OK)
string(JSON cpu GET "${stdout}" targets 1 registry trees default frequenciesHz cpu)
string(JSON default_component TYPE "${stdout}" stm32g474TreeComponents default)
string(JSON hse_component GET "${stdout}" stm32g474TreeComponents hse-160mhz)
if(NOT cpu EQUAL 240000000 OR NOT default_component STREQUAL "NULL" OR
   NOT hse_component STREQUAL "stm32g474-hse-24mhz")
  message(FATAL_ERROR "Clock listing changed: ${stdout}")
endif()
string(JSON entries LENGTH "${stdout}" boardSources)
if(entries EQUAL 0)
  message(FATAL_ERROR "Clock listing has no board sources: ${stdout}")
endif()
invoke(0 clock list --root "${JH_HAL_SOURCE}" --clocks "${JH_WORK}/clock-copy/clocks")
if(NOT stdout MATCHES "stm32g474 hse-160mhz\n")
  message(FATAL_ERROR "Clock listing lacks its trees: ${stdout}")
endif()
file(REMOVE "${JH_WORK}/clock-copy/clocks/esp32.json")
invoke(0 clock list --root "${JH_HAL_SOURCE}" --clocks "${JH_WORK}/clock-copy/clocks" --json)
file(WRITE "${JH_WORK}/clock-copy/clocks/broken.json" "{\"target\": \"broken\",")
invoke(2 clock list --root "${JH_HAL_SOURCE}" --clocks "${JH_WORK}/clock-copy/clocks" --json)
string(JSON message GET "${stdout}" message)
if(NOT message MATCHES "broken\\.json: invalid JSON")
  message(FATAL_ERROR "A broken clock file gave another message: ${message}")
endif()
# Boards are checked in the order of their IDs, as sorted() of a dict did:
# pico before pico-w, although pico-w.json sorts before pico.json.
file(COPY "${JH_HAL_SOURCE}/boards" DESTINATION "${JH_WORK}/clock-boards")
set(profiles "${JH_WORK}/clock-boards/boards/profiles")
file(READ "${profiles}/pico.json" text)
string(JSON text REMOVE "${text}" clockSources xosc)
file(WRITE "${profiles}/pico.json" "${text}")
file(READ "${profiles}/picow.json" text)
string(JSON text REMOVE "${text}" clockSources xosc)
string(JSON text SET "${text}" id "\"pico-w\"")
file(REMOVE "${profiles}/picow.json")
file(WRITE "${profiles}/pico-w.json" "${text}")
invoke(1 clock check --root "${JH_HAL_SOURCE}" --boards-root "${JH_WORK}/clock-boards/boards" --json)
string(JSON message GET "${stdout}" message)
if(NOT message STREQUAL "pico: default rp2040 tree: no xosc source")
  message(FATAL_ERROR "Boards were not checked in ID order: ${message}")
endif()

# ---- Board artifacts ----

# The checked-in artifacts are current; generate boards takes exactly one of
# --write and --check.
invoke(0 generate boards --root "${JH_HAL_SOURCE}" --check)
if(NOT stdout STREQUAL "verified 3 generated board artifacts\n")
  message(FATAL_ERROR "generate boards --check gave another summary: ${stdout}")
endif()
invoke(2 generate boards --root "${JH_HAL_SOURCE}" --json)
status(HAL_EINVAL)
# Writing below a new output root creates the directories and the files of the
# checkout; a second write changes nothing.
set(board_out "${JH_WORK}/board-artifacts")
file(REMOVE_RECURSE "${board_out}")
invoke(0 generate boards --root "${JH_HAL_SOURCE}" --write --output-root "${board_out}")
if(NOT stdout STREQUAL "generated 3 board artifacts (3 changed)\n")
  message(FATAL_ERROR "generate boards --write gave another summary: ${stdout}")
endif()
foreach(artifact src/hal/generated/jh_board_registry.h
                 src/hal/generated/jh_board_fallback_config.h
                 cmake/generated/jh_board_components_registry.cmake)
  file(READ "${JH_HAL_SOURCE}/${artifact}" expected)
  file(READ "${board_out}/${artifact}" written)
  if(NOT written STREQUAL expected)
    message(FATAL_ERROR "generate boards wrote another ${artifact}")
  endif()
endforeach()
invoke(0 generate boards --root "${JH_HAL_SOURCE}" --write --output-root "${board_out}" --json)
status(HAL_OK)
string(JSON changed GET "${stdout}" changed)
if(NOT changed EQUAL 0)
  message(FATAL_ERROR "A second generate boards --write changed ${changed} files")
endif()
# A stale artifact shows with a diff, a missing one by name; exit code 1.
file(APPEND "${board_out}/src/hal/generated/jh_board_registry.h" "/* edit */\n")
file(REMOVE "${board_out}/cmake/generated/jh_board_components_registry.cmake")
invoke(1 generate boards --root "${JH_HAL_SOURCE}" --check --output-root "${board_out}")
if(NOT stderr MATCHES "error: stale generated artifact [^\n]*jh_board_registry\\.h\n" OR
   NOT stderr MATCHES "\n-/\\* edit \\*/\n" OR
   NOT stderr MATCHES "error: missing generated artifact [^\n]*jh_board_components_registry\\.cmake")
  message(FATAL_ERROR "generate boards --check reported drift differently: ${stderr}")
endif()
# Every artifact follows the board registry of --boards-root.
file(COPY "${JH_HAL_SOURCE}/boards" DESTINATION "${JH_WORK}/board-render")
set(render_boards "${JH_WORK}/board-render/boards")
file(READ "${render_boards}/capabilities.json" text)
string(JSON text SET "${text}" capabilities usb-device macro "\"HAL_BOARD_CAP_USB_RENAMED\"")
file(WRITE "${render_boards}/capabilities.json" "${text}")
invoke(1 generate boards --root "${JH_HAL_SOURCE}" --boards-root "${render_boards}"
  --check --output-root "${JH_HAL_SOURCE}")
if(NOT stderr MATCHES "\n\\+#define HAL_BOARD_CAP_USB_RENAMED \\(UINT32_C\\(1\\) << ")
  message(FATAL_ERROR "generate boards ignored --boards-root: ${stderr}")
endif()

# board config: the files of one build, as JSON and below an output directory.
set(config_root "${JH_WORK}/board-config/.build")
file(REMOVE_RECURSE "${JH_WORK}/board-config")
invoke(0 board config --root "${JH_HAL_SOURCE}" --target rp2040 --json)
status(HAL_OK)
string(JSON board GET "${stdout}" board)
string(JSON cmake GET "${stdout}" files jh_board_config.cmake)
string(JSON header GET "${stdout}" files jh_board_config.h)
string(JSON resolved GET "${stdout}" files jh_board_resolved.json)
string(JSON dependencies GET "${stdout}" files generation.d)
if(NOT board STREQUAL "pico" OR
   NOT cmake MATCHES "^# Generated by jh board config; do not edit\\.\nset\\(JH_RESOLVED_TARGET \"rp2040\"\\)\nset\\(JH_RESOLVED_BOARD \"pico\"\\)\n" OR
   NOT header MATCHES "\n#define HAL_BOARD_PROFILE_RP_PICO 1\n" OR
   NOT header MATCHES "\n#define HAL_BOARD_IS_RP_PICO 1\n#define HAL_BOARD_IS_RP_PICO_W 0\n" OR
   NOT resolved MATCHES "^{\n  \"board\": \"pico\",\n" OR
   NOT dependencies MATCHES "/boards/profiles/pico\\.json\n")
  message(FATAL_ERROR "board config gave other files: ${stdout}")
endif()
# Requested features resolve with their closure and provenance.
invoke(0 board config --root "${JH_HAL_SOURCE}" --target esp32s3 --json
  --feature HAL_ENABLE_FREERTOS --feature " -DHAL_ENABLE_WIFI=1 ")
string(JSON cmake GET "${stdout}" files jh_board_config.cmake)
string(JSON resolved GET "${stdout}" files jh_board_resolved.json)
string(JSON sources LENGTH "${resolved}" featureProvenance HAL_ENABLE_FREERTOS)
string(JSON first GET "${resolved}" featureProvenance HAL_ENABLE_FREERTOS 0)
string(JSON second GET "${resolved}" featureProvenance HAL_ENABLE_FREERTOS 1)
if(NOT cmake MATCHES "\nset\\(JH_BOARD_REQUESTED_FEATURES \"HAL_ENABLE_FREERTOS;HAL_ENABLE_WIFI\"\\)\n" OR
   NOT sources EQUAL 2 OR NOT first STREQUAL "command-line:--requested-feature[0]" OR
   NOT second STREQUAL "target:esp32s3:requiredFeatures[0]")
  message(FATAL_ERROR "board config resolved features differently: ${cmake}\n${resolved}")
endif()
# The rejections of generate_board_config.py, each with exit code 2.
function(config_fails message)
  invoke(2 board config --root "${JH_HAL_SOURCE}" --json ${ARGN})
  string(JSON actual GET "${stdout}" message)
  slashed(actual "${actual}")
  if(NOT actual STREQUAL message)
    message(FATAL_ERROR "board config ${ARGN}: expected \"${message}\", got \"${actual}\"")
  endif()
endfunction()
config_fails("generation requires --target and --output-dir")
config_fails("unknown target 'nosuch'" --target nosuch)
config_fails("unknown board 'nosuch'" --target rp2040 --board nosuch)
config_fails("board 'nucleo-g474re' is not compatible with target 'rp2040'"
  --target rp2040 --board nucleo-g474re)
config_fails("compile definition 'HAL_BOARD_PROFILE_PICOW' conflicts with resolved board 'pico'"
  --target rp2040 --define HAL_BOARD_PROFILE_PICOW)
config_fails("[JH-CFG-VALUE] compile definition 'HAL_ENABLE_WIFI=2': HAL feature symbols accept only a bare macro or an explicit value of 1"
  --target rp2040 --define HAL_ENABLE_WIFI=2)
config_fails("[JH-CFG-TARGET-REQUIRED] esp32s3 requires HAL_ENABLE_FREERTOS; HAL_DISABLE_FREERTOS cannot be requested"
  --target esp32s3 --feature HAL_DISABLE_FREERTOS)
config_fails("command-line:--requested-feature[0]: [JH-CFG-UNKNOWN] unknown feature HAL_ENABLE_NOPE"
  --target rp2040 --feature HAL_ENABLE_NOPE)
invoke(2 board config --root "${JH_HAL_SOURCE}" --target rp2040)
if(NOT stderr MATCHES "generation requires --target and --output-dir")
  message(FATAL_ERROR "board config without an output directory: ${stderr}")
endif()
# The output directory lies below an output root that is a .build directory
# or below the runtime-managed build root; a jh_board_registry.h an older
# generator left there goes.
unset(ENV{JH_MANAGED_BUILD_ROOT})
cmake_path(GET JH_WORK ROOT_PATH filesystem_root)
config_fails("output directory must be below ${config_root}, got ${JH_WORK}/board-config/elsewhere"
  --target rp2040 --output-root "${config_root}" --output-dir "${JH_WORK}/board-config/elsewhere")
invoke(2 board config --root "${JH_HAL_SOURCE}" --target rp2040 --json
  --output-root "${filesystem_root}" --output-dir "${config_root}/pico")
string(JSON actual GET "${stdout}" message)
if(NOT actual MATCHES "^output root must be a \\.build directory, one of its descendants, or the runtime-managed build root; got ")
  message(FATAL_ERROR "An output root without .build was accepted: ${stdout}")
endif()
file(MAKE_DIRECTORY "${config_root}/pico")
file(WRITE "${config_root}/pico/jh_board_registry.h" "stale\n")
invoke(0 board config --root "${JH_HAL_SOURCE}" --target rp2040 --output-root "${config_root}"
  --output-dir "${config_root}/pico")
if(NOT stdout STREQUAL "" OR EXISTS "${config_root}/pico/jh_board_registry.h" OR
   NOT EXISTS "${config_root}/pico/generation.d")
  message(FATAL_ERROR "board config did not replace the older outputs: ${stdout}")
endif()
file(READ "${config_root}/pico/jh_board_config.h" written)
if(NOT written STREQUAL header)
  # header still holds the esp32s3 file; compare with the rp2040 one.
  invoke(0 board config --root "${JH_HAL_SOURCE}" --target rp2040 --json)
  string(JSON header GET "${stdout}" files jh_board_config.h)
  if(NOT written STREQUAL header)
    message(FATAL_ERROR "board config wrote another header than it returns")
  endif()
endif()
set(ENV{JH_MANAGED_BUILD_ROOT} "${filesystem_root}")
invoke(0 board config --root "${JH_HAL_SOURCE}" --target rp2040 --json
  --output-root "${filesystem_root}" --output-dir "${JH_WORK}/board-config/managed")
string(JSON changed GET "${stdout}" changed)
if(NOT changed EQUAL 7)
  message(FATAL_ERROR "board config below the managed build root wrote ${changed} files")
endif()
unset(ENV{JH_MANAGED_BUILD_ROOT})
# The output paths resolve as Path.resolve() does: "." drops out, ".." takes
# the parent also in a part that does not exist yet, and an escape that way is
# still outside the output root.
function(config_writes expected)
  invoke(0 board config --root "${JH_HAL_SOURCE}" --target rp2040 ${ARGN})
  if(NOT EXISTS "${expected}/jh_board_config.h")
    message(FATAL_ERROR "board config ${ARGN} did not write into ${expected}")
  endif()
endfunction()
config_writes("${config_root}/dotted" --output-root "${config_root}"
  --output-dir "${config_root}/missing/./../dotted")
config_fails("output directory must be below ${config_root}, got ${JH_WORK}/board-config/escaped"
  --target rp2040 --output-root "${config_root}"
  --output-dir "${config_root}/missing/../../escaped")
# An output file it cannot write exits with 2, as the shared publisher and
# generate_board_config.py did; an output directory it cannot create with 3.
file(MAKE_DIRECTORY "${config_root}/blocked/jh_board_config.h")
invoke(2 board config --root "${JH_HAL_SOURCE}" --target rp2040 --json
  --output-root "${config_root}" --output-dir "${config_root}/blocked")
string(JSON actual GET "${stdout}" message)
slashed(actual "${actual}")
string(FIND "${actual}"
  "${config_root}/blocked/jh_board_config.h: cannot write generated output: HAL_" at)
if(NOT at EQUAL 0)
  message(FATAL_ERROR "board config wrote over a directory: ${stdout}")
endif()
file(WRITE "${config_root}/plain" "")
invoke(3 board config --root "${JH_HAL_SOURCE}" --target rp2040 --json
  --output-root "${config_root}" --output-dir "${config_root}/plain/below")
string(JSON actual GET "${stdout}" message)
slashed(actual "${actual}")
string(FIND "${actual}"
  "${config_root}/plain/below: cannot create the output directory: HAL_" at)
if(NOT at EQUAL 0)
  message(FATAL_ERROR "board config created a directory below a file: ${stdout}")
endif()
if(NOT CMAKE_HOST_WIN32)
  # POSIX resolves a symbolic link before a ".." that follows it, as the
  # kernel and generate_board_config.py did; Windows removes ".." first.
  set(linked "${JH_WORK}/board-linked")
  file(MAKE_DIRECTORY "${linked}/.build/actual/child" "${linked}/.build/alias"
    "${linked}/top" "${linked}/real/.build" "${JH_WORK}/outside")
  file(CREATE_LINK "../actual/child" "${linked}/.build/alias/link" SYMBOLIC)
  file(CREATE_LINK "actual/child" "${linked}/.build/later" SYMBOLIC)
  # An absolute target outside the output root, not /: tools that follow
  # links would walk the whole machine from a build tree that keeps it.
  file(CREATE_LINK "${JH_WORK}/outside" "${linked}/.build/absolute" SYMBOLIC)
  file(CREATE_LINK "fresh" "${linked}/.build/dangling" SYMBOLIC)
  file(CREATE_LINK "loop" "${linked}/.build/loop" SYMBOLIC)
  file(CREATE_LINK "../real/.build/inner" "${linked}/top/link" SYMBOLIC)
  file(CREATE_LINK "${JH_HAL_SOURCE}/boards/profiles" "${linked}/boards-link"
    SYMBOLIC)
  config_writes("${linked}/.build/actual/out" --output-root "${linked}/.build"
    --output-dir "${linked}/.build/alias/link/../out")
  if(EXISTS "${linked}/.build/alias/out")
    message(FATAL_ERROR "board config removed .. before it resolved the link")
  endif()
  # Relative paths start at the working directory of jh, ${JH_WORK}/cwd.
  config_writes("${linked}/.build/actual/relative" --output-root "${linked}/.build"
    --output-dir "../board-linked/.build/alias/link/../relative")
  # A relative JH_MANAGED_BUILD_ROOT too: to-root in that directory leads to
  # the filesystem root, the output root without a .build component. Only /
  # has none wherever the build lies, so the link goes before the result is
  # checked: a failed case must not leave it either.
  file(CREATE_LINK "/" "${JH_WORK}/cwd/to-root" SYMBOLIC)
  set(ENV{JH_MANAGED_BUILD_ROOT} "to-root")
  execute_process(COMMAND "${JH}" board config --root "${JH_HAL_SOURCE}" --target rp2040
      --output-root "/" --output-dir "${linked}/managed"
    WORKING_DIRECTORY "${JH_WORK}/cwd" RESULT_VARIABLE code OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr ENCODING UTF-8)
  unset(ENV{JH_MANAGED_BUILD_ROOT})
  file(REMOVE "${JH_WORK}/cwd/to-root")
  if(NOT code STREQUAL "0" OR NOT EXISTS "${linked}/managed/jh_board_config.h")
    message(FATAL_ERROR
      "board config below a relative managed build root: exit ${code}\n${stdout}\n${stderr}")
  endif()
  config_writes("${linked}/.build/actual/child/after" --output-root "${linked}/.build"
    --output-dir "${linked}/.build/missing/../later/after")
  config_writes("${linked}/.build/fresh" --output-root "${linked}/.build"
    --output-dir "${linked}/.build/dangling")
  config_writes("${linked}/real/.build/linked-root" --output-root "${linked}/top/link/.."
    --output-dir "${linked}/real/.build/linked-root")
  file(REAL_PATH "${linked}" real_linked)
  file(REAL_PATH "${JH_WORK}/outside" real_outside)
  config_fails("output directory must be below ${real_linked}/.build, got ${real_outside}/etc"
    --target rp2040 --output-root "${linked}/.build"
    --output-dir "${linked}/.build/absolute/etc")
  invoke(3 board config --root "${JH_HAL_SOURCE}" --target rp2040 --json
    --output-root "${linked}/.build" --output-dir "${linked}/.build/loop/out")
  string(JSON actual GET "${stdout}" message)
  if(NOT actual STREQUAL "${real_linked}/.build/loop: too many levels of symbolic links")
    message(FATAL_ERROR "board config followed a link loop: ${stdout}")
  endif()
  invoke(0 generate boards --root "${JH_HAL_SOURCE}" --check --json
    --boards-root "${linked}/boards-link/..")
  string(JSON boards_root GET "${stdout}" boardsRoot)
  file(REAL_PATH "${JH_HAL_SOURCE}/boards" real_boards)
  if(NOT boards_root STREQUAL real_boards)
    message(FATAL_ERROR "--boards-root removed .. before it resolved the link: ${stdout}")
  endif()
endif()
# --file: the values an assembly owns replace the board's.
set(assembly "${JH_WORK}/board-config/assembly.json")
file(WRITE "${assembly}" [=[{"headerPrelude": ["#define PRELUDE_LINE 1"],
 "headerDefinitions": ["HAL_EXAMPLE=2u"], "cmakeDefinitions": ["HAL_ONLY_CMAKE"],
 "components": ["zeta", "alpha"], "contractHash": "0123456789abcdef",
 "generationExtra": ["extra/input.json"]}]=])
invoke(0 board config --root "${JH_HAL_SOURCE}" --target rp2040 --json "--file=${assembly}")
string(JSON cmake GET "${stdout}" files jh_board_config.cmake)
string(JSON header GET "${stdout}" files jh_board_config.h)
string(JSON dependencies GET "${stdout}" files generation.d)
if(NOT header MATCHES "#include <stdint.h>\n#define PRELUDE_LINE 1\n#define HAL_TARGET_DESCRIPTOR_ID" OR
   NOT header MATCHES "\n#define HAL_EXAMPLE 2u\n" OR
   NOT cmake MATCHES "\nset\\(JH_BOARD_COMPONENTS \"zeta;alpha\"\\)\nset\\(JH_BOARD_COMPILE_DEFINITIONS \"HAL_ONLY_CMAKE\"\\)\n" OR
   NOT cmake MATCHES "\nset\\(JH_BOARD_CONTRACT_SYMBOL \"jh_board_contract_rp2040_pico_0123456789ab\"\\)\n" OR
   NOT dependencies MATCHES "\nextra/input\\.json\n$")
  message(FATAL_ERROR "board config ignored the assembly: ${stdout}")
endif()
file(WRITE "${assembly}" "{\"headerPrelude\": [1], \"other\": true}")
config_fails("${assembly}: $: got ['other']; expected only fields ['board', 'cmakeDefinitions', 'components', 'contractHash', 'generationExtra', 'headerDefinitions', 'headerPrelude']"
  --target rp2040 "--file=${assembly}")
file(WRITE "${assembly}" "{\"headerPrelude\": [1]}")
config_fails("${assembly}: $.headerPrelude: got [1]; expected an array of strings"
  --target rp2040 "--file=${assembly}")
config_fails("[JH-CFG-VALUE] compile definition 'X=$<CONFIG>': generator expressions are not supported"
  --target rp2040 "--define=X=$<CONFIG>")
# An output directory above the output root is outside it as well.
config_fails("output directory must be below ${config_root}, got ${JH_WORK}/board-config"
  --target rp2040 --output-root "${config_root}" --output-dir "${JH_WORK}/board-config")
# A reserved capability keeps its macro and bit but stays out of
# HAL_BOARD_CAP_ALL and of the capabilities a board declares.
file(COPY "${JH_HAL_SOURCE}/boards" DESTINATION "${JH_WORK}/board-reserved")
set(reserved_boards "${JH_WORK}/board-reserved/boards")
file(READ "${reserved_boards}/capabilities.json" text)
string(JSON text SET "${text}" capabilities native-wifi status "\"reserved\"")
file(WRITE "${reserved_boards}/capabilities.json" "${text}")
invoke(0 generate boards --root "${JH_HAL_SOURCE}" --boards-root "${reserved_boards}"
  --write --output-root "${JH_WORK}/board-reserved/out")
file(READ "${JH_WORK}/board-reserved/out/src/hal/generated/jh_board_registry.h" registry_header)
if(NOT registry_header MATCHES "\n#define HAL_BOARD_CAP_NATIVE_WIFI \\(UINT32_C\\(1\\) << 5\\)\n" OR
   registry_header MATCHES "HAL_BOARD_CAP_ALL \\([^)\n]*HAL_BOARD_CAP_NATIVE_WIFI")
  message(FATAL_ERROR "A reserved capability counted as active:\n${registry_header}")
endif()
foreach(boards_dir "${JH_HAL_SOURCE}/boards" "${reserved_boards}")
  invoke(0 board config --root "${JH_HAL_SOURCE}" --boards-root "${boards_dir}"
    --target esp32s3 --json)
  string(JSON header GET "${stdout}" files jh_board_config.h)
  string(REGEX MATCH "HAL_BOARD_DECLARED_CAPABILITIES UINT32_C\\((0x[0-9a-f]+)\\)" _ "${header}")
  list(APPEND declared "${CMAKE_MATCH_1}")
endforeach()
list(GET declared 0 declared_all)
list(GET declared 1 declared_reserved)
math(EXPR declared_difference "${declared_all} - ${declared_reserved}")
if(NOT declared_difference EQUAL 32)
  message(FATAL_ERROR "A reserved capability stayed declared: ${declared}")
endif()
# The CMake catalogue lists the providers of a component sorted, whatever
# their order in config/tooling/board_components.json.
file(COPY "${JH_HAL_SOURCE}/config" "${JH_HAL_SOURCE}/boards"
  DESTINATION "${JH_WORK}/provider-root")
set(components_file "${JH_WORK}/provider-root/config/tooling/board_components.json")
file(READ "${components_file}" text)
string(REGEX REPLACE "\"jh-stm32-baremetal\",([ \r\n]*)\"pico-sdk\""
  "\"pico-sdk\",\\1\"jh-stm32-baremetal\"" reversed "${text}")
if(reversed STREQUAL text)
  message(FATAL_ERROR "board_components.json has no provider list to reverse")
endif()
file(WRITE "${components_file}" "${reversed}")
invoke(0 generate boards --root "${JH_WORK}/provider-root" --check
  --output-root "${JH_HAL_SOURCE}")
# A definition of the board's own selector or of one of its legacy selectors
# is accepted.
invoke(0 board config --root "${JH_HAL_SOURCE}" --target stm32g474 --json
  --define HAL_BOARD_PROFILE_STM32G474_GENERIC --define HAL_BOARD_PROFILE_STM32G474_NUCLEO_G474RE)
invoke(0 board config --root "${JH_HAL_SOURCE}" --target rp2040 --board picow --json
  --define HAL_CYW43_PROFILE_PICOW=1)
# Boards of different targets may share a legacy selector: the enum alias
# names the last board in profile order, the fallback picks the board by
# target.
file(COPY "${JH_HAL_SOURCE}/boards" DESTINATION "${JH_WORK}/board-legacy")
set(legacy_boards "${JH_WORK}/board-legacy/boards")
foreach(profile pico pico2)
  file(READ "${legacy_boards}/profiles/${profile}.json" text)
  string(JSON text SET "${text}" hal legacySelectors "[\"HAL_BOARD_PROFILE_SHARED_PROBE\"]")
  file(WRITE "${legacy_boards}/profiles/${profile}.json" "${text}")
endforeach()
invoke(0 generate boards --root "${JH_HAL_SOURCE}" --boards-root "${legacy_boards}"
  --write --output-root "${JH_WORK}/board-legacy/out")
file(READ "${JH_WORK}/board-legacy/out/src/hal/generated/jh_board_registry.h" registry_header)
file(READ "${JH_WORK}/board-legacy/out/src/hal/generated/jh_board_fallback_config.h" fallback_header)
if(NOT registry_header MATCHES "\n  HAL_BOARD_SHARED_PROBE = HAL_BOARD_RP_PICO_2,\n" OR
   NOT fallback_header MATCHES "#if defined\\(HAL_BOARD_PROFILE_SHARED_PROBE\\)\n#if HAL_TARGET_IS_RP2040\n#define HAL_BOARD_PROFILE_RP_PICO 1\n#elif HAL_TARGET_IS_RP2350_ARM\n#define HAL_BOARD_PROFILE_RP_PICO_2 1\n")
  message(FATAL_ERROR "A shared legacy selector resolved differently:\n${registry_header}")
endif()
# A board whose compatible targets need different board definitions has no
# fallback configuration.
file(COPY "${JH_HAL_SOURCE}/boards" DESTINATION "${JH_WORK}/board-fallback")
set(fallback_target "${JH_WORK}/board-fallback/boards/targets/rp2350-arm.json")
file(READ "${fallback_target}" text)
string(JSON text SET "${text}" components cyw43-pico-pio "{\"mode\": \"required\"}")
file(WRITE "${fallback_target}" "${text}")
invoke(2 generate boards --root "${JH_HAL_SOURCE}" --boards-root "${JH_WORK}/board-fallback/boards"
  --check --output-root "${JH_WORK}/board-fallback" --json)
string(JSON message GET "${stdout}" message)
if(NOT message STREQUAL "board 'pico2' requires target-specific fallback definitions")
  message(FATAL_ERROR "Target-specific board definitions gave another message: ${message}")
endif()
# No link left in the work directory may lead to a directory that holds it,
# such as /: an editor indexer, grep -R or find -L would walk the whole tree
# below it from the build directory, network mounts included. The checks make
# links only outside Windows.
if(NOT CMAKE_HOST_WIN32)
  file(GLOB_RECURSE work_entries LIST_DIRECTORIES true "${JH_WORK}/*")
  foreach(entry IN LISTS work_entries)
    if(IS_SYMLINK "${entry}")
      file(READ_SYMLINK "${entry}" target)
      cmake_path(GET entry PARENT_PATH holder)
      cmake_path(ABSOLUTE_PATH target BASE_DIRECTORY "${holder}" NORMALIZE)
      cmake_path(IS_PREFIX target "${holder}" NORMALIZE encloses)
      if(encloses)
        message(FATAL_ERROR "${entry} links to ${target}, a directory that holds the link")
      endif()
    endif()
  endforeach()
endif()
