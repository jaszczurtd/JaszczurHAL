idf_component_register(
    SRCS "phase0_main.c"
    INCLUDE_DIRS "."
    REQUIRES jaszczurhal)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
