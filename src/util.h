#include "unistd.h"
#include <sys/select.h>
#include <pthread.h>
#include <stdio.h>
#define BUFSIZE 1024
#define LOGGER(...) printf("(%lx) %s(%d) %s: ", pthread_self(), __FILE__, __LINE__, __func__), printf(__VA_ARGS__), printf("\n")

void close_connection(int retval, int fd)
{
    LOGGER("close connection socket=%d, retval=%d", fd, retval);
    close(fd);
    pthread_exit((void *)&retval);
}

int transfer(int fd_in, int fd_out)
{
    char buffer[BUFSIZE];
    ssize_t nread;

    nread = read(fd_in, buffer, BUFSIZE);
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
            // LOGGER("fd1(%d) to fd2(%d)", fd1, fd2);
            if (transfer(fd1, fd2) == 0)
                return 0;
        }

        if (FD_ISSET(fd2, &read_fds))
        {
            // LOGGER("fd2(%d) to fd1(%d)", fd2, fd1);
            if (transfer(fd2, fd1) == 0)
                return 0;
        }
    }
}

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
