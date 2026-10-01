cmake_minimum_required(VERSION 3.14)
project(UniversalDeviceInterface CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# IDevice.h / GlobalErrorSink.h have no Arduino dependency at all, so the
# whole library builds on the host. IDevice.cpp exists only to hold the
# out-of-line definitions of IDevice's static sink storage -- consumers'
# desktop harnesses compile it the same way Universal-Motion-Interface
# compiles extern/Universal-Motor-Interface/src/IMotorDriver.cpp today.
add_library(device src/IDevice.cpp)
target_include_directories(device PUBLIC src)

add_executable(test_device_sink     tests/test_device_sink.cpp)
add_executable(test_solenoid_device tests/test_solenoid_device.cpp)

target_link_libraries(test_device_sink     PRIVATE device)
target_link_libraries(test_solenoid_device PRIVATE device)

enable_testing()
add_test(NAME device_sink     COMMAND test_device_sink)
add_test(NAME solenoid_device COMMAND test_solenoid_device)

add_executable(test_describe tests/test_describe.cpp)
target_link_libraries(test_describe PRIVATE device)
add_test(NAME describe COMMAND test_describe)

add_executable(test_time tests/test_time.cpp)
target_link_libraries(test_time PRIVATE device)
add_test(NAME time COMMAND test_time)

# Compile-time rules. Each source here must NOT compile; the test builds
# its target and passes only when the compiler's message names the rule
# that was broken.
function(udi_compile_fail name regex)
    add_executable(cf_${name} EXCLUDE_FROM_ALL tests/compile_fail/${name}.cpp)
    target_link_libraries(cf_${name} PRIVATE device)
    add_test(NAME compile_fail_${name}
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target cf_${name} --config $<CONFIG>)
    set_tests_properties(compile_fail_${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
endfunction()

udi_compile_fail(w_without_setter  "Set_wSpeed")
udi_compile_fail(cb_without_setter "Set_cnfGain")
udi_compile_fail(bad_prefix        "attribute speed must be named r")
udi_compile_fail(enum_prefix       "enumeration Mode must be named enum")
udi_compile_fail(bad_type          "attrTypeOf")
udi_compile_fail(reserved_rstate   "rState is IDevice's own attribute")
udi_compile_fail(mount_cb          "UDI_MOUNT_CB")
udi_compile_fail(w_str_without_setter "Set_wName")
