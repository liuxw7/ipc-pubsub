#include "Globals.h"

#include <signal.h>

#include <iostream>
#include <memory>
#include <mutex>
#include <set>

#include "IPCMessenger.h"
#include "ShmMessage.h"

template <typename T>
struct WeakPtrCompare {
    bool operator()(const std::weak_ptr<T>& lhs, const std::weak_ptr<T>& rhs) const {
        return lhs.owner_before(rhs);
    }
};

struct {
    std::mutex mtx;
    std::set<std::weak_ptr<IPCMessenger>, WeakPtrCompare<IPCMessenger>> messengers;
    std::set<std::weak_ptr<ShmMessage>, WeakPtrCompare<ShmMessage>> messages;
} gGlobals;

sighandler_t gOrigIntHandler = nullptr;
sighandler_t gOrigAbortHandler = nullptr;
sighandler_t gOrigTermHandler = nullptr;

[[noreturn]] static void signalHandler(int signum) {
    std::cerr << "Caught " << signum << " Shutting down" << std::endl;
    if (gOrigTermHandler && signum == SIGTERM) {
        gOrigTermHandler(signum);
    }
    if (gOrigIntHandler && signum == SIGINT) {
        gOrigIntHandler(signum);
    }
    if (gOrigAbortHandler && signum == SIGABRT) {
        gOrigAbortHandler(signum);
    }

    std::cerr << "Locking " << std::endl;
    std::lock_guard<std::mutex> lk(gGlobals.mtx);
    for (auto weakPtr : gGlobals.messengers) {
        std::cerr << "Deallocate IPCMessenger" << std::endl;
        auto ptr = weakPtr.lock();
        if (ptr) {
            std::cerr << "Still Valid" << std::endl;
            ptr->~IPCMessenger();
        }
    }

    for (auto weakPtr : gGlobals.messages) {
        auto ptr = weakPtr.lock();
        std::cerr << "Deallocate Message" << std::endl;
        if (ptr) {
            std::cerr << "Still valid" << std::endl;
            ptr->~ShmMessage();
        }
    }

    exit(0);
}

static int HandleSignals() {
    std::cerr << "Initialize Cleanup" << std::endl;
    gOrigIntHandler = signal(SIGINT, signalHandler);
    gOrigTermHandler = signal(SIGTERM, signalHandler);
    gOrigAbortHandler = signal(SIGABRT, signalHandler);
    return 0;
}

static void SetupSignals() { [[maybe_unused]] static int foo = HandleSignals(); }

void EnsureCleanup(std::weak_ptr<IPCMessenger> ptr) {
    SetupSignals();
    std::lock_guard<std::mutex> lk(gGlobals.mtx);
    gGlobals.messengers.emplace(ptr);
}

void EnsureCleanup(std::weak_ptr<ShmMessage> ptr) {
    SetupSignals();
    std::lock_guard<std::mutex> lk(gGlobals.mtx);
    gGlobals.messages.emplace(ptr);
}
