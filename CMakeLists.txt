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
    src/encoder.c
    src/dfr0555_display.c
    src/menu.c
    src/i2c_bus.c
    Api/vl53l4cd/VL53L4CD_api.c
    Api/vl53l4cd/platform.c
)

pico_generate_pio_header(optical_theremin
    ${CMAKE_CURRENT_LIST_DIR}/src/audio_i2s.pio
)

# build_date.h is regenerated on every build, see build_date.cmake.
set(BUILD_DATE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/build_date)
add_custom_target(build_date ALL
    COMMAND ${CMAKE_COMMAND} -DOUTPUT=${BUILD_DATE_DIR}/build_date.h
            -P ${CMAKE_CURRENT_LIST_DIR}/build_date.cmake
    BYPRODUCTS ${BUILD_DATE_DIR}/build_date.h
    COMMENT "Updating build date"
)
add_dependencies(optical_theremin build_date)

target_include_directories(optical_theremin PRIVATE
    Api/vl53l4cd
    src
    ${BUILD_DATE_DIR}
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
