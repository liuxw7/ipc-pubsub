#include <spdlog/spdlog.h>
#include <unistd.h>

#include "TopologyManager.h"

int main() {
    TopologyManager mgr("hello", "node1");
    while (true) {
        SPDLOG_INFO("Still alive");
        sleep(1);
    }
}
