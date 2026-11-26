cmake_minimum_required(VERSION 3.22)
# jh compile --dry-run: the model and plan of a VS Code firmware build on a
# board path fixture (target profile, tokens, CMake cache arguments, USB
# identity, variant, debug tree, recorded cache keys, stale cache, Windows
# buildRoot and the Python3_EXECUTABLE policy), its configuration errors and
# the device tree and ESP-IDF builds of examples/01_core_runtime. Expected
# values come from the fixture and the tracked registry, read here with
# string(JSON). jh compile --configure-only then runs this CMake with the
# test's generator on a source tree without languages: environment and
# arguments of the configure, whole lines of its interleaved output, the key
# record, forgotten keys, the reset of a stale cache and the failures of the
# step. A build then runs the fixture's firmware target: publication from
# the CMake tree also after a build without work, invalidated artifacts and
# the linker diagnostics of a failed build, the build lock between two runs,
# and the preflight that stops an STM32G474 network build before CMake.
if(NOT EXISTS "${JH}" OR NOT IS_DIRECTORY "${JH_HAL_SOURCE}/boards" OR
   NOT JH_GENERATOR)
  message(FATAL_ERROR "jh_compile needs JH, JH_HAL_SOURCE and JH_GENERATOR")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/cli_helpers.cmake")
file(REMOVE_RECURSE "${JH_WORK}")
file(MAKE_DIRECTORY "${JH_WORK}/cwd")
get_filename_component(root "${JH_HAL_SOURCE}" REALPATH)
get_filename_component(work "${JH_WORK}" REALPATH)
set(project "${work}/proj")
# No host environment unless a check gives one: a missing file reads as {}.
unset(ENV{JH_TEST_WINDOWS_HOST})
set(ENV{JH_WINDOWS_HOST_ENVIRONMENT} "${work}/no-host-environment.json")
unset(ENV{JH_COMPONENT_PYTHON})
unset(ENV{JH_PICOTOOL_EXECUTABLE})
unset(ENV{CMAKE_C_COMPILER_LAUNCHER})
unset(ENV{CMAKE_CXX_COMPILER_LAUNCHER})
# jh passes itself to the configure as JH_EXECUTABLE.
unset(ENV{JH_EXECUTABLE})
unset(ENV{JH_EXECUTABLE_ROOT})
set(ENV{NO_COLOR} 1)
unset(ENV{FORCE_COLOR})
# The fixtures build no ELF objdump could read.
set(ENV{JH_VSCODE_MEMORY_OVERVIEW} 0)

# A Python executable reached through a link resolves to its target.
file(MAKE_DIRECTORY "${work}/bin")
if(CMAKE_HOST_WIN32)
  set(python "${work}/bin/python-real.exe")
  file(WRITE "${python}" "")
  set(python_given "${python}")
else()
  set(python "${work}/bin/python-real")
  file(WRITE "${python}" "#!/bin/sh\nexit 0\n")
  file(CHMOD "${python}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
  set(python_given "${work}/bin/python-link")
  file(CREATE_LINK "${python}" "${python_given}" SYMBOLIC)
endif()

function(write_project manifest settings)
  file(MAKE_DIRECTORY "${project}/.vscode")
  file(WRITE "${project}/.vscode/jaszczurhal.project.json" "${manifest}")
  file(WRITE "${project}/.vscode/settings.json" "${settings}")
endfunction()
set(settings "{\n  // JSONC, as VS Code writes it\n  \"jaszczurhal.root\": \"${root}\",\n  \"jaszczurhal.verbose\": false\n}\n")
set(manifest [=[{
  "project": "Fixture",
  "module": "app",
  "toolchain": "cmake",
  "target": "rp2040",
  "board": "pico",
  "buildDir": "${project}/.build",
  "cmakeBuildDir": "${buildDir}/cmake",
  "cmake": {
    "sourceDir": "${jhRoot}/cmake/jh_firmware_project",
    "cache": {
      "JH_PROJECT_DIR": "${project}",
      "JH_MODULE_NAME": "app",
      "FLAG_ON": true,
      "FLAG_OFF": false,
      "SKIPPED": null
    }
  },
  "identity": {"enabled": true, "usbManufacturer": "Maker", "usbProduct": "Fixture",
               "usbVid": "2E8A", "usbPid": 10}
}
]=])
write_project("${manifest}" "${settings}")
file(WRITE "${project}/hal_project_config.h"
  "#define JH_PROJECT_VARIANTS(X) X(EXTRA, \"Extra build\", FIXTURE_EXTRA = 1)\n")

# A plan array with the \\ separators of Windows paths turned into /.
function(plan_list variable)
  json_strings(items plan ${ARGN})
  slashed(items "${items}")
  set(${variable} "${items}" PARENT_SCOPE)
endfunction()
function(has item list what)
  if(NOT "${item}" IN_LIST list)
    message(FATAL_ERROR "${what}: missing ${item} in ${list}")
  endif()
endfunction()
function(text_at expected what)
  string(JSON actual GET "${stdout}" ${ARGN})
  slashed(actual "${actual}")
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "${what}: got '${actual}', expected '${expected}'")
  endif()
endfunction()
set(base_args compile --root "${root}" --project "${project}" --python "${python_given}")

# The registry facts of the fixture's target.
file(READ "${root}/boards/targets/rp2040.json" target_json)
string(JSON registry_vid ERROR_VARIABLE no_usb GET "${target_json}" usbDevice vid)

