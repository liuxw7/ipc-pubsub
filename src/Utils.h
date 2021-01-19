#pragma once
#include <cstdint>
#include <functional>

// Output should have 16 values
void ToHexString(uint64_t v, char out[]);
struct OnRet {
    OnRet(std::function<void()> cb) : mCb(cb) {}
    ~OnRet() { mCb(); }
    std::function<void()> mCb;
};
