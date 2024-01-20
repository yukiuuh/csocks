#include "unistd.h"

#define CSOCKS_PORT 11111
#define USER_ID_BUFFER_SIZE 1024
#define MSGSIZE 1024
#define BUFSIZE (MSGSIZE + 1)
#define MAX_CLIENT_NUM 100
#define LOGGER(...) printf("(%ld) %s(%d) %s: ", pthread_self(), __FILE__, __LINE__, __func__), printf(__VA_ARGS__), printf("\n")

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

typedef enum
{
    VERSION4 = 4
} SOCKS_VERSIONS;

typedef enum
{
    C_CONNECT = 1,
    C_BIND = 2,
    C_REQUEST_GRANTED = 90,
    C_REQUEST_REJECTED_OR_FAILED = 91,
    C_REQUEST_REJECTED_SERVER_CONNECT = 92,
    C_REQUEST_REJECTED_DIFFERENT_USER_IDS = 93
} SOCKS_CMDS;

ssize_t /* Read "n" bytes from a descriptor  */
readn(int fd, void *ptr, size_t n)
{
    size_t nleft;
    ssize_t nread;

    nleft = n;
    while (nleft > 0)
    {
        if ((nread = read(fd, ptr, nleft)) < 0)
        {
            if (nleft == n)
                return (-1); /* error, return -1 */
            else
                break; /* error, return amount read so far */
        }
        else if (nread == 0)
        {
            break; /* EOF */
        }
        nleft -= nread;
        ptr += nread;
    }
    return (n - nleft); /* return >= 0 */
}

ssize_t /* Write "n" bytes to a descriptor  */
writen(int fd, const void *ptr, size_t n)
{
    size_t nleft;
    ssize_t nwritten;

    nleft = n;
    while (nleft > 0)
    {
        if ((nwritten = write(fd, ptr, nleft)) < 0)
        {
            if (nleft == n)
                return (-1); /* error, return -1 */
            else
                break; /* error, return amount written so far */
        }
        else if (nwritten == 0)
        {
            break;
        }
        nleft -= nwritten;
        ptr += nwritten;
    }
    return (n - nleft); /* return >= 0 */
}
