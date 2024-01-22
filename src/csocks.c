#include <stdio.h>
#include "csocks.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <stddef.h>
#include <netdb.h>
#include <netinet/in.h>
#include "util.h"

int read_destination_socks4(int sock_client, uint8_t *dst_ip, uint16_t *dst_port)
{
    int nread;

    nread = readn(sock_client, (void *)dst_port, sizeof(*dst_port));
    if (nread < sizeof(*dst_port))
    {
        perror("dst_port?");
        return 1;
    }
    LOGGER("dst port %hu", ntohs(*dst_port));

    nread = readn(sock_client, dst_ip, 4);
    if (nread < 4)
    {
        perror("dst_ip?");
        return 1;
    }
    LOGGER("dst ip %hhu.%hhu.%hhu.%hhu", dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);

    return 0;
}
int read_ops(int sock_client, uint8_t *socks_version)
{
    uint8_t bytes[2];
    int nread = readn(sock_client, (void *)bytes, ARRAY_SIZE(bytes));
    if (nread != 2)
    {
        LOGGER("readn failed");
    }
    else if (bytes[0] != VERSION4 && bytes[0] != VERSION5)
    {
        LOGGER("version? %hhX %hhX", bytes[0], bytes[1]);
        close_connection(1, sock_client);
    }
    LOGGER("ver:%hhX cmd:%hhX", bytes[0], bytes[1]);
    *socks_version = bytes[0];
    return bytes[1];
}

int reply_noauth(int sock_client)
{
    uint8_t resp[2] = {VERSION5, 0x00};
    LOGGER("reply no authentication mode");
    writen(sock_client, (void *)resp, ARRAY_SIZE(resp));
}
int reply_connect_socks4(int sock_client, SOCKS_CMDS status)
{
    uint8_t resp[8] = {0x00, (uint8_t)status, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    writen(sock_client, (void *)resp, ARRAY_SIZE(resp));
}

int read_destination_socks5(int sock_client, SOCKS_ADDRESS_TYPES address_type, int *n_dst_addr, uint8_t *dst_addr, uint16_t *dst_port)
{
    int nread, err;
    switch (address_type)
    {
    case ATYPE_IPV4:
    {
        *n_dst_addr = 4;
        nread = readn(sock_client, dst_addr, 4);
        if (nread < 4)
        {
            perror("dst_ip?");
            return 1;
        }
        LOGGER("dstination ip:%hhu.%hhu.%hhu.%hhu", dst_addr[0], dst_addr[1], dst_addr[2], dst_addr[3]);

        break;
    }
    case ATYPE_DOMAINNAME:
    {
        uint8_t n_domain_address;
        nread = readn(sock_client, (void *)&n_domain_address, sizeof(n_domain_address));
        if (nread != sizeof(n_domain_address))
        {
            LOGGER("n_domain_address?");
        }
        *n_dst_addr = (int)n_domain_address;
        nread = readn(sock_client, dst_addr, *n_dst_addr);
        if (nread != *n_dst_addr)
        {
            perror("dst addr?");
            return 1;
        }
        dst_addr[*n_dst_addr] = '\0'; // null termination
        break;
    }
    default:
    {
        LOGGER("not implemented address type %hx", address_type);
        return 1;
    }
    }

    nread = readn(sock_client, (void *)dst_port, sizeof(*dst_port));
    if (nread != sizeof(*dst_port))
    {
        perror("dst_port?");
        return 1;
    }

    LOGGER("destination addr size=%d, addr=%s:%hu", *n_dst_addr, (char *)dst_addr, ntohs(*dst_port));

    return 0;
}
int reply_connect_socks5(int sock_client, SOCKS5_REPLYS reply_code)
{
    uint8_t resp[10] = {VERSION5, (char)reply_code, 0x00, ATYPE_IPV4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
    writen(sock_client, (void *)resp, ARRAY_SIZE(resp));
}

int get_relay_socket_socks5(SOCKS_ADDRESS_TYPES address_type, int n_dst_addr, uint8_t *dst_addr, uint16_t dst_port)
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
            LOGGER("failed to connect to dest");
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
        LOGGER("create relay socket(%d) to %s:%s", sock_relay, dst_addr, str_port);
        break;
    }
    default:
        LOGGER("not implemented address type %d", address_type);
        return -1;
    }
    return sock_relay;
}
int get_relay_socket_socks4(uint16_t dst_port, uint8_t *dst_ip)
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
        LOGGER("failed to connect to dest");
        close(sock_relay);
        return -1;
    }

    LOGGER("create relay socket(%d) to %hhu.%hhu.%hhu.%hhu:%hu", sock_relay, dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3], ntohs(dst_port));

    return sock_relay;
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

    switch (socks_version)
    {
    case VERSION5:
    {
        nread = readn(sock_client, (void *)&address_type, sizeof(address_type)); // ignore reserved byte

        nread = readn(sock_client, (void *)&address_type, sizeof(address_type));
        LOGGER("address type %hhx", address_type);

        if (nread != sizeof(address_type))
        {
            perror("address type?");
            err = 1;
        }
        err = read_destination_socks5(sock_client, address_type, &n_dst_addr, dst_addr, &dst_port);

        if (err != 0)
        {
            reply_connect_socks5(sock_client, R_GENERAL_FAIL);
            close_connection(1, sock_client);
        }
        sock_relay = get_relay_socket_socks5(address_type, n_dst_addr, dst_addr, dst_port);

        if (sock_relay == -1)
        {
            reply_connect_socks5(sock_client, R_GENERAL_FAIL);
            close_connection(1, sock_client);
        }
        reply_connect_socks5(sock_client, R_SUCCESS);
        break;
    }
    case VERSION4:
    {
        err = read_destination_socks4(sock_client, dst_ip, &dst_port);

        if (err != 0)
        {
            reply_connect_socks4(sock_client, C_REQUEST_REJECTED_OR_FAILED);
            close_connection(1, sock_client);
        }

        nread = read(sock_client, (void *)user_id, ARRAY_SIZE(user_id));
        LOGGER("user id=%s (size=%d)", (char *)user_id, nread);
        // however ignore user id...

        sock_relay = get_relay_socket_socks4(dst_port, dst_ip);

        if (sock_relay == -1)
        {
            reply_connect_socks4(sock_client, C_REQUEST_REJECTED_OR_FAILED);
            close_connection(1, sock_client);
        }
        reply_connect_socks4(sock_client, C_REQUEST_GRANTED);

        break;
    }
    }

    pipe_fd(sock_client, sock_relay);

    close(sock_relay);
    close_connection(0, sock_client);

    return 0;
}

