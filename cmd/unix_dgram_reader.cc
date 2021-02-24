#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <iostream>
#include <string>
/*
 * In the included file <sys/un.h> a sockaddr_un is defined as follows
 * struct sockaddr_un {
 *  short   sun_family;
 *  char    sun_path[108];
 * };
 */

static int streamversion() {
    const std::string NAME = "socket";
    struct sockaddr_un addr;
    char buf[100];
    int64_t fd, cl, rc;

    const char* socket_path = "socket";

    if ((fd = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
        perror("socket error");
        exit(-1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (*socket_path == '\0') {
        *addr.sun_path = '\0';
        strncpy(addr.sun_path + 1, socket_path + 1, sizeof(addr.sun_path) - 2);
    } else {
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
        unlink(socket_path);
    }

    if (bind(int(fd), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        perror("bind error");
        exit(-1);
    }

    if (listen(int(fd), 5) == -1) {
        perror("listen error");
        exit(-1);
    }

    while (1) {
        if ((cl = accept(int(fd), nullptr, nullptr)) == -1) {
            perror("accept error");
            continue;
        }

        while ((rc = read(int(cl), buf, sizeof(buf))) > 0) {
            std::cout << "read " << rc << " bytes: " << buf << std::endl;
        }
        if (rc == -1) {
            perror("read");
            exit(-1);
        } else if (rc == 0) {
            printf("EOF\n");
            close(int(cl));
        }
    }
}
/*
 * This program creates a UNIX domain datagram socket, binds a name to it,
 * then reads from the socket.
 */
static int sequence_version() {
    const std::string NAME = "socket";
    struct sockaddr_un addr;
    char buf[100];
    int fd;
    int64_t cl, rc;

    const char* socket_path = "socket";

    if ((fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
        perror("socket error");
        exit(-1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (*socket_path == '\0') {
        *addr.sun_path = '\0';
        strncpy(addr.sun_path + 1, socket_path + 1, sizeof(addr.sun_path) - 2);
    } else {
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
        unlink(socket_path);
    }

    if (bind(int(fd), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        perror("bind error");
        exit(-1);
    }

    if (listen(int(fd), 5) == -1) {
        perror("listen error");
        exit(-1);
    }

    while (1) {
        if ((cl = accept(fd, nullptr, nullptr)) == -1) {
            perror("accept error");
            continue;
        }

        while (true) {
            pollfd pfd;
            pfd.fd = int(cl);
            // pfd.events = pfd.revents = 0;
            pfd.events = POLLIN;
            if (int ret = poll(&pfd, 1, 10000); ret < 0) {
                perror("Failed to poll");
                return -1;
            }
            printf("poll result: %2x\n", pfd.revents);

            rc = recv(int(cl), buf, sizeof(buf), 0);
            if (rc == -1) {
                perror("read");
                exit(-1);
            } else if (rc == 0) {
                printf("EOF\n");
                close(int(cl));
            }
            std::cout << "read " << rc << " bytes: ";
            for (int i = 0; i < rc; ++i) std::cout << std::hex << buf[i];
            std::cout << std::endl;
        }
    }
}

static int datagramversion() {
    const std::string NAME = "socket";

    int sock;
    struct sockaddr_un name;
    char buf[1024];

    /* Create socket from which to read. */
    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("opening datagram socket");
        exit(1);
    }

    /* Create name. */
    name.sun_family = AF_UNIX;
    strcpy(name.sun_path, NAME.c_str());

    /* Bind the UNIX domain address to the created socket */
    if (bind(sock, reinterpret_cast<struct sockaddr*>(&name), sizeof(struct sockaddr_un))) {
        perror("binding name to datagram socket");
        exit(1);
    }
    std::cerr << "socket -->" << NAME << std::endl;

    /* Read from the socket */
    while (true) {
        if (read(sock, buf, 1024) < 0) perror("receiving datagram packet");
        std::cout << "--> " << buf << std::endl;
    }
}

int main(int argc, char** argv) {
    if (argc == 1 || strcmp(argv[1], "stream") == 0) {
        return streamversion();
    } else if (strcmp(argv[1], "datagram") == 0) {
        return datagramversion();
    } else if (strcmp(argv[1], "sequence") == 0) {
        return sequence_version();
    }
}
