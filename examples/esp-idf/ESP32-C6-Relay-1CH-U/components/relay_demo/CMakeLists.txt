idf_component_register(
    SRCS
        "relay_demo.c"
        "relay_wifi.c"
        "relay_ble.c"
        "relay_time.c"
        "relay_serial.c"
        "relay_network.c"
        "relay_ping.c"
        "relay_command_parser.c"
        "relay_web_tests.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        bt
        esp_event
        esp_http_server
        esp_netif
        esp_wifi
        nvs_flash
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_uart
        esp_timer
        lwip
    EMBED_TXTFILES "web/index.html" "web/style.css" "web/app.js"
)
