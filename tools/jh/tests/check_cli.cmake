cmake_minimum_required(VERSION 3.22)
if(NOT EXISTS "${JH}")
  message(FATAL_ERROR "Build jh before running its CLI checks")
endif()
file(REMOVE_RECURSE "${JH_WORK}")
file(MAKE_DIRECTORY "${JH_WORK}/cwd")
set(input "${JH_WORK}/dane ż.json")
set(output "${JH_WORK}/generated/result.json")
file(WRITE "${input}" "{\"a/b\":[18446744073709551615,\"ż\"],\"~1\":true}\n")

include("${CMAKE_CURRENT_LIST_DIR}/cli_helpers.cmake")

invoke(0 --help)
if(NOT stdout MATCHES "json inspect" OR NOT stderr STREQUAL "")
  message(FATAL_ERROR "Help is missing or writes to stderr")
endif()
invoke(0 json inspect --help --json)
status(HAL_OK)
invoke(0 --version --json)
status(HAL_OK)
# Unsupported majors must fail before writing the requested output.
invoke(0 --version --json --json-version 1)
status(HAL_OK)
invoke(2 json inspect --file "${input}" --output "${output}" --json --json-version 2)
status(HAL_EUNSUPPORTED)
if(EXISTS "${output}")
  message(FATAL_ERROR "Unsupported JSON version published output")
endif()
invoke(2 --version --json-version 1)
invoke(2 --version --json --json-version -1)
status(HAL_EINVAL)
# Keep Git's tracked/untracked/ignored selection, order and raw NUL bytes.
find_program(git NAMES git REQUIRED)
include("${CMAKE_CURRENT_LIST_DIR}/repository_fixture.cmake")
set(repository "${JH_WORK}/repository ż")
jh_repository_fixture("${git}" "${repository}" "${JH_WORK}/git.raw")
execute_process(COMMAND "${JH}" repository files --root "${repository}" --git "${git}"
  OUTPUT_FILE "${JH_WORK}/jh.raw" COMMAND_ERROR_IS_FATAL ANY)
jh_expect_listing("${JH_WORK}/git.raw" "${JH_WORK}/jh.raw" "Native repository files changed Git's binary output")
invoke(0 repository files --root "${repository}" --git "${git}" --json --json-version 1)
status(HAL_OK)
string(JSON paths_count LENGTH "${stdout}" paths)
string(JSON first GET "${stdout}" paths 0)
string(JSON exit_code GET "${stdout}" exitCode)
if(NOT paths_count EQUAL 3 OR NOT first STREQUAL "untracked file" OR NOT exit_code EQUAL 0)
  message(FATAL_ERROR "Repository selection or process result changed: ${stdout}")
endif()
invoke(4 repository files --root "${repository}" --git "${git}" --max-bytes 0 --json)
status(HAL_EOVERFLOW)
invoke(3 repository files --root "${repository}" --git "${repository}/absent" --json)
status(HAL_ENOENT)
string(JSON native_error GET "${stdout}" nativeError)
if(native_error EQUAL 0)
  message(FATAL_ERROR "Missing executable lost its native error: ${stdout}")
endif()
file(MAKE_DIRECTORY "${JH_WORK}/not-repository")
file(WRITE "${JH_WORK}/not-repository/.git" "gitdir: missing\n")
invoke(128 repository files --root "${JH_WORK}/not-repository" --git "${git}" --json)
# Git's own failure is returned unchanged (normally 128).
set(schema "${JH_WORK}/schema ż.json")
file(WRITE "${schema}" "{\"properties\":{\"~1\":{\"$ref\":\"rules.json#/$defs/flag\"}}}")
file(WRITE "${JH_WORK}/rules.json" "{\"$defs\":{\"flag\":{\"type\":\"boolean\"}}}")
invoke(0 json validate --file "${input}" --schema "${schema}" --json)
status(HAL_OK)
file(WRITE "${JH_WORK}/invalid.json" "{\"~1\":0}")
invoke(2 json validate --file "${JH_WORK}/invalid.json" --schema "${schema}" --json)
status(HAL_ECONFIG)
string(JSON pointer GET "${stdout}" errors 0 instancePointer)
string(JSON rule GET "${stdout}" errors 0 schemaPointer)
string(JSON reason GET "${stdout}" errors 0 reason)
string(JSON rule_uri GET "${stdout}" errors 0 schemaUri)
if(NOT pointer STREQUAL "/~01" OR NOT rule STREQUAL "/$defs/flag/type" OR NOT reason STREQUAL "type")
  message(FATAL_ERROR "Schema report lost the instance or referenced rule: ${stdout}")
endif()
if(NOT rule_uri MATCHES "/cli-check%20%23%25%20%C5%BC/rules[.]json$")
  message(FATAL_ERROR "Schema identity did not escape the actual UTF-8 filename: ${stdout}")
endif()
invoke(2 json validate --file "${input}" --json)
status(HAL_EINVAL)
invoke(2 json validate --file "${input}" --schema "${schema}" --output ignored --json)
status(HAL_EINVAL)
invoke(2 json inspect --file "${input}" --schema "${schema}" --json)
status(HAL_EINVAL)
invoke(0 json inspect --file "${input}" --pointer /a~1b/0)
if(NOT stdout STREQUAL "18446744073709551615\n")
  message(FATAL_ERROR "The number token changed: ${stdout}")
endif()
invoke(0 --json json inspect --file "${input}" --pointer=/~01)
status(HAL_OK)
string(JSON value GET "${stdout}" value)
if(NOT value)
  message(FATAL_ERROR "The escaped member did not resolve")
endif()
invoke(0 json inspect --file "../dane ż.json" --json)
status(HAL_OK)
if(NOT stdout MATCHES "18446744073709551615" OR NOT stdout MATCHES "ż")
  message(FATAL_ERROR "Machine output lost input bytes")
