#include "Dispatcher.h"

#include <cassert>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

#include "ShmBuffer.h"

void Dispatcher::HandleLoop() {
    while (true) {
        std::shared_ptr<ShmBuffer> buffer;
        std::function<void(const uint8_t* data, size_t len)> handler;

        // wait for condition then fill buffer and handler
        {
            std::unique_lock<std::mutex> lk(mMtx);
            mCv.wait(lk, [this]() { return !mKeepGoing || mWorkQueue.size() > 0; });
            if (!mKeepGoing) break;

            assert(!mWorkQueue.empty());
            auto item = mWorkQueue.front();
            mWorkQueue.pop_front();
            auto handlerIt = mHandlers.find(item.topic);

            if (handlerIt == mHandlers.end()) {
                std::cerr << "Passed a message that we don't have a handler for, topic: "
                          << item.topic << std::endl;
                continue;
            }

            handler = handlerIt->second;
            buffer = item.buffer;
        }

        // finally call the handler with the buffer
        handler(buffer->Data(), buffer->Size());
    }
}

Dispatcher::Dispatcher() {
    mWorker = std::thread([this]() { HandleLoop(); });
}

Dispatcher::~Dispatcher() {
    {
        std::lock_guard<std::mutex> lk(mMtx);
        mKeepGoing = false;
        mCv.notify_all();
    }

    // thread needs to acquire lock to exit properly so don't lock while we wait
    mWorker.join();
}

void Dispatcher::Push(std::string_view topic, std::string_view shmName) {
    std::lock_guard<std::mutex> lk(mMtx);
    std::shared_ptr<ShmBuffer> buff = ShmBuffer::Load(shmName);
    mWorkQueue.push_back({std::string(topic), buff});
    mCv.notify_all();
};

void Dispatcher::SetCallback(std::string_view topic,
                             std::function<void(const uint8_t* data, size_t len)> cb) {
    std::lock_guard<std::mutex> lk(mMtx);
    mHandlers[std::string(topic)] = cb;
}
