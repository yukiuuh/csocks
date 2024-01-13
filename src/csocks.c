#include <stdio.h>
#include "csocks.h"
#include <arpa/inet.h>

int main(int argc, char *argv[])

{
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buf[1024];

    // TCP socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    printf("Server socket is %d\n", server_sock);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(CSOCKS_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    listen(server_sock, 5);
    printf("Server listening on port %d...\n", CSOCKS_PORT);

    for (;;)
    {

        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sock < 0)
        {
            perror("accept() failed");
        }
        printf("Handling client %s\n", inet_ntoa(client_addr.sin_addr));
        handle_connection(client_sock);
    }

    return 0;
}