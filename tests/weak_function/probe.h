#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int weak_c_value(int value);
const int *weak_cpp_pointer(void);
void weak_cpp_update(int *value);
int call_from_c_default(int value);
int call_from_cpp_default(int value);
int call_from_cpp(void);

#ifdef __cplusplus
}
#endif
