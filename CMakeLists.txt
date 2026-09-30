cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(optical_theremin C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(optical_theremin
    src/main.c
    src/dds_generator.c
    src/i2s_output.c
    src/pcm5122.c
    src/waveform_select.c
    Api/core/src/vl53l0x_api.c
    Api/core/src/vl53l0x_api_core.c
    Api/core/src/vl53l0x_api_calibration.c
    Api/core/src/vl53l0x_api_ranging.c
    Api/core/src/vl53l0x_api_strings.c
    Api/platform/src/vl53l0x_platform.c
    Api/platform/src/vl53l0x_i2c_platform.c
)

pico_generate_pio_header(optical_theremin
    ${CMAKE_CURRENT_LIST_DIR}/src/audio_i2s.pio
)

target_include_directories(optical_theremin PRIVATE
    Api/core/inc
    Api/platform/inc
)

target_link_libraries(optical_theremin
    pico_stdlib
    hardware_i2c
    hardware_pio
    hardware_dma
)

pico_enable_stdio_usb(optical_theremin 1)
pico_enable_stdio_uart(optical_theremin 0)

pico_add_extra_outputs(optical_theremin)
