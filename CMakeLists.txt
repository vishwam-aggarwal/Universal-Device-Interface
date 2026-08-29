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