# --- Board path ---
invoke(0 ${base_args} --dry-run --json)
status(HAL_OK)
text_at("rp2040" "target" config target)
text_at("pico" "board" config board)
text_at("${project}/.build" "buildDir" config buildDir)
text_at("${project}/.build/cmake" "cmakeBuildDir" config cmakeBuildDir)
text_at(".vscode/jaszczurhal.project.json" "target source" config _sources target)
text_at("registry:rp2040.build.provider" "toolchain source" config _sources toolchain)
text_at(".vscode/settings.json" "root source" config _sources root)
text_at("${project}" "project dir" config _projectDir)
text_at("${root}/cmake/jh_firmware_project" "source tree" plan sourceDir)
text_at("${project}/.build/cmake/rp2040/pico" "CMake tree" plan cmakeBuildDir)
text_at("${project}/.build" "artifacts" plan artifactDir)
text_at("${project}/.build/.jh-build.lock" "lock" plan lockFile)
text_at("${root}" "jh root" plan configureEnvironment JH_EXECUTABLE_ROOT)
string(JSON reset ERROR_VARIABLE no_reset GET "${stdout}" plan resetCache)
if(NOT no_reset)
  message(FATAL_ERROR "a fresh tree has no cache to reset: ${reset}")
endif()
plan_list(configure configure)
list(GET configure 0 program)
if(NOT program STREQUAL "cmake")
  message(FATAL_ERROR "configure runs ${program}")
endif()
foreach(item "-S" "-B" "-G" "Ninja" "-DFLAG_ON=ON" "-DFLAG_OFF=OFF" "-DJH_TARGET=rp2040"
             "-DJH_BOARD=pico" "-DJH_MODULE_NAME=app" "-DJH_ROOT=${root}"
             "-DJH_PROJECT_DIR=${project}" "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
             "-DPython3_EXECUTABLE=${python}"
             "-DJH_PICOTOOL_EXECUTABLE=${root}/.build/tools/picotool/picotool")
  if(CMAKE_HOST_WIN32 AND item MATCHES "picotool")
    continue()
  endif()
  has("${item}" "${configure}" "configure")
endforeach()
foreach(item IN LISTS configure)
  if(item MATCHES "^-DSKIPPED")
    message(FATAL_ERROR "a null cache value reached the configure: ${item}")
  endif()
endforeach()
# A target with a USB device takes the identity of the manifest.
if(NOT no_usb)
  foreach(item "-DJH_USB_MANUFACTURER=Maker" "-DJH_USB_PRODUCT=Fixture" "-DJH_USB_VID=11914"
               "-DJH_USB_PID=10")
    has("${item}" "${configure}" "identity")
  endforeach()
endif()
# Without a key record every transient key is forgotten, in name order.
set(removed "")
foreach(item IN LISTS configure)
  if(item MATCHES "^-U")
    list(APPEND removed "${item}")
  endif()
endforeach()
set(transient JH_ARTIFACT_DIR JH_EXTRA_INCLUDES JH_EXTRA_LIBRARIES JH_EXTRA_SOURCES
  JH_LINK_LIBRARIES JH_OTA_GENERATION JH_OTA_VERSION JH_PROJECT_RECIPE JH_USB_MANUFACTURER
  JH_USB_PID JH_USB_PRODUCT JH_USB_VID JH_VARIANT)
list(TRANSFORM transient PREPEND "-U")
if(NOT removed STREQUAL transient)
  message(FATAL_ERROR "forgotten keys: ${removed}\nexpected: ${transient}")
endif()
plan_list(keys cacheKeys)
set(expected_keys FLAG_OFF FLAG_ON JH_BOARD JH_MODULE_NAME JH_PROJECT_DIR JH_TARGET
  PICO_SDK_PATH)
if(NOT keys STREQUAL expected_keys)
  message(FATAL_ERROR "recorded keys: ${keys}")
endif()
plan_list(build build)
slashed(build "${build}")
if(NOT build STREQUAL "cmake;--build;${project}/.build/cmake/rp2040/pico;--target;firmware")
  message(FATAL_ERROR "build: ${build}")
endif()
plan_list(artifacts artifacts)
if(NOT artifacts STREQUAL "firmware.elf;firmware.bin;firmware.hex;firmware.map;firmware.uf2")
  message(FATAL_ERROR "artifacts: ${artifacts}")
endif()
# The text form prints the commands as jh-vscode --verbose does.
invoke(0 ${base_args} --dry-run)
contains("${stdout}" "+ cmake -S " "text plan")
contains("${stdout}" "+ cmake --build " "text plan")

# --- Recorded keys and a stale cache ---
set(tree "${project}/.build/cmake/rp2040/pico")
file(MAKE_DIRECTORY "${tree}")
file(WRITE "${tree}/.jh-vscode-cache-keys.json" "[\"FLAG_ON\", \"OLD_KEY\", \"JH_VARIANT\"]\n")
file(WRITE "${tree}/CMakeCache.txt"
  "CMAKE_HOME_DIRECTORY:INTERNAL=${work}/elsewhere\nCMAKE_GENERATOR:INTERNAL=Unix Makefiles\n")
invoke(0 ${base_args} --dry-run --json)
plan_list(configure configure)
set(removed "")
foreach(item IN LISTS configure)
  if(item MATCHES "^-U")
    list(APPEND removed "${item}")
  endif()
