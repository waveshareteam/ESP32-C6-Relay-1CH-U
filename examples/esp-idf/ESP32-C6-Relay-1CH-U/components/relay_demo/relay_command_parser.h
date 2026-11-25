#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct { uint8_t bytes[8]; unsigned used; } relay_command_parser_t;
typedef void (*relay_command_callback_t)(uint8_t command, void *arg);
void relay_command_parser_reset(relay_command_parser_t *parser);
unsigned relay_command_parser_feed(relay_command_parser_t *parser, const uint8_t *data,
    size_t length, relay_command_callback_t callback, void *arg);