endif()
invoke(1 json inspect --file "${input}" --pointer /a~1b/0 --output "${output}" --check --json)
status(HAL_ESTATE)
if(EXISTS "${JH_WORK}/generated")
  message(FATAL_ERROR "Check mode created the output directory")
endif()
invoke(0 json inspect --file "${input}" --pointer /a~1b/0 --output "${output}" --json)
status(HAL_OK)
file(READ "${output}" actual)
if(NOT actual STREQUAL "18446744073709551615\n")
  message(FATAL_ERROR "Published bytes differ: ${actual}")
endif()
file(TIMESTAMP "${output}" before "%s")
invoke(0 json inspect --file "${input}" --pointer /a~1b/0 --output "${output}" --json)
status(HAL_OK)
string(JSON changed GET "${stdout}" changed)
file(TIMESTAMP "${output}" after "%s")
if(changed OR NOT before STREQUAL after)
  message(FATAL_ERROR "An identical output was rewritten")
endif()
invoke(0 json inspect --file "${input}" --pointer /a~1b/0 --output "${output}" --check --json)
status(HAL_OK)
file(WRITE "${output}" "old\n")
invoke(1 json inspect --file "${input}" --pointer /a~1b/0 --output "${output}" --check --json)
status(HAL_ESTATE)
file(READ "${output}" actual)
if(NOT actual STREQUAL "old\n")
  message(FATAL_ERROR "Check mode changed an outdated output")
endif()
foreach(pointer /a~1b/4294967296 /a~1b/18446744073709551616 /missing)
  invoke(2 json inspect --file "${input}" --pointer "${pointer}" --json)
  status(HAL_ENOENT)
  string(JSON reported GET "${stdout}" pointer)
  if(NOT reported STREQUAL pointer)
    message(FATAL_ERROR "The error lost its JSON Pointer: ${stdout}")
  endif()
endforeach()
invoke(2 json inspect --file "${input}" --pointer /~ --json)
status(HAL_EPROTO)
invoke(3 json inspect --file "${JH_WORK}/missing.json" --json)
status(HAL_ENOENT)
invoke(4 json inspect --file "${input}" --max-bytes=2 --json)
status(HAL_EOVERFLOW)
foreach(arguments "json;inspect;--unknown" "json;inspect;--check" "json;inspect;--file"
    "json;inspect;--file;${input};--check" "json;inspect;--file;${input};--file;${input}"
    "json;inspect;--file;${input};--max-bytes=-1" "json;inspect;--file;${input};--max-bytes=67108865"
    "json;inspect;--file;${input};--max-bytes=18446744073709551616" "other" "json;inspect;extra")
  invoke(2 ${arguments} --json)
  status(HAL_EINVAL)
endforeach()
set(bad "${JH_WORK}/bad.json")
file(WRITE "${bad}" "{\n\"x\": ]}")
invoke(2 json inspect --file "${bad}" --json)
status(HAL_EPROTO)
string(JSON line GET "${stdout}" line)
string(JSON column GET "${stdout}" column)
if(NOT line EQUAL 2 OR NOT column EQUAL 6 OR NOT stderr MATCHES ":2:6:")
  message(FATAL_ERROR "Wrong source location: ${stdout} ${stderr}")
endif()
file(WRITE "${bad}" "{\"a\":0,\"\\u0061\":1}")
invoke(2 json inspect --file "${bad}" --json)
status(HAL_EPROTO)
string(JSON reason GET "${stdout}" jsonError)
if(NOT reason EQUAL 4)
  message(FATAL_ERROR "Duplicate-key diagnostic lost: ${stdout}")
endif()
string(ASCII 255 invalid)
file(WRITE "${bad}" "\"${invalid}\"")
invoke(2 json inspect --file "${bad}" --json)
status(HAL_EPROTO)
string(REPEAT "[" 257 opening)
string(REPEAT "]" 257 closing)
file(WRITE "${bad}" "${opening}0${closing}")
invoke(4 json inspect --file "${bad}" --json)
status(HAL_EOVERFLOW)
# The device defaults generator reproduces the tracked header byte for byte,
# publishes it elsewhere, reports a missing or edited copy and rejects a type
# catalogue with the specification's code and JSON Pointer.
file(READ "${JH_HAL_SOURCE}/config/tooling/artifacts.json" artifacts)
string(JSON defaults_header GET "${artifacts}" generatedArtifacts deviceDefaultsHeader)
invoke(0 generate device-defaults --root "${JH_HAL_SOURCE}" --check --json)
status(HAL_OK)
string(JSON changed GET "${stdout}" changed)
string(JSON root GET "${stdout}" root)
if(changed OR root MATCHES "[/\\\\]$")
  message(FATAL_ERROR "The tracked header differs from the generator or the root kept a separator: ${stdout}")
endif()
set(defaults_root "${JH_WORK}/defaults ż")
invoke(1 generate device-defaults --root "${JH_HAL_SOURCE}" --output-root "${defaults_root}" --check --json)
status(HAL_ESTATE)
if(NOT stderr MATCHES "missing generated artifact" OR EXISTS "${defaults_root}")
  message(FATAL_ERROR "A missing header was not reported, or check mode wrote: ${stderr}")
endif()
invoke(0 generate device-defaults --root "${JH_HAL_SOURCE}" --output-root "${defaults_root}" --write --json)
status(HAL_OK)
file(READ "${JH_HAL_SOURCE}/${defaults_header}" expected_header)
file(READ "${defaults_root}/${defaults_header}" produced_header)
if(NOT produced_header STREQUAL expected_header)
  message(FATAL_ERROR "The published header differs from the tracked one")
