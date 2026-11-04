# Files outside tools/jh that the jh build reads. The bootstrap key covers them
# together with the tools/jh sources, and tools/jh/tests/check_bootstrap.cmake
# exports them into its stand-alone tree.
set(JH_TOOL_INPUTS
  cmake/jh_jhbase.cmake cmake/jh_pcre2.cmake cmake/jh_verify_pcre2.cmake
  cmake/jh_build_dir_guard.cmake cmake/jh_sanitizers.cmake cmake/jh_memcheck.cmake
  third_party/jhbase_version.conf
  third_party/pcre2_version.conf config/tooling/managed_components.json
  scripts/jh_bootstrap.cmake scripts/jh_msvc_environment.cmake scripts/jh_tool_inputs.cmake
  src/hal/security/jh_sha256.c src/hal/security/jh_sha256.h src/hal/core/jh_endian.h
  src/hal/core/hal_layout_check.h)
