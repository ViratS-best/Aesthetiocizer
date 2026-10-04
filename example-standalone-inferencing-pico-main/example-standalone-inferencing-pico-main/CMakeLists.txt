cmake_minimum_required(VERSION 3.13.1)

set(PROJECT_FOLDER .)

if(NOT PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_PATH "$ENV{USERPROFILE}/pico-sdk" CACHE PATH "Path to the PICO SDK" FORCE)
endif()

set(PICO_BOARD seeed_xiao_rp2040 CACHE STRING "Target board")

include(pico_sdk_import.cmake)

project(app C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

pico_sdk_init()

add_definitions(-DEIDSP_QUANTIZE_FILTERBANK=0
                -DEI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN=0
                -DARM_MATH_LOOPUNROLL
                )

add_executable(app
  source/main.cpp
  )

pico_generate_pio_header(app ${CMAKE_CURRENT_LIST_DIR}/source/pdm_microphone.pio)

include(${PROJECT_FOLDER}/source/edge-impulse-sdk/cmake/utils.cmake)

target_link_libraries(app 
    pico_stdlib
    hardware_gpio
    hardware_pwm
    hardware_dma
    hardware_pio
)

target_include_directories(app PRIVATE
  ${PROJECT_FOLDER}/source/tflite-model
  ${PROJECT_FOLDER}/source/model-parameters
    ${PROJECT_FOLDER}
  ${PROJECT_FOLDER}/source
)

pico_enable_stdio_usb(app 1)
pico_enable_stdio_uart(app 0)

RECURSIVE_FIND_FILE(SOURCE_FILES "${PROJECT_FOLDER}/source/edge-impulse-sdk" "*.cpp")
RECURSIVE_FIND_FILE(MODEL_FILES "${PROJECT_FOLDER}/source/tflite-model" "*.cpp")
RECURSIVE_FIND_FILE(CC_FILES "${PROJECT_FOLDER}/source/edge-impulse-sdk" "*.cc")
RECURSIVE_FIND_FILE(C_FILES "${PROJECT_FOLDER}/source/edge-impulse-sdk" "*.c")

list(APPEND SOURCE_FILES ${S_FILES})
list(APPEND SOURCE_FILES ${C_FILES})
list(APPEND SOURCE_FILES ${CC_FILES})
list(APPEND SOURCE_FILES ${MODEL_FILES})

target_sources(app PRIVATE ${SOURCE_FILES})

pico_add_extra_outputs(app)