int connection(int *ref_sock_client)
{
    int socks_command;
    char socks_version;
    int sock_client = *ref_sock_client;
    int nread;

    socks_command = read_ops(sock_client, &socks_version);

    // in socks5, need authentication negotiation
    if (socks_version == VERSION5)
    {
        int nmethods = socks_command;
        uint8_t tmp[256]; // max domain name length 255 + 1
        nread = readn(sock_client, tmp, nmethods);
        if (nread != nmethods)
        {
            LOGGER("authentication methods?");
            return -1;
        }
        LOGGER("negotiation: nmethods=%d", nmethods);
        for (int i = 0; i < nmethods; i++)
        {
            LOGGER("  - method=%hx", tmp[i]);
        }

        // authentication is not implemented...
        reply_noauth(sock_client);

        // now expect CONNECT method
        socks_command = read_ops(sock_client, &socks_version);
    }

    switch (socks_command)
    {
    case C_CONNECT:
    {
        LOGGER("CONNECT received");
        process_connect(sock_client, socks_version);
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
    int sock_server, sock_client;
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

    sock_server = socket(AF_INET, SOCK_STREAM, 0);
    int reuseaddr = 1;
    setsockopt(sock_server, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(csocks_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    status = bind(sock_server, (struct sockaddr *)&server_addr, sizeof(server_addr));

    if (status != 0)
    {
        LOGGER("bind failed status=%d,errno=%d", status, errno);
        exit(-1);
    }

    status = listen(sock_server, MAX_CLIENT_NUM);
    if (status != 0)
    {
        LOGGER("listen failed status=%d,errno=%d", status, errno);
        exit(-1);
    }

    LOGGER("listening on port %d...", csocks_port);

    while (1)
    {
        pthread_t t_connection;
        LOGGER("waiting for client...");
        sock_client = accept(sock_server, (struct sockaddr *)&client_addr, &client_addr_len);
        if (sock_client < 0)
        {
            LOGGER("accept failed");
            continue;
        }
        LOGGER("accept from %s fd=%d", inet_ntoa(client_addr.sin_addr), sock_client);

        int nodelay = 1;
        setsockopt(sock_client, SOL_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        if (pthread_create(&t_connection, NULL, (void *)connection, (void *)&sock_client) != 0)
        {
            LOGGER("pthread create failed");
            continue;
        }

        pthread_detach(t_connection);
    }

    return 0;
}