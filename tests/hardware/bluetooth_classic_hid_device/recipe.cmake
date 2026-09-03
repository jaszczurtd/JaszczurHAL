set(JH_BLUETOOTH_CLASSIC_HID_DEVICE_FIXTURE TRUE)
include("${JH_ROOT}/cmake/targets/rp-native.cmake")

target_sources(JaszczurHAL PRIVATE
    "${JH_PROJECT_DIR}/device_profile.c")
target_include_directories(JaszczurHAL PRIVATE "${JH_PROJECT_DIR}")
