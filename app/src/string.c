#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>


bool is_printable_ascii_char(uint8_t byte) {
    return byte >= 32 && byte <= 126;
}


bool is_printable_ascii_string(uint8_t* buffer, size_t buffer_len) {
    if (buffer == NULL) {
        return false;
    }

    for (size_t i = 0; i < buffer_len; i++) {
        if (buffer[i] == '\0') {
            return true;
        }
        if (!is_printable_ascii_char(buffer[i])) {
            return false;
        }
    }
    // no null-terminator so return false
    return false;
}