endif()
invoke(0 generate device-defaults --root "${JH_HAL_SOURCE}" --output-root "${defaults_root}" --write --json)
status(HAL_OK)
string(JSON changed GET "${stdout}" changed)
if(changed)
  message(FATAL_ERROR "An identical header was rewritten")
endif()
file(APPEND "${defaults_root}/${defaults_header}" "/* edit */\n")
invoke(1 generate device-defaults --root "${JH_HAL_SOURCE}" --output-root "${defaults_root}" --check --json)
status(HAL_ESTATE)
if(NOT stderr MATCHES "stale generated artifact" OR NOT stderr MATCHES "\\+\\+\\+ generated:" OR NOT stderr MATCHES "\n-/\\* edit \\*/\n")
  message(FATAL_ERROR "The stale header diff is missing: ${stderr}")
endif()
file(READ "${defaults_root}/${defaults_header}" edited_header)
if(NOT edited_header MATCHES "/\\* edit \\*/\n$")
  message(FATAL_ERROR "Check mode changed the edited header")
endif()
file(COPY "${JH_HAL_SOURCE}/config/hardware/bindings/" DESTINATION "${JH_WORK}/types")
file(RENAME "${JH_WORK}/types/nxp-pcf8574.json" "${JH_WORK}/types/pcf8574.json")
invoke(2 generate device-defaults --root "${JH_HAL_SOURCE}" --bindings "${JH_WORK}/types"
  --output-root "${JH_WORK}/types-out" --check --json)
status(HAL_ECONFIG)
string(JSON code GET "${stdout}" code)
string(JSON reported GET "${stdout}" pointer)
string(JSON source GET "${stdout}" source)
if(NOT code STREQUAL "JH-HW-BINDING" OR NOT reported STREQUAL "/compatible" OR NOT source MATCHES "pcf8574[.]json$")
  message(FATAL_ERROR "The catalogue error lost its code, source or pointer: ${stdout}")
endif()
foreach(arguments "generate;device-defaults;--check" "generate;device-defaults;--root;${JH_HAL_SOURCE}"
    "generate;device-defaults;--root;${JH_HAL_SOURCE};--check;--write"
    "generate;device-defaults;--root;${JH_HAL_SOURCE};--check;--file;${input}"
    "generate;device-defaults;--root;${JH_HAL_SOURCE};--check;--max-bytes=16"
    "generate;device-defaults;extra;--root;${JH_HAL_SOURCE};--check")
  invoke(2 ${arguments} --json)
  status(HAL_EINVAL)
endforeach()
invoke(3 generate device-defaults --root "${JH_WORK}/no such root" --check --json)
status(HAL_ENOENT)
# The feature registry generator reproduces both tracked artifacts, publishes
# them elsewhere with mode 0644 whatever the umask, reports a missing or edited
# copy and rejects an implies cycle with the Python generator's message.
string(JSON features_header GET "${artifacts}" generatedArtifacts featureHeader)
string(JSON features_cmake GET "${artifacts}" generatedArtifacts featureCmake)
invoke(0 generate features --root "${JH_HAL_SOURCE}" --check --json)
status(HAL_OK)
string(JSON changed GET "${stdout}" changed)
string(JSON command GET "${stdout}" command)
if(NOT changed EQUAL 0 OR NOT command STREQUAL "generate.features")
  message(FATAL_ERROR "The tracked feature artifacts differ from the generator: ${stdout}")
endif()
set(features_root "${JH_WORK}/features ż")
invoke(1 generate features --root "${JH_HAL_SOURCE}" --output-root "${features_root}" --check --json)
status(HAL_ESTATE)
string(JSON changed GET "${stdout}" changed)
if(NOT changed EQUAL 2 OR NOT stderr MATCHES "missing generated artifact" OR EXISTS "${features_root}")
  message(FATAL_ERROR "Missing feature artifacts were not reported, or check mode wrote: ${stdout} ${stderr}")
endif()
set(publish_features "${JH}" generate features --root "${JH_HAL_SOURCE}" --output-root "${features_root}" --write --json)
if(UNIX)
  set(publish_features sh -c "umask 077 && exec \"$0\" \"$@\"" ${publish_features})
endif()
execute_process(COMMAND ${publish_features} WORKING_DIRECTORY "${JH_WORK}/cwd"
  RESULT_VARIABLE code OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr ENCODING UTF-8)
status(HAL_OK)
string(JSON changed GET "${stdout}" changed)
if(NOT code EQUAL 0 OR NOT changed EQUAL 2)
  message(FATAL_ERROR "Feature artifacts were not published: ${code} ${stdout} ${stderr}")
endif()
foreach(artifact "${features_header}" "${features_cmake}")
  file(READ "${JH_HAL_SOURCE}/${artifact}" expected_artifact)
  file(READ "${features_root}/${artifact}" produced_artifact)
  if(NOT produced_artifact STREQUAL expected_artifact)
    message(FATAL_ERROR "The published ${artifact} differs from the tracked one")
  endif()
