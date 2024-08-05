idf_component_register(
    SRCS "main.cpp" "utils.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi nvs_flash esp_driver_gpio esp_driver_ledc
)