/* SPDX-License-Identifier: Apache-2.0 */
#include "relay_command_parser.h"
static const uint8_t commands[3][8] = {
    {0x06,0x05,0x00,0x01,0x55,0x00,0xA2,0xED},
    {0x06,0x05,0x00,0xFF,0xFF,0x00,0xBD,0xBD},
    {0x06,0x05,0x00,0xFF,0x00,0x00,0xFC,0x4D},
};
void relay_command_parser_reset(relay_command_parser_t *parser) { parser->used = 0; }
unsigned relay_command_parser_feed(relay_command_parser_t *parser, const uint8_t *data,
    size_t length, relay_command_callback_t callback, void *arg)
{
    unsigned matches = 0;
    for (size_t i = 0; i < length; ++i) {
        parser->bytes[parser->used++] = data[i];
        if (parser->used < 8) continue;
        for (unsigned cmd = 0; cmd < 3; ++cmd) {
            unsigned n = 0;
            while (n < 8 && parser->bytes[n] == commands[cmd][n]) ++n;
            if (n == 8) {
                ++matches;
                if (callback) callback(cmd == 0 ? '1' : cmd == 1 ? '9' : '0', arg);
            }
        }
        for (unsigned n = 0; n < 7; ++n) parser->bytes[n] = parser->bytes[n+1];
        parser->used = 7;
    }
    return matches;
}
