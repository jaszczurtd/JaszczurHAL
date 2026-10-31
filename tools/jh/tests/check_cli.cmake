cmake_minimum_required(VERSION 3.22)
if(NOT EXISTS "${JH}")
  message(FATAL_ERROR "Build jh before running its CLI checks")
endif()
file(REMOVE_RECURSE "${JH_WORK}")
file(MAKE_DIRECTORY "${JH_WORK}/cwd")
set(input "${JH_WORK}/dane ż.json")
set(output "${JH_WORK}/generated/result.json")
file(WRITE "${input}" "{\"a/b\":[18446744073709551615,\"ż\"],\"~1\":true}\n")

function(invoke expected)
  execute_process(COMMAND "${JH}" ${ARGN} WORKING_DIRECTORY "${JH_WORK}/cwd"
    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err ENCODING UTF-8)
  if(NOT code STREQUAL "${expected}")
    message(FATAL_ERROR "jh ${ARGN}: expected exit ${expected}, got ${code}\n${out}\n${err}")
  endif()
  set(stdout "${out}" PARENT_SCOPE)
  set(stderr "${err}" PARENT_SCOPE)
endfunction()

function(status expected)
  string(JSON format ERROR_VARIABLE error GET "${stdout}" formatVersion)
  if(error OR NOT format EQUAL 1)
    message(FATAL_ERROR "stdout is not one versioned JSON result: ${stdout}")
  endif()
  string(JSON actual GET "${stdout}" status)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Expected ${expected}, got ${stdout}")
  endif()
endfunction()

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
set(repository "${JH_WORK}/repository ż")
file(MAKE_DIRECTORY "${repository}")
execute_process(COMMAND "${git}" init -q "${repository}" COMMAND_ERROR_IS_FATAL ANY)
file(WRITE "${repository}/.gitignore" "ignored\n")
file(WRITE "${repository}/tracked ż" "tracked")
execute_process(COMMAND "${git}" -C "${repository}" add .gitignore "tracked ż"
  COMMAND_ERROR_IS_FATAL ANY)
file(WRITE "${repository}/untracked file" "untracked")
file(WRITE "${repository}/ignored" "ignored")
execute_process(COMMAND "${git}" -C "${repository}" ls-files --cached --others --exclude-standard -z
  OUTPUT_FILE "${JH_WORK}/git.raw" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${JH}" repository files --root "${repository}" --git "${git}"
  OUTPUT_FILE "${JH_WORK}/jh.raw" COMMAND_ERROR_IS_FATAL ANY)
file(SHA256 "${JH_WORK}/git.raw" expected_raw)
file(SHA256 "${JH_WORK}/jh.raw" actual_raw)
if(NOT actual_raw STREQUAL expected_raw)
  message(FATAL_ERROR "Native repository files changed Git's binary output")
endif()
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
