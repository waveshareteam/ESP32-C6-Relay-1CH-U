#pragma once
#include "esp_err.h"

/* Start 9600/8N1 relay-command RX and raw echo after a 20 ms idle gap.
 * Receive bursts up to 1024 bytes are echoed without extra framing/CRC. */
esp_err_t relay_serial_init(void);
