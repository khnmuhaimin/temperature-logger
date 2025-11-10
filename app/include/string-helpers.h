#ifndef STRING_HELPERS_H
#define STRING_HELPERS_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>


bool is_printable_ascii_char(uint8_t byte);
bool is_printable_ascii_string(uint8_t *buffer, size_t buffer_len);


#endif