endforeach()
if(NOT removed STREQUAL "-UJH_VARIANT;-UOLD_KEY")
  message(FATAL_ERROR "forgotten recorded keys: ${removed}")
endif()
text_at("Unix Makefiles" "cached generator" plan resetCache cachedGenerator)
text_at("${work}/elsewhere" "cached source" plan resetCache cachedSource)
text_at("ON" "reset in the project tree" plan resetCache allowed)
file(WRITE "${tree}/CMakeCache.txt"
  "CMAKE_HOME_DIRECTORY:INTERNAL=${root}/cmake/jh_firmware_project\nCMAKE_GENERATOR:INTERNAL=Ninja\n")
invoke(0 ${base_args} --dry-run --json)
string(JSON reset ERROR_VARIABLE no_reset GET "${stdout}" plan resetCache)
if(NOT no_reset)
  message(FATAL_ERROR "a current cache is kept: ${reset}")
endif()
file(REMOVE_RECURSE "${project}/.build")

# --- Debug tree ---
invoke(0 ${base_args} --debug --dry-run --json)
text_at("${project}/.build/cmake/rp2040/pico/debug" "debug tree" plan cmakeBuildDir)
plan_list(configure configure)
has("-DCMAKE_BUILD_TYPE=Debug" "${configure}" "debug configure")
plan_list(build build)
list(GET build 4 target_name)
if(NOT target_name STREQUAL "firmware_debug")
  message(FATAL_ERROR "debug build target: ${target_name}")
endif()

# --- Variant ---
invoke(0 ${base_args} --variant EXTRA --dry-run --json)
text_at("app_EXTRA" "variant module" config module)
text_at("${project}/.build/variants/EXTRA" "variant buildDir" config buildDir)
text_at("${project}/.build/cmake/variants/EXTRA" "variant cmakeBuildDir" config cmakeBuildDir)
text_at("cli" "variant source" config _sources variant)
text_at("${project}/.build/cmake/variants/EXTRA/rp2040/pico" "variant tree" plan cmakeBuildDir)
plan_list(configure configure)
foreach(item "-DJH_VARIANT=EXTRA" "-DJH_MODULE_NAME=app_EXTRA"
             "-DJH_ARTIFACT_DIR=${project}/.build/variants/EXTRA")
  has("${item}" "${configure}" "variant configure")
endforeach()
invoke(2 ${base_args} --variant NOPE --dry-run --json)
text_at("unknown variant 'NOPE'; known variants: EXTRA" "unknown variant" message)

# --- Python3_EXECUTABLE from JH_COMPONENT_PYTHON ---
set(ENV{JH_COMPONENT_PYTHON} "${python_given}")
invoke(0 compile --root "${root}" --project "${project}" --dry-run --json)
plan_list(configure configure)
has("-DPython3_EXECUTABLE=${python}" "${configure}" "JH_COMPONENT_PYTHON")
set(ENV{JH_COMPONENT_PYTHON} "${work}/bin/missing-python")
invoke(2 compile --root "${root}" --project "${project}" --dry-run --json)
string(JSON message GET "${stdout}" message)
contains("${message}" "Python3_EXECUTABLE" "missing interpreter")
unset(ENV{JH_COMPONENT_PYTHON})

# --- Windows buildRoot: the variant has its own tree there ---
file(MAKE_DIRECTORY "${work}/tools/cmake/bin" "${work}/tools/ninja")
file(WRITE "${work}/host-environment.json" "{\"buildRoot\": \"${work}/winroot\", \"tools\": {\"cmake\": \"${work}/tools/cmake/bin/cmake\", \"ninja\": \"${work}/tools/ninja/ninja\", \"git\": \"\"}}\n")
set(ENV{JH_TEST_WINDOWS_HOST} 1)
set(ENV{JH_WINDOWS_HOST_ENVIRONMENT} "${work}/host-environment.json")
# The tree key hashes the project path in ntpath.normcase form on Windows.
set(normalized "${project}")
set(separator ":")
if(CMAKE_HOST_WIN32)
  string(REPLACE "/" "\\" normalized "${normalized}")
  string(TOLOWER "${normalized}" normalized)
  set(separator ";")
endif()
string(SHA256 digest "${normalized}")
string(SUBSTRING "${digest}" 0 12 digest)
set(key "proj-${digest}")
invoke(0 ${base_args} --dry-run --json)
text_at("${work}/winroot/${key}/cmake/rp2040/pico" "buildRoot tree" plan cmakeBuildDir)
text_at("${work}/winroot" "managed root" plan configureEnvironment JH_MANAGED_BUILD_ROOT)
string(JSON path GET "${stdout}" plan configureEnvironment PATH)
slashed(path "${path}")
string(FIND "${path}" "${work}/tools/cmake/bin${separator}${work}/tools/ninja${separator}" at)
if(NOT at EQUAL 0)
  message(FATAL_ERROR "tool directories lead PATH: ${path}")
endif()
plan_list(configure configure)
list(GET configure 0 program)
if(NOT program STREQUAL "${work}/tools/cmake/bin/cmake")
  message(FATAL_ERROR "configure runs ${program}")
