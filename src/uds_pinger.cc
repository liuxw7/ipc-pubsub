#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <iostream>
#include <string>

int main() {
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
    if (read(sock, buf, 1024) < 0) perror("receiving datagram packet");
    printf("-->%s\n", buf);
    close(sock);
    unlink(NAME.c_str());
}
