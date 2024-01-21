#include <stdio.h>
#include "csocks.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/select.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <stddef.h>
#include <netdb.h>
#include <netinet/in.h>

void close_connection(int retval, int fd)
{
    LOGGER("close connection socket=%d, retval=%d", fd, retval);
    close(fd);
    pthread_exit((void *)&retval);
}

int read_destination_socks4(int client_sock, uint8_t *dst_ip, uint16_t *dst_port)
{
    int nread;

    nread = readn(client_sock, (void *)dst_port, sizeof(*dst_port));
    if (nread == sizeof(*dst_port))
    {

        LOGGER("dst port %hu", ntohs(*dst_port));
    }
    else
    {
        perror("dst_port?");
        return 1;
    }

    nread = readn(client_sock, dst_ip, 4);
    if (nread == 4)
    {
        LOGGER("dst ip %hhu.%hhu.%hhu.%hhu", dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
    }
    else
    {
        perror("dst_ip?");
        return 1;
    }
    return 0;
}
int read_ops(int client_sock, char *socks_version)
{
    char bytes[2];
    int nread = readn(client_sock, (void *)bytes, ARRAY_SIZE(bytes));
    if (nread != 2)
    {
        LOGGER("readn failed");
    }
    else if (bytes[0] != VERSION4 && bytes[0] != VERSION5)
    {
        LOGGER("version?");
        LOGGER("%hhX %hhX", bytes[0], bytes[1]);
        close_connection(1,client_sock);
    }
    LOGGER("ver:%hhX cmd:%hhX", bytes[0], bytes[1]);
    *socks_version = bytes[0];
    return bytes[1];
}

int noauth_reply(int client_sock)
{
    char resp[2] = {VERSION5, 0x00};
    writen(client_sock, (void *)resp, ARRAY_SIZE(resp));
}
int connect_reply(int client_sock, SOCKS_CMDS status)
{
    char resp[8] = {0x00, (char)status, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    writen(client_sock, (void *)resp, ARRAY_SIZE(resp));
}

int read_destination_socks5(int client_sock, uint8_t address_type, int *n_dst_addr, uint8_t *dst_addr, uint16_t *dst_port)
{
    int nread, err;
    switch (address_type)
    {
    case ATYPE_IPV4:
    {
        *n_dst_addr = 4;
        nread = readn(client_sock, dst_addr, 4);
        if (nread == 4)
        {
            LOGGER("dst ip %hhu.%hhu.%hhu.%hhu", dst_addr[0], dst_addr[1], dst_addr[2], dst_addr[3]);
        }
        else
        {
            perror("dst_ip?");
            return 1;
        }
        break;
    }
    case ATYPE_DOMAINNAME:
    {
        uint8_t n_domain_address;
        nread = readn(client_sock, (void *)&n_domain_address, sizeof(n_domain_address));
        if (nread != sizeof(n_domain_address))
        {
            LOGGER("n_domain_address?");
        }
        *n_dst_addr = (int)n_domain_address;
        LOGGER("n_dst_addr:%d", *n_dst_addr);
        nread = readn(client_sock, dst_addr, *n_dst_addr);
        dst_addr[*n_dst_addr] = '\0'; // null termination
        if (nread == *n_dst_addr)
        {
            LOGGER("dst addr %s", (char *)dst_addr);
        }
        else
        {
            perror("dst addr?");
            return 1;
        }
        break;
    }
    default:
    {
        LOGGER("not implemented address type %hx", address_type);
        return 1;
    }
    }

    nread = readn(client_sock, (void *)dst_port, sizeof(*dst_port));
    if (nread == sizeof(*dst_port))
    {
        LOGGER("dst port %hu", ntohs(*dst_port));
    }
    else
    {
        perror("dst_port?");
        return 1;
    }

    return 0;
}
int connect_reply_socks5(int client_sock, SOCKS5_REPLYS reply_code)
{
    char resp[10] = {VERSION5, (char)reply_code, 0x00, ATYPE_IPV4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
    writen(client_sock, (void *)resp, ARRAY_SIZE(resp));
}

int fail_reply_socks5(int client_sock)
{
    char resp[10] = {VERSION5, R_GENERAL_FAIL, 0x00, ATYPE_IPV4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    writen(client_sock, (void *)resp, ARRAY_SIZE(resp));
}

int get_relay_sock_socks5(SOCKS_ADDRESS_TYPES address_type, int n_dst_addr, uint8_t *dst_addr, uint16_t dst_port)
{
    int sock_relay;

    char str_ip[16];

    switch (address_type)
    {
    case ATYPE_IPV4:
    {
        struct sockaddr_in dst;
        memset(str_ip, 0, ARRAY_SIZE(str_ip));
        snprintf(str_ip, ARRAY_SIZE(str_ip), "%hhu.%hhu.%hhu.%hhu",
                 dst_addr[0], dst_addr[1], dst_addr[2], dst_addr[3]);
        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_addr.s_addr = inet_addr(str_ip);
        dst.sin_port = dst_port;

        sock_relay = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(sock_relay, (struct sockaddr *)&dst, sizeof(dst)) < 0)
        {
            LOGGER("connect failed");
            close(sock_relay);
            return -1;
        }

        LOGGER("create relay socket(%d) to %s:%hu", sock_relay, str_ip, ntohs(dst_port));
        break;
    }
    case ATYPE_DOMAINNAME:
    {
        char str_port[6];
        struct addrinfo *dst;

        snprintf(str_port, ARRAY_SIZE(str_port), "%hd", ntohs(dst_port));

        LOGGER("create relay socket to %s:%s", dst_addr, str_port);

        int status = getaddrinfo((char *)dst_addr, str_port, NULL, &dst);
        if (status == EAI_NONAME)
        {
            return -1;
        }
        else if (status == 0)
        {
            struct addrinfo *r;
            for (r = dst; r != NULL; r = r->ai_next)
            {
                sock_relay = socket(r->ai_family, r->ai_socktype,
                                    r->ai_protocol);
                if (sock_relay == -1)
                {
                    continue;
                }
                LOGGER("connect to %s", r->ai_canonname);
                status = connect(sock_relay, r->ai_addr, r->ai_addrlen);
                if (status == 0)
                {
                    freeaddrinfo(dst);
                    return sock_relay;
                }
                else
                {
                    close(sock_relay);
                }
            }
        }
        break;
    }
    default:
        LOGGER("not implemented address type %d", address_type);
        return -1;
    }
    return sock_relay;
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

    LOGGER("create relay socket(%d) to %hhu.%hhu.%hhu.%hhu:%hu", sock_relay, dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3], ntohs(dst_port));

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
    maxfd = (fd1 > fd2) ? fd1 : fd2;
    while (1)
    {
        FD_ZERO(&read_fds);
        FD_SET(fd1, &read_fds);
        FD_SET(fd2, &read_fds);

        if (select(maxfd + 1, &read_fds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            return 1;
        }

        if (FD_ISSET(fd1, &read_fds))
        {
            LOGGER("fd1(%d) to fd2(%d)", fd1, fd2);
            if (transfer(fd1, fd2) == 0)
                return 0;
        }

        if (FD_ISSET(fd2, &read_fds))
        {
            LOGGER("fd2(%d) to fd1(%d)", fd2, fd1);
            if (transfer(fd2, fd1) == 0)
                return 0;
        }
    }
}

int process_connect(int sock_client, SOCKS_VERSIONS socks_version)
{
    uint16_t dst_port;
    uint8_t dst_ip[4];
    uint8_t address_type;
    int n_dst_addr;
    uint8_t dst_addr[DOMAINNAME_BUFFER_SIZE];
    uint8_t user_id[USER_ID_BUFFER_SIZE];
    int sock_relay;
    int nread;
    int err = 0;

    if (socks_version == VERSION5)
    {
        LOGGER("process socks5");

        // ignore reserved byte
        nread = readn(sock_client, (void *)&address_type, sizeof(address_type));
        LOGGER("rsv %hhx", address_type);

        nread = readn(sock_client, (void *)&address_type, sizeof(address_type));
        LOGGER("address type %hhx", address_type);

        if (nread != sizeof(address_type))
        {
            perror("address type?");
            err = 1;
        }
        err = read_destination_socks5(sock_client, address_type, &n_dst_addr, dst_addr, &dst_port);

        if (err)
        {
            fail_reply_socks5(sock_client);
            close_connection(1, sock_client);
        }

        sock_relay = get_relay_sock_socks5(address_type, n_dst_addr, dst_addr, dst_port);

        if (sock_relay != -1)
        {
            connect_reply_socks5(sock_client, R_SUCCESS);
        }
        else
        {
            fail_reply_socks5(sock_client);
            close_connection(1, sock_client);
        }
    }
    else if (socks_version == VERSION4)
    {
        err = read_destination_socks4(sock_client, dst_ip, &dst_port);

        if (err)
        {
            connect_reply(sock_client, C_REQUEST_REJECTED_OR_FAILED);
            close_connection(1, sock_client);
        }

        nread = read(sock_client, (void *)user_id, ARRAY_SIZE(user_id));
        LOGGER("user id: %s (size = %d)", user_id, nread);
        // however ignore user id...

        sock_relay = get_relay_sock(dst_port, dst_ip);

        if (sock_relay != -1)
        {
            connect_reply(sock_client, C_REQUEST_GRANTED);
        }
        else
        {
            connect_reply(sock_client, C_REQUEST_REJECTED_OR_FAILED);
            close_connection(1, sock_client);
        }
    }

    pipe_fd(sock_client, sock_relay);
    close(sock_relay);
    close_connection(0, sock_client);
    return 0;
}

int connection(int *client_sock_ref)
{
    int socks_command;
    char socks_version;
    int client_sock = *client_sock_ref;
    int nread;
    LOGGER("client sock: %d", client_sock);

    socks_command = read_ops(client_sock, &socks_version);

    if (socks_version == VERSION5)
    {
        // in socks5, first packet is authentication negothiation...
        int nmethods = socks_command;
        uint8_t tmp[256];
        nread = readn(client_sock, tmp, nmethods);
        if (nread != nmethods)
        {
            LOGGER("authentication methods?");
            return -1;
        }
        LOGGER("nmethods:%d", nmethods);
        for (int i = 0; i < nmethods; i++)
        {
            LOGGER("method:%hx", tmp[i]);
        }
        // authentication is not implemented...
        LOGGER("no authentication mode");
        noauth_reply(client_sock);
        socks_command = read_ops(client_sock, &socks_version);
    }

    switch (socks_command)
    {
    case C_CONNECT:
    {
        LOGGER("CONNECT received");

        process_connect(client_sock, socks_version);

        break;
    }
    case C_BIND:
    {
        LOGGER("BIND received");
        LOGGER("BIND is not implemented...");
        return 1;
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
    int csocks_port;
    int status;

    if (argc == 1)
    {
        csocks_port = CSOCKS_PORT_DEFAULT;
    }
    else
    {
        csocks_port = atoi(argv[1]);
    }

    // TCP socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int reuseaddr = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
    LOGGER("Server socket is %d", server_sock);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(csocks_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    status = bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    if (status != 0)
    {
        LOGGER("bind failed status=%d,errno=%d", status, errno);
        exit(-1);
    }

    status = listen(server_sock, MAX_CLIENT_NUM);
    if (status != 0)
    {
        LOGGER("listen failed status=%d,errno=%d", status, errno);
        exit(-1);
    }

    LOGGER("Server listening on port %d...", csocks_port);

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

        if (pthread_create(&t_connection, NULL, (void *)connection, (void *)&client_sock) == 0)
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