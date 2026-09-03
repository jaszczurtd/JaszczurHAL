include("${JH_ROOT}/cmake/targets/rp-native.cmake")

# This is a private diagnostic fixture. Its application uses BTstack's dump
# interface and the private CYW43 transport snapshot, neither of which is part
# of the public JaszczurHAL API.
target_include_directories(firmware PRIVATE
    "${JH_ROOT}/src/hal/bluetooth"
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040"
    "${JH_ROOT}/third_party/BTstack/src")
