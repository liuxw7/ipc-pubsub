#pragma once
#include <functional>
#include <string>

// Output should have 16 values
void ToHexString(uint64_t v, char out[]);

struct OnRet {
    OnRet(std::function<void()> cb) : mCb(cb) {}
    ~OnRet() { mCb(); }
    std::function<void()> mCb;
};

uint64_t GenRandom();
void GenRandom(size_t len, std::string* out);
std::string GenRandom(size_t len);
