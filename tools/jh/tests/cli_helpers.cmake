# Helpers of the CLI checks: run jh from JH_WORK/cwd and read its JSON result,
# and check the text and JSON it produces (also for tests/*.cmake).

# Run jh with ARGN, require exit code expected and keep stdout and stderr.
function(invoke expected)
  execute_process(COMMAND "${JH}" ${ARGN} WORKING_DIRECTORY "${JH_WORK}/cwd"
    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err ENCODING UTF-8)
  if(NOT code STREQUAL "${expected}")
    message(FATAL_ERROR "jh ${ARGN}: expected exit ${expected}, got ${code}\n${out}\n${err}")
  endif()
  set(stdout "${out}" PARENT_SCOPE)
  set(stderr "${err}" PARENT_SCOPE)
endfunction()

# Require one versioned JSON result on stdout with this status.
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

# text with the \ separators jh writes in paths on Windows turned into /, to
# compare with the paths the checks spell for CMake.
function(slashed variable text)
  if(CMAKE_HOST_WIN32)
    string(REPLACE "\\" "/" text "${text}")
  endif()
  set(${variable} "${text}" PARENT_SCOPE)
endfunction()

# Fail unless text contains, or lacks, fragment literally; what names it.
function(contains text fragment what)
  string(FIND "${text}" "${fragment}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "${what}: missing '${fragment}' in:\n${text}")
  endif()
endfunction()
function(lacks text fragment what)
  string(FIND "${text}" "${fragment}" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "${what}: unexpected '${fragment}' in:\n${text}")
  endif()
endfunction()

# Fail unless the JSON value at path ARGN of json equals the JSON text expected.
function(json_equals json expected what)
  string(JSON actual GET "${json}" ${ARGN})
  string(JSON same EQUAL "${actual}" "${expected}")
  if(NOT same)
    message(FATAL_ERROR "${what}: got ${actual}, expected ${expected}")
  endif()
endfunction()

# The strings of the stdout array at the JSON path ARGN, as a CMake list; an
# item holding a semicolon becomes several elements, so count with
# string(JSON LENGTH) instead.
function(json_strings variable)
  string(JSON count LENGTH "${stdout}" ${ARGN})
  set(items "")
  if(count GREATER 0)
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
      string(JSON item GET "${stdout}" ${ARGN} ${index})
      list(APPEND items "${item}")
    endforeach()
  endif()
  set(${variable} "${items}" PARENT_SCOPE)
endfunction()
