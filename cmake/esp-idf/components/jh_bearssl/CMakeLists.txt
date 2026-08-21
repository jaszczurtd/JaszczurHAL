file(TO_CMAKE_PATH "$ENV{JH_ESP_IDF_PROJECT_CONFIG}"
    _jh_project_config)
if(NOT _jh_project_config OR NOT EXISTS "${_jh_project_config}")
    message(FATAL_ERROR
        "JH_ESP_IDF_PROJECT_CONFIG must name the generated project config")
endif()
include("${_jh_project_config}")

list(FIND JH_ESP_IDF_RESOLVED_FEATURES "HAL_ENABLE_TLS"
    _jh_tls_feature_index)
if(_jh_tls_feature_index EQUAL -1)
    idf_component_register()
    return()
endif()

get_filename_component(JH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.."
    ABSOLUTE)
include("${JH_ROOT}/cmake/jh_bearssl.cmake")
jh_bearssl_source_manifest(_jh_bearssl_sources _jh_bearssl_include_dirs)

idf_component_register(
    SRCS ${_jh_bearssl_sources}
    INCLUDE_DIRS ${_jh_bearssl_include_dirs})

target_compile_options(${COMPONENT_LIB} PRIVATE
    -ffunction-sections
    -fdata-sections)