endif()
has("-DCMAKE_MAKE_PROGRAM=${work}/tools/ninja/ninja" "${configure}" "ninja")
invoke(0 ${base_args} --variant EXTRA --dry-run --json)
text_at("${work}/winroot/${key}/cmake/variants/EXTRA/rp2040/pico" "buildRoot variant tree"
  plan cmakeBuildDir)
unset(ENV{JH_TEST_WINDOWS_HOST})
set(ENV{JH_WINDOWS_HOST_ENVIRONMENT} "${work}/no-host-environment.json")

# --- Configuration errors ---
invoke(2 ${base_args} --dry-run --configure-only --json)
text_at("compile takes --dry-run or --configure-only, not both" "both modes" message)
invoke(2 ${base_args} --board nucleo-g474re --dry-run --json)
string(JSON message GET "${stdout}" message)
contains("${message}" "[JH-CFG-BOARD] cli: board 'nucleo-g474re' belongs to stm32g474, not to target rp2040; rp2040 boards: "
  "board of another target")
invoke(2 ${base_args} --assembly pico --dry-run --json)
string(JSON message GET "${stdout}" message)
slashed(message "${message}")
contains("${message}" "[JH-HW-ASSEMBLY] --assembly: assembly 'pico' needs ${project}/device_tree.json"
  "assembly without a device tree")
string(REPLACE "\"SKIPPED\": null" "\"HAL_ENABLE_WIFI\": \"1\"" with_feature "${manifest}")
write_project("${with_feature}" "${settings}")
invoke(2 ${base_args} --dry-run --json)
string(JSON message GET "${stdout}" message)
contains("${message}" "[JH-CFG-MANIFEST]" "feature in the manifest")
string(REPLACE "\"2E8A\"" "\"zzzz\"" bad_vid "${manifest}")
write_project("${bad_vid}" "${settings}")
invoke(2 ${base_args} --dry-run --json)
text_at("invalid USB identity configuration: usbVid must be an integer or a USB hexadecimal identifier"
  "invalid USB ID" message)
write_project("${manifest}" "{\"jaszczurhal.root\": \"${work}\"}\n")
invoke(2 ${base_args} --dry-run --json)
string(JSON message GET "${stdout}" message)
contains("${message}" "[JH-HAL-ROOT] .vscode/settings.json: jaszczurhal.root is " "foreign root")
invoke(2 compile --root "${root}" --project "${work}/missing" --dry-run --json)
string(JSON message GET "${stdout}" message)
contains("${message}" "project directory does not exist: " "missing project")

# --- Device tree: buildDir of targetProfiles and the local assembly ---
set(dt "${work}/dt")
file(MAKE_DIRECTORY "${dt}/.vscode")
file(COPY "${root}/examples/01_core_runtime/device_tree.json"
  "${root}/examples/01_core_runtime/hal_project_config.h" DESTINATION "${dt}")
file(WRITE "${dt}/.vscode/settings.json" "${settings}")
file(WRITE "${dt}/.vscode/jaszczurhal.project.json" [=[{
  "project": "Fixture", "module": "dt", "toolchain": "cmake", "assembly": "pico",
  "buildDir": "${project}/.build", "cmakeBuildDir": "${buildDir}/cmake",
  "targetProfiles": {"rp2040": {"buildDir": "${project}/custom"}},
  "cmake": {"sourceDir": "${jhRoot}/cmake/jh_firmware_project",
            "cache": {"JH_PROJECT_DIR": "${project}"}}
}
]=])
set(dt_args compile --root "${root}" --project "${dt}" --python "${python_given}")
invoke(0 ${dt_args} --dry-run --json)
text_at("${dt}/custom/rp2040/pico" "targetProfiles buildDir" config buildDir)
text_at("${dt}/custom/rp2040/pico" "assembly root" config _assemblyBuildDir)
text_at("${dt}/custom/rp2040/pico/cmake" "tree in the assembly directory" plan cmakeBuildDir)
string(JSON kept ERROR_VARIABLE dropped GET "${stdout}" config targetProfiles rp2040 buildDir)
if(NOT dropped)
  message(FATAL_ERROR "the targetProfiles buildDir stays in the model: ${kept}")
endif()
text_at("device_tree.json assemblies.pico (.vscode/jaszczurhal.project.json)"
  "manifest assembly" config _sources assembly)
file(WRITE "${dt}/.vscode/jaszczurhal.local.json" "{\"assembly\": \"pico2-arm\"}\n")
invoke(0 ${dt_args} --dry-run --json)
text_at("rp2350-arm" "local assembly target" config target)
text_at("device_tree.json assemblies.pico2-arm (.vscode/jaszczurhal.local.json)"
  "local assembly" config _sources assembly)
text_at("${dt}/.build/rp2350-arm/pico2-arm" "default base" config buildDir)
file(WRITE "${dt}/.vscode/jaszczurhal.local.json" "{\"assembly\": \"ghost\"}\n")
invoke(2 ${dt_args} --dry-run --json)
string(JSON message GET "${stdout}" message)
slashed(message "${message}")
contains("${message}" "[JH-HW-ASSEMBLY] ${dt}/device_tree.json:/: .vscode/jaszczurhal.local.json selects assembly 'ghost', which the device tree does not declare; known: ["
  "undeclared stored assembly")
invoke(0 ${dt_args} --assembly nucleo --dry-run --json)
text_at("stm32g474" "explicit assembly over the local one" config target)

