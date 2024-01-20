#include <stdio.h>
#include "csocks.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/select.h>

int get_ops(int client_sock, char *socks_version)
{
    char bytes[2];
    int nread = readn(client_sock, (void *)bytes, ARRAY_SIZE(bytes));
    if (nread != 2)
    {
        LOGGER("readn failed");
    }
    else if (bytes[0] != VERSION4)
    {
        LOGGER("version?");
        LOGGER("%hhX %hhX", bytes[0], bytes[1]);
        close(client_sock);
        pthread_exit(0);
    }
    LOGGER("%hhX %hhX", bytes[0], bytes[1]);
    *socks_version = bytes[0];
    return bytes[1];
}
int connect_reply(int client_sock, SOCKS_CMDS status)
{
    char resp[8] = {0x00, (char)status, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    writen(client_sock, (void *)resp, ARRAY_SIZE(resp));
}
int get_relay_sock(uint16_t dst_port, uint8_t *dst_ip)
{
    int sock_relay;
    struct sockaddr_in dst;
    char str_ip[16];

    memset(str_ip, 0, ARRAY_SIZE(str_ip));
    snprintf(str_ip, ARRAY_SIZE(str_ip), "%hhu.%hhu.%hhu.%hhu",
             dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr(str_ip);
    dst.sin_port = dst_port;

    sock_relay = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sock_relay, (struct sockaddr *)&dst, sizeof(dst)) < 0)
    {
        LOGGER("connect() in app_connect");
        close(sock_relay);
        return -1;
    }

    LOGGER("create socket success fd: %d", sock_relay);

    return sock_relay;
}

int transfer(int fd_in, int fd_out)
{
    char buffer[MSGSIZE];
    ssize_t nread;

    nread = read(fd_in, buffer, MSGSIZE);
    if (nread > 0)
    {
        write(fd_out, buffer, nread);
    }

    return nread;
}

int pipe_fd(int fd1, int fd2)
{
    int maxfd;
    fd_set read_fds;
    // maxfd = (fd1 > fd2) ? fd1 : fd2;
    while (1)
    {
        FD_ZERO(&read_fds);
        FD_SET(fd1, &read_fds);
        FD_SET(fd2, &read_fds);

        if (select(FD_SETSIZE, &read_fds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            return 1;
        }

        if (FD_ISSET(fd1, &read_fds))
        {
            LOGGER("fd1 to fd2");
            if (transfer(fd1, fd2) == 0)
                return 0;
        }

        if (FD_ISSET(fd2, &read_fds))
        {
            LOGGER("fd2 to fd1");
            if (transfer(fd2, fd1) == 0)
                return 0;
        }
    }
}

int process_connect(int client_sock)
{
    uint16_t dst_port;
    uint8_t dst_ip[4];
    char user_id[USER_ID_BUFFER_SIZE];
    int relay_sock;

    int nread;
    int err = 0;

    nread = readn(client_sock, (void *)&dst_port, sizeof(dst_port));
    if (nread == sizeof(dst_port))
    {

        LOGGER("dst port %hu", ntohs(dst_port));
    }
    else
    {
        perror("dst_port?");
        err = 1;
    }

    nread = readn(client_sock, (void *)dst_ip, ARRAY_SIZE(dst_ip));
    if (nread == ARRAY_SIZE(dst_ip))
    {
        LOGGER("dst ip %hhu.%hhu.%hhu.%hhu", dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
    }
    else
    {
        perror("dst_ip?");
        err = 1;
    }

    if (err)
    {
        connect_reply(client_sock, C_REQUEST_REJECTED_OR_FAILED);
        close(client_sock);
        pthread_exit(1);
    }

    nread = read(client_sock, (void *)user_id, ARRAY_SIZE(user_id));
    LOGGER("user id: %s (size = %d)", user_id, nread);
    // however ignore user id...

    relay_sock = get_relay_sock(dst_port, dst_ip);

    if (relay_sock != -1)
    {
        connect_reply(client_sock, C_REQUEST_GRANTED);
    }
    else
    {
        connect_reply(client_sock, C_REQUEST_REJECTED_OR_FAILED);
        close(client_sock);
        pthread_exit(1);
    }

    pipe_fd(client_sock, relay_sock);
    close(relay_sock);
    close(client_sock);
    LOGGER("%lu", pthread_self());
    pthread_exit(0);
    return 0;
}

int connection(int *client_sock_ref)
{
    int socks_command;
    char socks_version;
    int client_sock = *client_sock_ref;
    LOGGER("client sock: %d", client_sock);

    socks_command = get_ops(client_sock, &socks_version);

    switch (socks_command)
    {
    case C_CONNECT:
    {
        LOGGER("CONNECT received");

        process_connect(client_sock);

        break;
    }
    case C_BIND:
    {
        LOGGER("BIND received");

        break;
    }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buf[BUFSIZE];


    // TCP socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    LOGGER("Server socket is %d", server_sock);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(CSOCKS_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    listen(server_sock, MAX_CLIENT_NUM);
    LOGGER("Server listening on port %d...", CSOCKS_PORT);

    while (1)
    {
        pthread_t t_connection;
        LOGGER("waiting for client...");
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        LOGGER("accept... client sock:%d", client_sock);
        if (client_sock < 0)
        {
            perror("accept() failed");
        }
        LOGGER("Handling client %s", inet_ntoa(client_addr.sin_addr));

        int nodelay = 1;
        setsockopt(client_sock, SOL_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        if (pthread_create(&t_connection, NULL, connection, (void *)&client_sock) == 0)
        {
            LOGGER("pthread_create()");
            pthread_detach(t_connection);
        }
        else
        {
            perror("pthread_create() failed");
        }
        LOGGER("end loop...");
    }

    return 0;
}