# Install one HAL build, repair an existing prefix, relocate it, then link
# C and C++ applications using only its public headers and archive.
cmake_minimum_required(VERSION 3.22)

foreach(required JH_PACKAGE_BUILD JH_PACKAGE_WORK)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
    get_filename_component(${required} "${${required}}" ABSOLUTE)
endforeach()

function(package_cache name output)
    file(STRINGS "${JH_PACKAGE_BUILD}/CMakeCache.txt" entry REGEX "^${name}:[^=]+=")
    # Match once so '=' in compiler flags remains part of the value.
    string(REGEX MATCH "^[^=]+=(.*)$" match "${entry}")
    set(${output} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

function(package_compiler language)
    file(GLOB records "${JH_PACKAGE_BUILD}/CMakeFiles/*/CMake${language}Compiler.cmake")
    list(LENGTH records count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Expected one ${language} compiler record, found ${count}")
    endif()
    list(GET records 0 record)
    include("${record}")
    package_cache(CMAKE_${language}_FLAGS flags)
    separate_arguments(options NATIVE_COMMAND "${flags}")
    set(expanded "")
    foreach(option IN LISTS options)
        if(option MATCHES "^@(.+)$")
            file(READ "${CMAKE_MATCH_1}" response)
            separate_arguments(response NATIVE_COMMAND "${response}")
            list(APPEND expanded ${response})
        else()
            list(APPEND expanded "${option}")
        endif()
    endforeach()
    # Keep the machine ABI, without SDK includes, response files or linker specs.
    list(FILTER expanded INCLUDE REGEX "^-m|^-f(no-)?short-enums$|^-fpack-struct")
    list(JOIN expanded " " machine_flags)
    set(JH_PACKAGE_${language}_COMPILER "${CMAKE_${language}_COMPILER}" PARENT_SCOPE)
    set(JH_PACKAGE_${language}_FLAGS "${machine_flags}" PARENT_SCOPE)
endfunction()

package_compiler(C)
package_compiler(CXX)
if(NOT DEFINED JH_PACKAGE_JHBASE_REF)
    file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../third_party/jhbase_version.conf"
        pin REGEX "^JHBASE_REF=")
    string(REPLACE "JHBASE_REF=" "" JH_PACKAGE_JHBASE_REF "${pin}")
endif()

function(package_run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
        OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Package check failed (${result}): ${ARGV}\n${output}\n${error}")
    endif()
endfunction()

set(prefix "${JH_PACKAGE_WORK}/installed")
set(relocated "${JH_PACKAGE_WORK}/relocated prefix")
set(project "${JH_PACKAGE_WORK}/consumer")
file(REMOVE_RECURSE "${JH_PACKAGE_WORK}")
package_run("${CMAKE_COMMAND}" --install "${JH_PACKAGE_BUILD}" --prefix "${prefix}")
foreach(header abi.h array.h status.h)
    if(NOT EXISTS "${prefix}/include/jb/${header}")
        message(FATAL_ERROR "Installed status header is missing: jb/${header}")
    endif()
endforeach()
# An upgrade must repair an incomplete or damaged status package.
file(WRITE "${prefix}/include/jb/status.h" "#error incomplete installation\n")
file(REMOVE "${prefix}/include/jb/abi.h")
package_run("${CMAKE_COMMAND}" --install "${JH_PACKAGE_BUILD}" --prefix "${prefix}")
file(RENAME "${prefix}" "${relocated}")

set(source [=[
#include <hal/core/hal_status.h>
#include <hal/core/hal_array.h>
#include <hal/core/hal_math.h>

JB_STATIC_ASSERT(sizeof(hal_status_t) == 4, "status must remain 32 bits");
static const int values[] = {10, 20};
int main(void) {
    hal_status_t status = COUNTOF(values) == 2 ? HAL_OK : HAL_EINVAL;
    return hal_status_is_ok(status) && hal_math_percent_from_value(20, 40) == 50
               ? 0 : 1;
}
]=])
file(MAKE_DIRECTORY "${project}")
file(WRITE "${project}/app.c" "${source}")
file(WRITE "${project}/app.cpp" "${source}")
# No HAL source path or generator is passed to this independent project.
file(TO_CMAKE_PATH "${relocated}" JH_PACKAGE_PREFIX)
set(consumer [=[
cmake_minimum_required(VERSION 3.22)
project(installed_hal_consumer C CXX)
set(prefix "@JH_PACKAGE_PREFIX@")
find_package(jhbase 0.1 REQUIRED CONFIG
    PATHS "${prefix}/lib/cmake/jhbase" NO_DEFAULT_PATH)
math(EXPR bits "${CMAKE_SIZEOF_VOID_P} * 8")
if(NOT jhbase_ABI_PROFILE STREQUAL "natural-ptr${bits}-status32-bool8")
    message(FATAL_ERROR "Installed jhbase ABI does not match this compiler")
endif()
if(NOT jhbase_REVISION MATCHES "^[0-9a-f]+$" OR
   NOT jhbase_REVISION STREQUAL "@JH_PACKAGE_JHBASE_REF@")
    message(FATAL_ERROR "Installed jhbase revision differs from the build pin")
endif()
get_target_property(headers jb::status INTERFACE_INCLUDE_DIRECTORIES)
if(NOT headers STREQUAL "${prefix}/include")
    message(FATAL_ERROR "Status headers depend on a path outside the package: ${headers}")
endif()
foreach(language c cpp)
    add_executable(consumer_${language} app.${language})
    target_compile_options(consumer_${language} PRIVATE -Wall -Wextra -Werror)
    target_link_options(consumer_${language} PRIVATE
        -nostdlib -Wl,-e,main -Wl,--gc-sections)
    target_link_libraries(consumer_${language} PRIVATE jb::status
        "${prefix}/lib/libJaszczurHAL.a" gcc c)
endforeach()
]=])
string(CONFIGURE "${consumer}" consumer @ONLY)
file(WRITE "${project}/CMakeLists.txt" "${consumer}")
package_run("${CMAKE_COMMAND}" -S "${project}" -B "${project}/.build"
    -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
    "-DCMAKE_C_COMPILER=${JH_PACKAGE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${JH_PACKAGE_CXX_COMPILER}"
    "-DCMAKE_C_FLAGS=${JH_PACKAGE_C_FLAGS}"
    "-DCMAKE_CXX_FLAGS=${JH_PACKAGE_CXX_FLAGS}")
package_run("${CMAKE_COMMAND}" --build "${project}/.build" --parallel 2)
message(STATUS "Installed HAL package links C and C++ consumers after repair and relocation")
