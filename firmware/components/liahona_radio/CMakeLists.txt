# Shared LoRa radio code: SX1262 driver + Liahona packet format.
# Used by the ball (firmware/) and the base station (base_station/).
idf_component_register(
    SRCS "sx1262.c" "sx1262_hal_esp.c" "liahona_proto.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_spi
)
