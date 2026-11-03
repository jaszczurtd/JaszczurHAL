# The developer shell environment of a Visual Studio installation, for
# scripts/jh_bootstrap.cmake: a CMake without the generator of the installed
# release builds jh with Ninja and cl.exe in this environment. A file of its
# own, so tools/jh/tests/check_bootstrap.cmake calls it directly on Windows.

# jh_enter_msvc_environment(<vs path> <arch>)
#
# Takes the environment that vcvarsall.bat of the installation at <vs path>
# sets for a jh process of <arch> (x64 or x86 on Windows x64). CMake itself
# prints it and the output is read as UTF-8, whatever the console code page,
# so a non-ASCII directory (a user profile in PATH, TEMP or TMP) survives;
# the batch file reports on stderr.
function(jh_enter_msvc_environment vs_path arch)
  set(vcvarsall "${vs_path}/VC/Auxiliary/Build/vcvarsall.bat")
  if(NOT EXISTS "${vcvarsall}")
    message(FATAL_ERROR "[jh-bootstrap] ${vcvarsall} is missing; install the MSVC x64/x86 toolset of this Visual Studio")
  endif()
  if(arch STREQUAL "x86")
    set(vcvars_arch x64_x86)
  else()
    set(vcvars_arch x64)
  endif()
  file(TO_NATIVE_PATH "${vcvarsall}" vcvarsall)
  set(shell cmd)
  if(DEFINED ENV{ComSpec} AND NOT "$ENV{ComSpec}" STREQUAL "")
    set(shell "$ENV{ComSpec}")
  endif()
  execute_process(COMMAND "${shell}" /d /c call "${vcvarsall}" ${vcvars_arch} 1>&2
      && "${CMAKE_COMMAND}" -E environment
    RESULT_VARIABLE rc OUTPUT_VARIABLE text ERROR_VARIABLE messages ENCODING UTF8)
  if(NOT rc EQUAL 0)
    string(STRIP "${messages}" messages)
    message(FATAL_ERROR "[jh-bootstrap] ${vcvarsall} ${vcvars_arch} failed (exit ${rc}): ${messages}")
  endif()
  # One NAME=VALUE per line; a value keeps its semicolons, so the lines are
  # cut out of the text rather than read as a CMake list.
  string(REPLACE "\r\n" "\n" text "${text}")
  while(NOT text STREQUAL "")
    string(FIND "${text}" "\n" end)
    if(end EQUAL -1)
      set(line "${text}")
      set(text "")
    else()
      string(SUBSTRING "${text}" 0 ${end} line)
      math(EXPR next "${end} + 1")
      string(SUBSTRING "${text}" ${next} -1 text)
    endif()
    string(FIND "${line}" "=" separator)
    # The drive variables of cmd (=C:=C:\...) have no name before the sign.
    if(separator GREATER 0)
      string(SUBSTRING "${line}" 0 ${separator} name)
      math(EXPR next "${separator} + 1")
      string(SUBSTRING "${line}" ${next} -1 value)
      set(ENV{${name}} "${value}")
    endif()
  endwhile()
endfunction()
