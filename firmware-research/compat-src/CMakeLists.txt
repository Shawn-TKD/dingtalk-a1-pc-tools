cmake_minimum_required(VERSION 3.20)
project(a1_compat_core VERSION 0.1.0 LANGUAGES C)

add_library(a1_compat_core STATIC
    src/audio.c
    src/audio_container.c
    src/audio_index.c
    src/auth.c
    src/ble_endpoint.c
    src/ble_link.c
    src/boot_image.c
    src/button.c
    src/companion_core.c
    src/device_info.c
    src/display.c
    src/file_sync.c
    src/flash_layout.c
    src/firmware_update.c
    src/frame.c
    src/gray_switch.c
    src/inbound_transfer.c
    src/json_reader.c
    src/json_writer.c
    src/live_audio.c
    src/memo_container.c
    src/persistence.c
    src/protocol_service.c
    src/raw_transfer.c
    src/recording_policy.c
    src/remark.c
    src/rmt_ipc.c
    src/router.c
    src/runtime.c
    src/schedule.c
    src/session.c
    src/system_control.c
    src/stream_transport.c
    src/usb_hid.c
    src/wifi.c
    src/wifi_tcp.c
    src/wire_requests.c
    src/wire_responses.c
)

target_include_directories(a1_compat_core PUBLIC include)
target_compile_features(a1_compat_core PUBLIC c_std_11)

if(MSVC)
    target_compile_options(a1_compat_core PRIVATE /W4)
else()
    target_compile_options(a1_compat_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(A1_BUILD_TESTS "Build focused host-side protocol tests" ON)

if(A1_BUILD_TESTS)
    enable_testing()
    add_executable(a1_core_tests tests/core_tests.c)
    target_link_libraries(a1_core_tests PRIVATE a1_compat_core)
    target_compile_features(a1_core_tests PRIVATE c_std_11)
    add_test(NAME a1_core_tests COMMAND a1_core_tests)
endif()
