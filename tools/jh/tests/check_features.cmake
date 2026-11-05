cmake_minimum_required(VERSION 3.22)
# The feature registry commands of jh on edited copies of the HAL
# configuration: what generation depends on, the registries it rejects, the
# rules requires and conflicts add to the artifacts, resolution and the raw
# lint of project headers and manifests. The facts of the tracked registry are
# checked by tests/test_feature_registry.cmake. At the end, the pin-set bound
# and the unique capability bits of the board registry on an edited copy of
# boards/.
if(NOT EXISTS "${JH}" OR NOT IS_DIRECTORY "${JH_HAL_SOURCE}/config/features")
  message(FATAL_ERROR "jh_features needs JH and JH_HAL_SOURCE")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/cli_helpers.cmake")
file(REMOVE_RECURSE "${JH_WORK}")
file(MAKE_DIRECTORY "${JH_WORK}/cwd")
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