# --- Device tree and ESP-IDF builds of an example ---
set(example "${root}/examples/01_core_runtime")
set(example_build "${root}/.build/examples/01_core_runtime")
invoke(0 compile --root "${root}" --project "${example}" --assembly pico --python "${python_given}"
  --dry-run --json)
text_at("pico" "assembly" config assembly)
text_at("rp2040" "assembly target" config target)
text_at("device_tree.json assemblies.pico (cli)" "assembly source" config _sources assembly)
text_at("${example_build}/rp2040/pico/cmake" "assembly tree" plan cmakeBuildDir)
plan_list(configure configure)
has("-DJH_ASSEMBLY=pico" "${configure}" "assembly configure")
foreach(item IN LISTS configure)
  if(item MATCHES "^-DJH_BOARD=")
    message(FATAL_ERROR "a device tree build names no board: ${item}")
  endif()
endforeach()
invoke(0 compile --root "${root}" --project "${example}" --assembly esp32s3-zero --dry-run --json)
text_at("esp-idf" "ESP toolchain" plan toolchain)
plan_list(esp build)
list(SUBLIST esp 1 -1 esp)
slashed(esp "${esp}")
set(expected esp build --root "${root}" --project "${example}" --target esp32s3 --assembly
  esp32s3-zero --output "${example_build}/esp32s3/esp32s3-zero")
if(NOT esp STREQUAL expected)
  message(FATAL_ERROR "jh esp build: ${esp}\nexpected: ${expected}")
endif()
# EXIT_UNSUPPORTED of jh-vscode, which its adapter passes on.
invoke(8 compile --root "${root}" --project "${example}" --assembly esp32s3-zero --debug --dry-run
  --json)
text_at("ESP-IDF debug builds are not implemented by jh-vscode" "ESP debug" message)
invoke(2 compile --root "${root}" --project "${example}" --assembly esp32s3-zero --configure-only
  --json)
string(JSON message GET "${stdout}" message)
contains("${message}" "--configure-only applies to CMake projects" "ESP configure")

# --- Configure ---
# The configure runs this CMake; the source tree records what it receives.
set(source "${work}/cfgsrc")
file(MAKE_DIRECTORY "${source}")
file(WRITE "${source}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.22)
project(compile_fixture NONE)
if(DEFINED ENV{FIXTURE_FAIL})
  message(FATAL_ERROR "fixture configure failure")
endif()
if(DEFINED ENV{FIXTURE_INTERLEAVE})
  foreach(i RANGE 1 200)
    message(STATUS "fixture out ${i}")
    message(NOTICE "fixture err ${i}")
  endforeach()
endif()
file(WRITE "${CMAKE_BINARY_DIR}/seen.txt"
  "JH_EXECUTABLE=$ENV{JH_EXECUTABLE}\nJH_EXECUTABLE_ROOT=$ENV{JH_EXECUTABLE_ROOT}\n"
  "Python3_EXECUTABLE=${Python3_EXECUTABLE}\nFLAG_ON=${FLAG_ON}\nSTALE_KEY=${STALE_KEY}\n")
message(STATUS "fixture configured")
# The firmware target writes its artifacts once; the map as the ARM linker
# names it.
set(outputs firmware.elf firmware.bin firmware.hex firmware.uf2 firmware.elf.map)
add_custom_command(OUTPUT ${outputs}
  COMMAND "${CMAKE_COMMAND}" "-DOUT=${CMAKE_BINARY_DIR}"
    -P "${CMAKE_CURRENT_LIST_DIR}/firmware.cmake"
  VERBATIM)
add_custom_target(firmware DEPENDS ${outputs})
]=])
file(WRITE "${source}/firmware.cmake" [=[
if(DEFINED ENV{FIXTURE_LOG})
  file(APPEND "$ENV{FIXTURE_LOG}" "start\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 2)
  file(APPEND "$ENV{FIXTURE_LOG}" "end\n")
endif()
if(DEFINED ENV{FIXTURE_OVERFLOW})
  message("ld: section `.text' will not fit in region `FLASH'")
  message("ld: region `FLASH' overflowed by 1234 bytes")
  message("ld: section `.data' will not fit in region `RAM'")
  message("ld: region `FLASH' overflowed by 1234 bytes")
  message(FATAL_ERROR "fixture link failure")
endif()
foreach(name elf bin hex uf2)
  file(WRITE "${OUT}/firmware.${name}" "fixture ${name}\n")
endforeach()
file(WRITE "${OUT}/firmware.elf.map" "fixture map\n")
]=])
set(cfg "${work}/cfg")
function(write_configure_project executable build_dir)
  file(MAKE_DIRECTORY "${cfg}/.vscode")
  file(WRITE "${cfg}/hal_project_config.h" "")
  file(WRITE "${cfg}/.vscode/settings.json" "${settings}")
  file(WRITE "${cfg}/.vscode/jaszczurhal.project.json" "{
  \"project\": \"Fixture\", \"module\": \"app\", \"toolchain\": \"cmake\",
  \"target\": \"rp2040\", \"board\": \"pico\",
  \"buildDir\": \"\${project}/.build\", \"cmakeBuildDir\": \"${build_dir}\",
  \"cmake\": {\"executable\": \"${executable}\", \"generator\": \"${JH_GENERATOR}\",
            \"sourceDir\": \"${source}\",
            \"cache\": {\"JH_PROJECT_DIR\": \"\${project}\", \"FLAG_ON\": true}}
}
")
endfunction()
write_configure_project("${CMAKE_COMMAND}" "\${buildDir}/cmake")
set(cfg_args compile --root "${root}" --project "${cfg}" --python "${python_given}")
set(cfg_tree "${cfg}/.build/cmake/rp2040/pico")
invoke(0 ${cfg_args} --dry-run --json)
plan_list(cfg_keys cacheKeys)
# json.dumps(keys, indent=2) and a newline, in text mode.
set(newline "\n")
if(CMAKE_HOST_WIN32)
  set(newline "\r\n")
endif()
set(record "[")
set(comma "")
foreach(item IN LISTS cfg_keys)
  string(APPEND record "${comma}${newline}  \"${item}\"")
  set(comma ",")
endforeach()
string(APPEND record "${newline}]${newline}")
# As bytes: file(READ) without HEX turns CRLF into LF on Windows.
string(HEX "${record}" record_hex)
function(require_record what)
  file(READ "${cfg_tree}/.jh-vscode-cache-keys.json" actual HEX)
  if(NOT actual STREQUAL record_hex)
    file(READ "${cfg_tree}/.jh-vscode-cache-keys.json" text)
    message(FATAL_ERROR "${what}: key record ${actual}\n${text}\nexpected ${record_hex}")
  endif()
endfunction()
function(seen key expected)
  file(STRINGS "${cfg_tree}/seen.txt" lines REGEX "^${key}=" ENCODING UTF-8)
  string(REPLACE "${key}=" "" actual "${lines}")
  slashed(actual "${actual}")
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "configure saw ${key}='${actual}', expected '${expected}'")
  endif()
