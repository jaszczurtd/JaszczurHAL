# Host sanitizer options of the test builds: the root project and the jh host
# CLI (tools/jh). Include this before the first target, so every target of the
# project, its dependencies included, is instrumented.
option(JH_ENABLE_SANITIZERS
    "Instrument host code with AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF)
option(JH_ENABLE_THREAD_SANITIZER
    "Instrument host code with ThreadSanitizer instead of ASan/UBSan"
    OFF)

if(JH_ENABLE_SANITIZERS AND JH_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR
        "JH_ENABLE_SANITIZERS and JH_ENABLE_THREAD_SANITIZER are mutually exclusive")
endif()
if((JH_ENABLE_SANITIZERS OR JH_ENABLE_THREAD_SANITIZER) AND MSVC)
    message(FATAL_ERROR "Host sanitizer options require GCC or Clang")
endif()
if(JH_ENABLE_SANITIZERS)
    # Public HAL entry points deliberately validate out-of-range enum values.
    # Loading such a value is itself diagnosed by Clang's enum sanitizer before
    # the validation can run, so keep the rest of UBSan enabled but exclude that
    # incompatible check.
    add_compile_options(
        -fsanitize=address,undefined
        -fno-sanitize=enum
        -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
if(JH_ENABLE_THREAD_SANITIZER)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread -fno-omit-frame-pointer)
endif()
