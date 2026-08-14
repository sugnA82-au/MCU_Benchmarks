cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(rp2040_bench C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(rp2040_bench
    main.cpp
    coremark/core_list_join.c
    coremark/core_main.c
    coremark/core_matrix.c
    coremark/core_state.c
    coremark/core_util.c
    coremark/core_portme.c
)

target_include_directories(rp2040_bench PRIVATE coremark)

# ITERATIONS=0 lets CoreMark self-calibrate to a ~10s run regardless of the
# target's actual speed -- important since this is meant as a portable
# baseline across different MCUs, not just this one.
target_compile_definitions(rp2040_bench PRIVATE
    ITERATIONS=0
    PERFORMANCE_RUN=1
    FLAGS_STR="-O2 cortex-m0plus"
)

pico_set_program_name(rp2040_bench "rp2040_bench")
pico_set_program_version(rp2040_bench "1.0")

# USB CDC serial console only (works regardless of which physical board this
# is, since it uses the RP2040's native USB peripheral, not board-specific
# UART pins).
pico_enable_stdio_usb(rp2040_bench 1)
pico_enable_stdio_uart(rp2040_bench 0)

target_compile_options(rp2040_bench PRIVATE -O2 -Wall)
target_compile_options(rp2040_bench PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)

target_link_libraries(rp2040_bench
    pico_stdlib
    pico_multicore
    pico_flash
    hardware_flash
    hardware_sync
    hardware_clocks
    hardware_structs
)

pico_add_extra_outputs(rp2040_bench)