endfunction()

# A fresh tree: CMake's output on stdout, the plan's arguments and
# environment in the configure, then the key record.
invoke(0 ${cfg_args} --configure-only)
contains("${stdout}" "fixture configured" "CMake output on stdout")
lacks("${stdout}" "+ ${CMAKE_COMMAND}" "a quiet configure")
require_record("fresh tree")
seen(JH_EXECUTABLE_ROOT "${root}")
seen(Python3_EXECUTABLE "${python}")
seen(FLAG_ON "ON")
file(STRINGS "${cfg_tree}/seen.txt" jh_line REGEX "^JH_EXECUTABLE=" ENCODING UTF-8)
string(REPLACE "JH_EXECUTABLE=" "" jh_seen "${jh_line}")
if(NOT EXISTS "${jh_seen}")
  message(FATAL_ERROR "JH_EXECUTABLE of the configure is not a file: '${jh_seen}'")
endif()
# With --json the result takes stdout: the echo and CMake go to stderr.
invoke(0 ${cfg_args} --configure-only --verbose --json)
status(HAL_OK)
slashed(echoed "${stderr}")
contains("${echoed}" "+ ${CMAKE_COMMAND} -S ${source} -B ${cfg_tree} -G ${JH_GENERATOR}"
  "verbose echo")
contains("${stderr}" "fixture configured" "CMake output on stderr")

# CMake writes a message to stderr in pieces while stdout carries the next
# one; each line still arrives whole, as through one pipe (stderr=STDOUT).
set(ENV{FIXTURE_INTERLEAVE} 1)
invoke(0 ${cfg_args} --configure-only)
unset(ENV{FIXTURE_INTERLEAVE})
string(REGEX MATCHALL "(-- fixture out|fixture err) [0-9]+\r?\n" ended "${stdout}")
string(REGEX MATCHALL "\n(-- fixture out|fixture err) [0-9]+" started "\n${stdout}")
list(LENGTH ended ended_count)
list(LENGTH started started_count)
if(NOT ended_count EQUAL 400 OR NOT started_count EQUAL 400)
  message(FATAL_ERROR "interleaved CMake output split its lines (${ended_count} and "
    "${started_count} of 400 whole):\n${stdout}")
endif()

