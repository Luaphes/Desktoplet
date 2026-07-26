idf_component_register(
    SRCS "main.cpp" "button.cpp" "mic_i2s.cpp" "ota.cpp" "websocket.cpp" "wifi_manager.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_websocket_client
)
