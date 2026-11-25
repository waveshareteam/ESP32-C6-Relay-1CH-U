#include "esp_err.h"
#include "relay_demo.h"

void app_main(void)
{
    ESP_ERROR_CHECK(relay_demo_start());
}
