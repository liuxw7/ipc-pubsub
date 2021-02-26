
#include "ipc_pubsub/Utils.h"

#include <random>
#include <string>

static char intToHex[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

void ToHexString(uint64_t v, char out[]) {
    for (uint8_t i = 0; i < 16; ++i) {
        out[i + 1] = intToHex[(v >> ((15 - i) * 4)) & 0xf];
    }
}

uint64_t GenRandom() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 rng(rd());
    return rng();
}

void GenRandom(const size_t len, std::string* out) {
    std::random_device rd;
    std::mt19937_64 rng(rd());

    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    out->reserve(len);
    for (size_t i = 0; i < len; ++i) {
        *out += alphanum[rng() % (sizeof(alphanum) - 1)];
    }
}

std::string GenRandom(const size_t len) {
    std::string out;
    GenRandom(len, &out);
    return out;
}
