# Liahona base station: Seeed XIAO ESP32-S3 + Wio-SX1262.
# Bridges the LoRa radio to the laptop over USB (one JSON object per line).
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.1.0")
# shared radio driver + packet format live with the ball firmware
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../firmware/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(liahona_base)
