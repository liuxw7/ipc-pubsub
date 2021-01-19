
#include "Utils.h"

static char intToHex[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

void ToHexString(uint64_t v, char out[]) {
    for (uint8_t i = 0; i < 16; ++i) {
        out[i + 1] = intToHex[(v >> ((15 - i) * 4)) & 0xf];
    }
}
