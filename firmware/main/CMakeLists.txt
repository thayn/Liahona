idf_component_register(
    SRCS "main.c" "ui.c" "sensors.c" "gps.c" "nfc.c" "lora.c" "leds.c"
         "inputs.c" "storage.c" "audio_test.c" "ui_compass.c"
         "fonts/font_cinzel_56.c" "fonts/font_cinzel_bold_46.c"
         "fonts/font_cinzel_30.c" "fonts/font_inter_22.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_adc
             esp_driver_rmt fatfs app_update nvs_flash
)
