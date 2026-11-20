# Firmware for the board's ESP32-C6 Wi-Fi chip (ESP-Hosted 3.1.0 co-processor).
# Build with target esp32c6, then copy build/liahona_c6.bin to liahona/c6_wifi.bin
# on the ball's SD card. See README.md.
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(liahona_c6)
