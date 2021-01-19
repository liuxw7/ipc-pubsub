#pragma once
#include <memory>

class IPCMessenger;
class ShmMessage;
void EnsureCleanup(std::weak_ptr<IPCMessenger> ptr);
void EnsureCleanup(std::weak_ptr<ShmMessage> ptr);
