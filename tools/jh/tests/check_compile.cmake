cmake_minimum_required(VERSION 3.22)
# jh compile --dry-run: the model and plan of a VS Code firmware build on a
# board path fixture (target profile, tokens, CMake cache arguments, USB
# identity, variant, debug tree, recorded cache keys, stale cache, Windows
# buildRoot and the Python3_EXECUTABLE policy), its configuration errors and
# the device tree and ESP-IDF builds of examples/01_core_runtime. Expected
# values come from the fixture and the tracked registry, read here with
# string(JSON). No process of the plan runs.
if(NOT EXISTS "${JH}" OR NOT IS_DIRECTORY "${JH_HAL_SOURCE}/boards")
  message(FATAL_ERROR "jh_compile needs JH and JH_HAL_SOURCE")
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
invoke(2 ${base_args} --json)
text_at("compile runs with --dry-run only" "without --dry-run" message)
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
invoke(2 compile --root "${root}" --project "${example}" --assembly esp32s3-zero --debug --dry-run
  --json)
text_at("ESP-IDF debug builds are not implemented by jh-vscode" "ESP debug" message)
file(REMOVE_RECURSE "${JH_WORK}")
