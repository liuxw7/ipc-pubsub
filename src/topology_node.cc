#include <unistd.h>

#include "TopologyManager.h"

int main() {
    TopologyManager mgr("hello", "node1");
    while (true) {
        std::cout << "Still alive" << std::endl;
        sleep(1);
    }
}
