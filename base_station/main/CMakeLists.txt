idf_component_register(
    SRCS "base.c" "bridge.c"
    INCLUDE_DIRS "."
    REQUIRES liahona_radio esp_driver_usb_serial_jtag esp_timer
)
