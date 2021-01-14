#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

class ShmBuffer;

class Dispatcher {
   public:
    Dispatcher();
    ~Dispatcher();

    void HandleLoop();
    void Push(std::string_view topic, std::string_view shmName);
    void SetCallback(std::string_view topic,
                     std::function<void(const uint8_t* data, size_t len)> cb);

   private:
    struct WorkItem {
        std::string topic;
        std::shared_ptr<ShmBuffer> buffer;
    };

    std::mutex mMtx;
    std::condition_variable mCv;
    std::deque<WorkItem> mWorkQueue;
    std::unordered_map<std::string, std::function<void(const uint8_t* data, size_t len)>> mHandlers;
    std::thread mWorker;
    bool mKeepGoing;
};