# A recorded key the project no longer sets leaves the cache.
execute_process(COMMAND "${CMAKE_COMMAND}" -DSTALE_KEY=1 "${cfg_tree}"
  RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
file(READ "${cfg_tree}/CMakeCache.txt" cache)
if(NOT code EQUAL 0 OR NOT cache MATCHES "STALE_KEY:")
  message(FATAL_ERROR "the fixture could not set STALE_KEY: ${code}")
endif()
file(WRITE "${cfg_tree}/.jh-vscode-cache-keys.json" "[\"FLAG_ON\", \"STALE_KEY\"]\n")
invoke(0 ${cfg_args} --configure-only)
file(READ "${cfg_tree}/CMakeCache.txt" cache)
if(cache MATCHES "STALE_KEY:")
  message(FATAL_ERROR "-USTALE_KEY left the key in the cache")
endif()
seen(STALE_KEY "")
require_record("rewritten record")

# A cache of another source tree is reset inside the project, with a
# warning.
string(REGEX REPLACE "CMAKE_HOME_DIRECTORY:INTERNAL=[^\n]*"
  "CMAKE_HOME_DIRECTORY:INTERNAL=${work}/elsewhere" cache "${cache}")
file(WRITE "${cfg_tree}/CMakeCache.txt" "${cache}")
file(WRITE "${cfg_tree}/CMakeFiles/stale-marker" "")
invoke(0 ${cfg_args} --configure-only)
slashed(warning "${stderr}")
contains("${warning}" "warning: stale CMake cache contract changed; resetting ${cfg_tree} (source ${work}/elsewhere -> ${source}, generator ${JH_GENERATOR} -> ${JH_GENERATOR})"
  "reset warning")
if(EXISTS "${cfg_tree}/CMakeFiles/stale-marker")
  message(FATAL_ERROR "the reset kept CMakeFiles")
endif()
file(READ "${cfg_tree}/CMakeCache.txt" cache)
if(NOT cache MATCHES "CMAKE_HOME_DIRECTORY:INTERNAL=${source}")
  message(FATAL_ERROR "the reset cache names another source")
endif()

# A failed configure leaves the record of the last good one.
file(WRITE "${cfg_tree}/.jh-vscode-cache-keys.json" "MARKER\n")
set(ENV{FIXTURE_FAIL} 1)
invoke(5 ${cfg_args} --configure-only --json)
unset(ENV{FIXTURE_FAIL})
string(JSON message GET "${stdout}" message)
contains("${message}" "CMake configure failed with exit code " "configure failure")
contains("${stderr}" "fixture configure failure" "CMake error on stderr")
file(READ "${cfg_tree}/.jh-vscode-cache-keys.json" kept)
if(NOT kept STREQUAL "MARKER\n")
  message(FATAL_ERROR "a failed configure wrote the record: ${kept}")
endif()

# --- Build ---
set(cfg_out "${cfg}/.build")
function(require_published what)
  foreach(name elf bin hex uf2 map)
    set(expected "fixture ${name}\n")
    set(path "${cfg_out}/firmware.${name}")
    if(EXISTS "${path}")
      file(READ "${path}" actual)
    endif()
    if(NOT EXISTS "${path}" OR NOT actual STREQUAL expected)
      message(FATAL_ERROR "${what}: firmware.${name} is not the fixture's")
    endif()
  endforeach()
endfunction()
# A stale OTA image goes; every planned artifact comes from the tree.
file(WRITE "${cfg_out}/firmware.ota" "stale\n")
invoke(0 ${cfg_args})
require_published("build")
if(EXISTS "${cfg_out}/firmware.ota")
  message(FATAL_ERROR "a stale OTA image survived the build")
endif()
# A build without work publishes the files of the tree again.
file(REMOVE "${cfg_out}/firmware.bin" "${cfg_out}/firmware.uf2")
invoke(0 ${cfg_args} --json)
status(HAL_OK)
require_published("build without work")

# A failed link leaves no uploadable file and explains the overflow once,
# then the section of a region that did not overflow.
file(REMOVE "${cfg_tree}/firmware.elf")
set(ENV{FIXTURE_OVERFLOW} 1)
invoke(5 ${cfg_args} --json)
unset(ENV{FIXTURE_OVERFLOW})
string(JSON message GET "${stdout}" message)
contains("${message}" "CMake build failed with exit code " "build failure")
contains("${stderr}" "\nJaszczurHAL diagnostics:\n  - axis-3: firmware does not fit rp2040/pico region FLASH; linker reports overflow by 1.2 KiB (1234 bytes).\n  - axis-3: section .data will not fit rp2040/pico region RAM.\n"
  "linker diagnostics")
foreach(name elf bin hex uf2 map)
  if(EXISTS "${cfg_out}/firmware.${name}")
    message(FATAL_ERROR "a failed build kept firmware.${name}")
  endif()
endforeach()

# Two builds of one artifact directory at once: the second waits for the
# lock, so the firmware step never overlaps, and plans again, so a stale cache
# both found is reset once. execute_process starts its commands together;
# each runner keeps the output of its jh, so neither writes into the other's
# input.
file(READ "${cfg_tree}/CMakeCache.txt" cache)
string(REGEX REPLACE "CMAKE_HOME_DIRECTORY:INTERNAL=[^\n]*"
  "CMAKE_HOME_DIRECTORY:INTERNAL=${work}/elsewhere" cache "${cache}")
file(WRITE "${cfg_tree}/CMakeCache.txt" "${cache}")
set(runner "execute_process(COMMAND")
foreach(item "${JH}" ${cfg_args})
  string(APPEND runner " [==[${item}]==]")
endforeach()
string(APPEND runner " WORKING_DIRECTORY [==[${JH_WORK}/cwd]==] RESULT_VARIABLE code"
  " OUTPUT_VARIABLE out ERROR_VARIABLE err)\n"
  "file(WRITE \"\${RESULT}\" \"\${code}\\n\${out}\\n\${err}\")\n")
file(WRITE "${work}/run-build.cmake" "${runner}")
set(ENV{FIXTURE_LOG} "${work}/build-log.txt")
execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DRESULT=${work}/build-1.txt" -P "${work}/run-build.cmake"
  COMMAND "${CMAKE_COMMAND}" "-DRESULT=${work}/build-2.txt" -P "${work}/run-build.cmake"
  RESULTS_VARIABLE codes)
unset(ENV{FIXTURE_LOG})
file(READ "${work}/build-log.txt" log)
file(READ "${work}/build-1.txt" first)
file(READ "${work}/build-2.txt" second)
if(NOT codes STREQUAL "0;0" OR NOT first MATCHES "^0\n" OR NOT second MATCHES "^0\n" OR
   NOT log MATCHES "^(start\nend\n)+$")
  message(FATAL_ERROR "concurrent builds overlapped or failed:\n${log}\n${first}\n${second}")