endforeach()
function(expect_feature_modes)
  if(NOT UNIX)
    return()
  endif()
  foreach(artifact "${features_header}" "${features_cmake}")
    execute_process(COMMAND stat -c %a "${features_root}/${artifact}" OUTPUT_VARIABLE mode
      OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(NOT mode STREQUAL "644")
      message(FATAL_ERROR "${artifact} has mode ${mode}, expected 644")
    endif()
  endforeach()
endfunction()
expect_feature_modes()
invoke(0 generate features --root "${JH_HAL_SOURCE}" --output-root "${features_root}" --write --json)
string(JSON changed GET "${stdout}" changed)
if(NOT changed EQUAL 0)
  message(FATAL_ERROR "Identical feature artifacts were rewritten: ${stdout}")
endif()
if(UNIX)
  # Identical contents with other permission bits count as a change.
  file(CHMOD "${features_root}/${features_cmake}" PERMISSIONS OWNER_READ OWNER_WRITE)
  invoke(0 generate features --root "${JH_HAL_SOURCE}" --output-root "${features_root}" --write --json)
  string(JSON changed GET "${stdout}" changed)
  if(NOT changed EQUAL 1)
    message(FATAL_ERROR "A permission-only difference was not repaired: ${stdout}")
  endif()
  expect_feature_modes()
endif()
file(APPEND "${features_root}/${features_header}" "/* edit */\n")
invoke(1 generate features --root "${JH_HAL_SOURCE}" --output-root "${features_root}" --check)
if(NOT stderr MATCHES "stale generated artifact" OR NOT stderr MATCHES "\\+\\+\\+ generated:${features_header}"
    OR NOT stderr MATCHES "\n-/\\* edit \\*/\n")
  message(FATAL_ERROR "The stale feature header diff is missing: ${stderr}")
endif()
file(READ "${features_root}/${features_header}" edited_header)
if(NOT edited_header MATCHES "/\\* edit \\*/\n$")
  message(FATAL_ERROR "Check mode changed the edited feature header")
endif()
set(cycle_config "${JH_WORK}/config ż")
file(COPY "${JH_HAL_SOURCE}/config/features" "${JH_HAL_SOURCE}/config/features.schema.json"
  DESTINATION "${cycle_config}")
file(READ "${cycle_config}/features/buses.json" buses)
string(JSON buses SET "${buses}" symbols HAL_ENABLE_SPI implies "[\"HAL_ENABLE_HC595\"]")
file(WRITE "${cycle_config}/features/buses.json" "${buses}")
invoke(2 generate features --root "${JH_HAL_SOURCE}" --config-root "${cycle_config}"
  --output-root "${JH_WORK}/cycle-out" --write --json)
status(HAL_ECONFIG)
if(NOT stderr MATCHES "feature registry: implies cycle: [^\n]*HAL_ENABLE_HC595"
    OR NOT stderr MATCHES "HAL_ENABLE_SPI" OR EXISTS "${JH_WORK}/cycle-out")
  message(FATAL_ERROR "The implies cycle was not rejected before writing: ${stderr}")
endif()
# An artifact path of artifacts.json never leads outside the output root: the
# generators reject it before writing anything.
set(escape_root "${JH_WORK}/escape root")
file(MAKE_DIRECTORY "${escape_root}/config/tooling")
file(WRITE "${escape_root}/victim.h" "victim\n")
foreach(field featureHeader featureCmake deviceDefaultsHeader)
  foreach(bad "../victim.h" "${escape_root}/victim.h" "generated/../../victim.h" "..\\\\victim.h"
      "C:victim.h")
    string(JSON escaped SET "${artifacts}" generatedArtifacts ${field} "\"${bad}\"")
    file(WRITE "${escape_root}/config/tooling/artifacts.json" "${escaped}")
    if(field STREQUAL "deviceDefaultsHeader")
      invoke(2 generate device-defaults --root "${escape_root}" --output-root "${escape_root}/out" --write --json)
    else()
      invoke(2 generate features --root "${escape_root}" --config-root "${JH_HAL_SOURCE}/config"
        --output-root "${escape_root}/out" --write --json)
    endif()
    status(HAL_ECONFIG)
    file(READ "${escape_root}/victim.h" victim)
    if(NOT stderr MATCHES "generatedArtifacts[.]${field} must be repository-relative"
        OR NOT victim STREQUAL "victim\n" OR EXISTS "${escape_root}/out")
      message(FATAL_ERROR "${field} ${bad} was not rejected before writing: ${stderr}")
    endif()
  endforeach()
endforeach()
# feature list gives the registry the generator writes, with closures;
# feature resolve one build's resolution, findings and provenance.
invoke(0 feature list --root "${JH_HAL_SOURCE}" --json)
status(HAL_OK)
set(registry "${stdout}")
string(JSON registry_digest GET "${registry}" registryDigest)
file(READ "${JH_HAL_SOURCE}/${features_header}" tracked_header)
if(NOT tracked_header MATCHES "JH_HAL_FEATURE_REGISTRY_DIGEST \"${registry_digest}\"")
  message(FATAL_ERROR "feature list and the generated header disagree on the registry digest")
endif()
string(JSON feature_count LENGTH "${registry}" features)
math(EXPR last_feature "${feature_count} - 1")
set(implying "")
foreach(index RANGE ${last_feature})
  string(JSON implies_count LENGTH "${registry}" features ${index} implies)
  string(JSON kind GET "${registry}" features ${index} kind)
  if(implies_count GREATER 0 AND kind STREQUAL "feature")
    string(JSON implying GET "${registry}" features ${index} name)
    string(JSON closure GET "${registry}" features ${index} closure)
    string(JSON direct GET "${registry}" features ${index} implies 0)
    if(NOT closure MATCHES "\"${direct}\"")
      message(FATAL_ERROR "The closure of ${implying} lacks ${direct}: ${closure}")
    endif()
    break()
  endif()
endforeach()
invoke(0 feature list --config-root "${JH_HAL_SOURCE}/config")
if(NOT stdout MATCHES "(^|\n)${implying} feature [a-z-]+\n")
  message(FATAL_ERROR "feature list text lacks ${implying}: ${stdout}")
endif()
invoke(0 feature resolve --root "${JH_HAL_SOURCE}" --request ${implying})
if(NOT stdout MATCHES "(^|\n)${direct}\n" OR NOT stderr STREQUAL "")
  message(FATAL_ERROR "feature resolve did not resolve what ${implying} implies: ${stdout} ${stderr}")
endif()
invoke(1 feature resolve --root "${JH_HAL_SOURCE}" --context "ctx ż"
  --request ${implying} --source "h ż.h:3" --request HAL_ENABLE_NOT_REGISTERED --source "h ż.h:4"
  --request "${implying}=0" --source "h ż.h:5" --request ${implying} --source "nowhere"
  --target "t ż" --required-feature "${direct}=1" --required-feature "${implying}" --json)
status(HAL_ECONFIG)
json_strings(requested requestedFeatures)
json_strings(sources provenance ${implying})
json_strings(required_sources provenance ${direct})
json_strings(resolved_names resolvedFeatures)
list(JOIN resolved_names "\n" resolved_text)
string(SHA256 resolved_digest "${resolved_text}")
string(JSON reported_digest GET "${stdout}" resolvedFeaturesDigest)
json_strings(finding_list findings)
list(JOIN finding_list "\n" findings)
foreach(expected
    "h ż.h:4: \\[JH-CFG-UNKNOWN\\] unknown feature HAL_ENABLE_NOT_REGISTERED"
    "h ż.h:5: \\[JH-CFG-VALUE\\] ${implying}=0 is unsupported"
    "ctx ż: \\[JH-CFG-REDUNDANT\\] ${implying} is requested 2 times by h ż.h:3, nowhere")
  if(NOT findings MATCHES "${expected}" OR NOT stderr MATCHES "${expected}")
    message(FATAL_ERROR "feature resolve lacks the finding ${expected}: ${findings} ${stderr}")
  endif()
endforeach()
if(NOT requested STREQUAL "${implying}"
    OR NOT sources STREQUAL "h ż.h:3;nowhere;target:t ż:requiredFeatures[1]"
    OR NOT "target:t ż:requiredFeatures[0]" IN_LIST required_sources
    OR NOT direct IN_LIST resolved_names
    OR NOT resolved_digest STREQUAL reported_digest)
  message(FATAL_ERROR "feature resolve requested, provenance or digest is wrong: ${stdout}")
endif()
invoke(2 feature resolve --config-root "${cycle_config}" --request ${implying} --json)
status(HAL_ECONFIG)
if(NOT stderr MATCHES "feature registry: implies cycle")
  message(FATAL_ERROR "feature resolve accepted a registry with an implies cycle: ${stderr}")
endif()
# feature lint checks the requests of every header and manifest below the
# input roots, skipping build trees and tests/hardware unless asked;
# config manifest checks one manifest as jh-vscode reads it.
set(lint_root "${JH_WORK}/lint ż")
file(WRITE "${lint_root}/app/hal_project_config.h"
  "#define ${implying}\n#define HAL_ENABLE_NOT_REGISTERED\n#define ${implying} 0\n"
  "#define JH_PROJECT_VARIANTS(X) X(V, \"v\", ${direct}=2)\n")
file(WRITE "${lint_root}/app/.vscode/jaszczurhal.project.json"
  "{\"variants\": [], \"cmake\": {\"cache\": {\"JH_VARIANT\": \"x\", \"${implying}\": 1}}}\n")
file(WRITE "${lint_root}/build-out/hal_project_config.h" "#define HAL_ENABLE_SKIPPED\n")
file(WRITE "${lint_root}/tests/hardware/hal_project_config.h" "#define HAL_ENABLE_FIXTURE\n")
set(app_header "${lint_root}/app/hal_project_config.h")
set(app_manifest "${lint_root}/app/.vscode/jaszczurhal.project.json")
invoke(1 feature lint --root "${JH_HAL_SOURCE}" --input-root "${lint_root}")
slashed(lint_text "${stderr}")
foreach(expected
    "error: ${app_header}:2: \\[JH-CFG-UNKNOWN\\] unknown feature HAL_ENABLE_NOT_REGISTERED"
    "error: ${app_header}:3: \\[JH-CFG-VALUE\\] ${implying}=0 is unsupported"
    "error: ${app_header}:JH_PROJECT_VARIANTS\\(V\\): \\[JH-CFG-VALUE\\] ${direct} has unsupported value '2'"
    "error: ${app_manifest}: \\[JH-CFG-MANIFEST\\] 'variants' is not a manifest field"
    "error: ${app_manifest}: cmake.cache.JH_VARIANT: \\[JH-CFG-MANIFEST\\] the manifest does not pick a variant"
    "error: ${app_manifest}: cmake.cache.${implying}: \\[JH-CFG-MANIFEST\\] project configuration belongs"
    "linted 2 feature inputs: 6 finding\\(s\\)")
  if(NOT lint_text MATCHES "${expected}")
    message(FATAL_ERROR "feature lint lacks ${expected}: ${stderr}")
  endif()
endforeach()
if(stderr MATCHES "SKIPPED|FIXTURE")
  message(FATAL_ERROR "feature lint read a build tree or tests/hardware: ${stderr}")
endif()
invoke(0 feature lint --root "${JH_HAL_SOURCE}" --input-root "${lint_root}" --report-only
  --include-hardware-fixtures --json)
status(HAL_OK)
# Findings hold semicolons, so count them as JSON.
string(JSON lint_input_count LENGTH "${stdout}" inputs)
string(JSON lint_finding_count LENGTH "${stdout}" findings)
if(NOT lint_input_count EQUAL 3 OR NOT lint_finding_count EQUAL 7
    OR NOT stderr MATCHES "warning: [^\n]*HAL_ENABLE_FIXTURE")
  message(FATAL_ERROR "feature lint --include-hardware-fixtures --report-only is wrong (${lint_input_count} inputs, ${lint_finding_count} findings): ${stderr}")
endif()
invoke(2 feature lint --root "${JH_HAL_SOURCE}" --input-root "${lint_root}/nothing" --json)
status(HAL_ENOENT)
if(NOT stderr MATCHES "lint input root does not exist")
  message(FATAL_ERROR "A missing lint root was not reported: ${stderr}")
endif()
file(WRITE "${lint_root}/commented.json" "// a comment\n{\"example\": 1}\n")
invoke(1 config manifest --file "${lint_root}/commented.json" --label "m ż" --json)
status(HAL_ECONFIG)
json_strings(manifest_findings findings)
if(NOT manifest_findings MATCHES "^m ż: \\[JH-CFG-MANIFEST\\] 'example' is not a manifest field")
  message(FATAL_ERROR "config manifest did not read the commented manifest: ${stdout}")
endif()
invoke(0 config manifest --file "${lint_root}/absent.json" --json)
status(HAL_OK)
string(JSON manifest_exists GET "${stdout}" exists)
if(manifest_exists)
  message(FATAL_ERROR "A missing manifest was reported as present: ${stdout}")
endif()
foreach(arguments "feature;list" "feature;resolve;--request;${implying}"
    "feature;resolve;--root;${JH_HAL_SOURCE};--request;${implying};--source;a;--source;b"
    "feature;resolve;--root;${JH_HAL_SOURCE};--required-feature;${direct}"
    "feature;list;--root;${JH_HAL_SOURCE};--request;${implying}"
    "feature;list;--root;${JH_HAL_SOURCE};--config-root="
    "feature;lint;--config-root;${JH_HAL_SOURCE}/config"
    "feature;lint;--root;${JH_HAL_SOURCE};--request;${implying}"
    "config;manifest" "config;manifest;--file;x;--input-root;y")
  invoke(2 ${arguments} --json)
  status(HAL_EINVAL)
endforeach()
# --file gives the requests and sources whole: this source alone outgrows the
# 32767 characters of a Windows command line.
string(REPEAT "p" 40000 long_directory)
set(long_source "/${long_directory}/h ż.h:7")
set(escaped_source "esc\\aped \"q\" ż:2")
set(request_file "${JH_WORK}/requests ż.json")
file(WRITE "${request_file}" "{\"requests\": [
  {\"request\": \"${implying}\", \"source\": \"${long_source}\"},
  {\"source\": \"esc\\\\aped \\\"q\\\" \\u017c:2\", \"request\": \"HAL_ENABLE_NOT_REGISTERED=1\"}]}")
invoke(1 feature resolve --root "${JH_HAL_SOURCE}" --file "${request_file}" --json)
status(HAL_ECONFIG)
json_strings(sources provenance ${implying})
json_strings(requested requestedFeatures)
json_strings(finding_list findings)
if(NOT sources STREQUAL long_source OR NOT requested STREQUAL implying
    OR NOT "${escaped_source}: [JH-CFG-UNKNOWN] unknown feature HAL_ENABLE_NOT_REGISTERED"
    IN_LIST finding_list)
  message(FATAL_ERROR "feature resolve --file lost a request or source: ${stdout}")
endif()
# A registry failure does not name the request file it never read.
invoke(2 feature resolve --config-root "${cycle_config}" --file "${request_file}" --json)
string(JSON source_name ERROR_VARIABLE source_error GET "${stdout}" source)
if(NOT stderr MATCHES "implies cycle" OR NOT source_error)
  message(FATAL_ERROR "feature resolve named the request file for a registry failure: ${stdout}")
endif()
foreach(text [=[[]]=] [=[{"requests": {}}]=] [=[{"requests": [], "other": 1}]=]
    [=[{"requests": [{"request": "HAL_ENABLE_X"}]}]=]
    [=[{"requests": [{"request": "HAL_ENABLE_X", "source": 1}]}]=]
    [=[{"requests": [{"request": "HAL_ENABLE_X", "source": "s", "other": 1}]}]=])
  file(WRITE "${JH_WORK}/bad-requests.json" "${text}")
  invoke(2 feature resolve --root "${JH_HAL_SOURCE}" --file "${JH_WORK}/bad-requests.json" --json)
  status(HAL_ETYPE)
  string(JSON source_name GET "${stdout}" source)
  slashed(source_name "${source_name}")
  if(NOT source_name STREQUAL "${JH_WORK}/bad-requests.json")
    message(FATAL_ERROR "feature resolve accepted the request file ${text}: ${stdout}")
  endif()
endforeach()
invoke(3 feature resolve --root "${JH_HAL_SOURCE}" --file "${JH_WORK}/missing.json" --json)
status(HAL_ENOENT)
foreach(arguments "--file;${request_file};--request;${implying}" "--file=")
  invoke(2 feature resolve --root "${JH_HAL_SOURCE}" ${arguments} --json)
  status(HAL_EINVAL)
endforeach()
foreach(arguments "generate;features;--check" "generate;features;--root;${JH_HAL_SOURCE}"
    "generate;features;--root;${JH_HAL_SOURCE};--check;--write"
    "generate;features;--root;${JH_HAL_SOURCE};--check;--bindings;${JH_WORK}"
    "generate;features;extra;--root;${JH_HAL_SOURCE};--check")
  invoke(2 ${arguments} --json)
  status(HAL_EINVAL)
endforeach()
file(GLOB_RECURSE leftovers "${JH_WORK}/*.tmp-*")
if(leftovers)
  message(FATAL_ERROR "Temporary files leaked: ${leftovers}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${JH_BUILD}" --config "${JH_CONFIG}"
  --component jh --prefix "${JH_WORK}/installed" RESULT_VARIABLE install_status
  OUTPUT_VARIABLE install_output ERROR_VARIABLE install_error)
if(NOT install_status EQUAL 0)
  message(FATAL_ERROR "CLI installation failed: ${install_output}${install_error}")
endif()
file(RENAME "${JH_WORK}/installed" "${JH_WORK}/relocated prefix")
foreach(license pcre2/LICENCE.md JaszczurHAL/LICENSE jhbase/LICENSE ryu/LICENSE-Apache2 ryu/LICENSE-Boost
    utf8proc/LICENSE.md)
  if(NOT EXISTS "${JH_WORK}/relocated prefix/share/jh/licenses/${license}")
    message(FATAL_ERROR "Installed CLI lacks license ${license}")
  endif()
endforeach()
set(JH "${JH_WORK}/relocated prefix/bin/jh${JH_SUFFIX}")
invoke(0 json inspect --file "${input}" --pointer=/~01 --json)
status(HAL_OK)
invoke(0 json validate --file "${input}" --schema "${schema}" --json)
status(HAL_OK)
invoke(0 generate device-defaults --root "${JH_HAL_SOURCE}" --check --json)
status(HAL_OK)
set(checkout "${JH_WORK}/dependency check")
# HAL must select its own utf8proc checkout: preparing one inside the managed
# jhbase tree would make that dependency fail its source verification.
set(unicode_source "${JH_BUILD}/unicode consumer ż")
file(REMOVE_RECURSE "${unicode_source}")
file(MAKE_DIRECTORY "${unicode_source}")
file(WRITE "${unicode_source}/CMakeLists.txt"
  "cmake_minimum_required(VERSION 3.22)\nproject(unicode_consumer C)\n"
  "include(\"${JH_HAL_SOURCE}/cmake/jh_jhbase.cmake\")\n"
  "set(JHBASE_UNICODE ON)\njh_add_jhbase()\n"
  "add_executable(unicode_consumer main.c)\n"
  "add_executable(process_fixture process.c)\n"
  "target_link_libraries(process_fixture PRIVATE jb::base)\n"
  "if(MSVC)\n  target_compile_options(process_fixture PRIVATE /W4 /WX /utf-8)\nendif()\n"
  "target_link_libraries(unicode_consumer PRIVATE jb::unicode)\n"
  "enable_testing()\nadd_test(NAME nfc COMMAND unicode_consumer)\n")
file(WRITE "${unicode_source}/main.c" [=[
#include <jb/array.h>
#include <jb/unicode.h>
#include <string.h>
int main(void) {
    const char input[] = "e\xCC\x81";
    const char expected[] = "\xC3\xA9";
    int32_t scratch[8];
    size_t required = 0, length = 0;
    const char *text = NULL;
    if (jb_unicode_nfc_measure(input, COUNTOF(input) - 1, 64, &required) != HAL_OK ||
        required > COUNTOF(scratch))
        return 1;
    if (jb_unicode_nfc(input, COUNTOF(input) - 1, scratch, COUNTOF(scratch), 64,
                       &text, &length) != HAL_OK)
        return 2;
    return length != COUNTOF(expected) - 1 ||
           memcmp(text, expected, COUNTOF(expected) - 1) != 0;
}
]=])
# A stalled external tool must be stopped and reaped before the CLI returns.
file(WRITE "${unicode_source}/process.c" [=[
#include <jb/array.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
  wchar_t *ready = NULL;
  size_t ready_length = 0;
  if (_wdupenv_s(&ready, &ready_length, L"JH_CLI_PROCESS_READY")) return 20;
#else
int main(int argc, char **argv) {
  const char *ready = getenv("JH_CLI_PROCESS_READY");
#endif
  const char *expected[] = {"-C", "", "ls-files", "--cached", "--others", "--exclude-standard", "-z"};
  if (argc != (int)COUNTOF(expected) + 1) return 19;
  for (int i = 1; i < argc; ++i) {
    if (i == 2) continue;
    size_t j = 0;
    while (expected[i - 1][j] && argv[i][j] == expected[i - 1][j]) ++j;
    if (argv[i][j] || expected[i - 1][j]) return 19;
  }
  if (!ready) {
#ifdef _WIN32
    FILE *cwd = NULL;
    if (fopen_s(&cwd, "process-fixture-cwd", "w")) return 24;
#else
    FILE *cwd = fopen("process-fixture-cwd", "w");
#endif
    if (!cwd || fclose(cwd)) return 24;
    fwrite("a\0", 1, 2, stdout);
    fputs("fixture stderr\n", stderr);
    return 23;
  }
#ifdef _WIN32
  FILE *file = NULL;
  const int open_error = _wfopen_s(&file, ready, L"w");
  free(ready);
  if (open_error) return 20;
#else
  FILE *file = fopen(ready, "w");
#endif
  if (!file) return 20;
#ifdef _WIN32
  fprintf(file, "%lu", (unsigned long)GetCurrentProcessId());
#else
  fprintf(file, "%ld", (long)getpid());
#endif
  if (fclose(file)) return 21;
#ifdef _WIN32
  Sleep(30000);
#else
  sleep(30);
#endif
  return 22;
}
]=])
set(unicode_generator -G "${JH_GENERATOR}")
if(NOT JH_PLATFORM STREQUAL "")
  list(APPEND unicode_generator -A "${JH_PLATFORM}")
endif()
if(NOT JH_TOOLSET STREQUAL "")
  list(APPEND unicode_generator -T "${JH_TOOLSET}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${unicode_generator} -S "${unicode_source}"
    -B "${unicode_source}/.build/invalid" -DBUILD_TESTING=OFF
    -DJHBASE_UTF8PROC_DIR:PATH=OFF
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(result EQUAL 0 OR NOT output MATCHES "must be an absolute path")
  message(FATAL_ERROR "Explicit invalid utf8proc path was not rejected: ${output}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${unicode_generator} -S "${unicode_source}"
    -B "${unicode_source}/.build/consumer" -DBUILD_TESTING=OFF
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "HAL Unicode configure failed: ${output}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${unicode_source}/.build/consumer"
    --config "${JH_CONFIG}" --target unicode_consumer process_fixture
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "HAL Unicode build failed: ${output}")
endif()
find_program(ctest NAMES ctest REQUIRED)
execute_process(COMMAND "${ctest}" --test-dir "${unicode_source}/.build/consumer"
    -C "${JH_CONFIG}" --output-on-failure
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "HAL Unicode consumer failed: ${output}")
endif()
set(process_fixture "${unicode_source}/.build/consumer/process_fixture${JH_SUFFIX}")
if(NOT EXISTS "${process_fixture}")
  set(process_fixture "${unicode_source}/.build/consumer/${JH_CONFIG}/process_fixture${JH_SUFFIX}")
endif()
invoke(23 repository files --root "${repository}" --git "${process_fixture}" --json)
status(HAL_EIO)
string(JSON child_exit GET "${stdout}" exitCode)
if(NOT child_exit EQUAL 23 OR NOT stderr MATCHES "fixture stderr"
    OR NOT EXISTS "${JH_WORK}/cwd/process-fixture-cwd")
  message(FATAL_ERROR "Child exit or stderr was lost: ${stdout} ${stderr}")
endif()
set(ENV{JH_CLI_PROCESS_READY} "${JH_WORK}/process-ready")
invoke(124 repository files --root "${repository}" --git "${process_fixture}" --timeout-ms 1000 --json)
status(HAL_ETIMEOUT)
if(NOT EXISTS "$ENV{JH_CLI_PROCESS_READY}")
  message(FATAL_ERROR "Timeout did not exercise a running child")
endif()
unset(ENV{JH_CLI_PROCESS_READY})
if(UNIX)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  file(WRITE "${JH_WORK}/cancel.py" [=[
import json, os, signal, subprocess, sys, time
from pathlib import Path
jh, child, root, work = sys.argv[1:]
ready = Path(work) / "cancel-ready"
env = dict(os.environ, JH_CLI_PROCESS_READY=str(ready))
command = [jh, "repository", "files", "--root", root, "--git", child, "--json"]
with subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
    try:
        deadline = time.monotonic() + 10
        while not ready.exists() or ready.stat().st_size == 0:
            if process.poll() is not None or time.monotonic() >= deadline:
                raise AssertionError("child did not start")
            time.sleep(0.01)
        child_pid = int(ready.read_text())
        process.send_signal(signal.SIGINT)
        output, errors = process.communicate(timeout=10)
        assert process.returncode == 130, (process.returncode, output, errors)
        assert json.loads(output)["status"] == "HAL_ECANCELED", output
        try:
            os.kill(child_pid, 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError("SIGINT left the child alive")
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()
# Invalid UTF-8 and LF in POSIX filenames remain exact bytes in raw mode.
raw_name = os.fsencode(root) + b"/raw-\xff\n"
with open(raw_name, "wb") as stream:
    stream.write(b"x")
try:
    expected = subprocess.check_output([os.environ["JH_CLI_GIT"],
        "-C", root, "ls-files", "--cached", "--others", "--exclude-standard", "-z"])
    actual = subprocess.check_output([jh, 'repository', 'files', '--root', root, '--git', os.environ['JH_CLI_GIT']])
    assert actual == expected, (actual, expected)
finally:
    os.unlink(raw_name)
]=])
  set(ENV{JH_CLI_GIT} "${git}")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${JH_WORK}/cancel.py"
      "${JH}" "${process_fixture}" "${repository}" "${JH_WORK}"
    COMMAND_ERROR_IS_FATAL ANY)
  unset(ENV{JH_CLI_GIT})
endif()
file(MAKE_DIRECTORY "${checkout}/cmake" "${checkout}/third_party")
file(COPY "${JH_HAL_SOURCE}/cmake/jh_verify_pcre2.cmake" "${JH_HAL_SOURCE}/cmake/jh_jhbase.cmake"
  DESTINATION "${checkout}/cmake")
file(COPY "${JH_HAL_SOURCE}/third_party/pcre2_version.conf" DESTINATION "${checkout}/third_party")
file(COPY "${JH_PCRE2}/" DESTINATION "${checkout}/third_party/pcre2")
find_program(git NAMES git REQUIRED)
set(dependency "${checkout}/third_party/pcre2")
function(check_pcre2 expected_error)
  execute_process(COMMAND "${CMAKE_COMMAND}" "-DJH_VERIFY_JHBASE_DIR=${JH_JHBASE}"
      -P "${checkout}/cmake/jh_verify_pcre2.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
  if(expected_error STREQUAL "")
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "Clean PCRE2 verification failed: ${output}")
    endif()
  elseif(result EQUAL 0 OR NOT output MATCHES "${expected_error}")
    message(FATAL_ERROR "Expected PCRE2 rejection ${expected_error}: ${result}, ${output}")
  endif()
endfunction()
check_pcre2("")
execute_process(COMMAND "${git}" -C "${dependency}" remote get-url origin
  OUTPUT_VARIABLE origin OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${git}" -C "${dependency}" remote set-url origin https://example.invalid/wrong.git
  COMMAND_ERROR_IS_FATAL ANY)
check_pcre2("Wrong origin")
execute_process(COMMAND "${git}" -C "${dependency}" remote set-url origin "${origin}"
  COMMAND_ERROR_IS_FATAL ANY)
file(APPEND "${dependency}/src/pcre2_compile.c" "\n/* hidden mutation */\n")
execute_process(COMMAND "${git}" -C "${dependency}" update-index --assume-unchanged src/pcre2_compile.c
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${git}" --no-optional-locks -C "${dependency}" status --porcelain
  OUTPUT_VARIABLE dirty COMMAND_ERROR_IS_FATAL ANY)
if(NOT dirty STREQUAL "")
  message(FATAL_ERROR "PCRE2 mutation must be hidden from status for this regression")
endif()
check_pcre2("source bytes differ")
file(COPY "${JH_PCRE2}/src/pcre2_compile.c" DESTINATION "${dependency}/src")
check_pcre2("")
message(STATUS "jh CLI: PASS")