endif()
string(REGEX MATCHALL "warning: stale CMake cache contract changed" resets "${first}${second}")
list(LENGTH resets reset_count)
if(NOT reset_count EQUAL 1)
  message(FATAL_ERROR "the stale cache was reset ${reset_count} times:\n${first}\n${second}")
endif()
require_published("concurrent builds")

# --- Build preflight: STM32G474 network modules need the CYW43 profile ---
set(pf "${work}/pf")
file(MAKE_DIRECTORY "${pf}/.vscode")
file(WRITE "${pf}/.vscode/settings.json" "${settings}")
file(WRITE "${pf}/.vscode/jaszczurhal.project.json" "{
  \"project\": \"Fixture\", \"module\": \"app\", \"toolchain\": \"cmake\",
  \"target\": \"stm32g474\", \"board\": \"nucleo-g474re\",
  \"buildDir\": \"\${project}/.build\", \"cmakeBuildDir\": \"\${buildDir}/cmake\",
  \"cmake\": {\"executable\": \"${CMAKE_COMMAND}\", \"generator\": \"${JH_GENERATOR}\",
            \"sourceDir\": \"${source}\", \"cache\": {\"JH_PROJECT_DIR\": \"\${project}\"}}
}
")
file(WRITE "${pf}/hal_project_config.h" "#define HAL_ENABLE_WIFI\n")
set(pf_args compile --root "${root}" --project "${pf}" --python "${python_given}")
set(preflight "axis-2: stm32g474 network modules require the explicit CYW43 gSPI/lwIP backend profile; enabled modules: HAL_ENABLE_WIFI. Define HAL_NETWORK_BACKEND_CYW43, HAL_CYW43_BUS_STM32_GSPI and HAL_CYW43_STACK_LWIP, or disable those modules.")
invoke(0 ${pf_args} --dry-run --json)
plan_list(found preflight)
if(NOT found STREQUAL preflight)
  message(FATAL_ERROR "preflight: ${found}")
endif()
invoke(5 ${pf_args})
contains("${stderr}" "\nJaszczurHAL diagnostics:\n  - ${preflight}\n" "preflight diagnostics")
if(EXISTS "${pf}/.build/cmake")
  message(FATAL_ERROR "the preflight let CMake configure")
endif()
file(WRITE "${pf}/hal_project_config.h" "#define HAL_ENABLE_WIFI\n#define HAL_NETWORK_BACKEND_CYW43\n#define HAL_CYW43_BUS_STM32_GSPI\n#define HAL_CYW43_STACK_LWIP\n")
invoke(0 ${pf_args} --dry-run --json)
string(JSON count LENGTH "${stdout}" plan preflight)
if(NOT count EQUAL 0)
  message(FATAL_ERROR "the CYW43 profile still stops the build: ${stdout}")
endif()

# A missing program.
write_configure_project("jh-no-such-cmake" "\${buildDir}/cmake")
invoke(5 ${cfg_args} --configure-only --json)
text_at("command not found: jh-no-such-cmake" "missing program" message)

# A stale cache outside the managed roots stays; the step stops. The tree
# lies in the temporary directory, which must be outside the project and the
# checkout's .build.
if(CMAKE_HOST_WIN32)
  set(temporary "$ENV{TEMP}")
elseif(DEFINED ENV{TMPDIR} AND IS_DIRECTORY "$ENV{TMPDIR}")
  set(temporary "$ENV{TMPDIR}")
else()
  set(temporary "/tmp")
endif()
file(TO_CMAKE_PATH "${temporary}" temporary)
get_filename_component(temporary "${temporary}" REALPATH)
string(RANDOM LENGTH 12 suffix)
set(outside "${temporary}/jh-compile-check-${suffix}")
string(FIND "${outside}/" "${root}/" in_root)
string(FIND "${outside}/" "${cfg}/" in_project)
if(in_root EQUAL 0 OR in_project EQUAL 0)
  message(FATAL_ERROR "the temporary directory ${outside} is not outside the managed roots")
endif()
write_configure_project("${CMAKE_COMMAND}" "${outside}")
set(outside_tree "${outside}/rp2040/pico")
file(MAKE_DIRECTORY "${outside_tree}")
set(foreign "CMAKE_HOME_DIRECTORY:INTERNAL=${work}/elsewhere\n")
file(WRITE "${outside_tree}/CMakeCache.txt" "${foreign}")
# The tree goes before the first check, so a failing check leaves nothing in
# the temporary directory.
invoke_unchecked(${cfg_args} --configure-only --json)
set(kept "")
if(EXISTS "${outside_tree}/CMakeCache.txt")
  file(READ "${outside_tree}/CMakeCache.txt" kept)
endif()
file(REMOVE_RECURSE "${outside}")
if(NOT code STREQUAL "5")
  message(FATAL_ERROR "refused reset: expected exit 5, got ${code}\n${stdout}\n${stderr}")
endif()
string(JSON message GET "${stdout}" message)
slashed(message "${message}")
contains("${message}" "refusing to reset stale CMake cache outside managed artifact roots: ${outside_tree}\n       cached source: ${work}/elsewhere\n       requested source: ${source}"
  "refused reset")
if(NOT kept STREQUAL foreign)
  message(FATAL_ERROR "the refused reset changed the cache")
endif()
file(REMOVE_RECURSE "${JH_WORK}")
