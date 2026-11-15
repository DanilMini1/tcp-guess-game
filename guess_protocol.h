#ifndef GUESS_PROTOCOL_H
#define GUESS_PROTOCOL_H

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define GAME_MAXIMUM 100
#define LINE_CAPACITY 128

static int parse_positive(const char *text, int maximum, int *value)
{
    char *end;
    errno = 0;
    long number = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || number < 1 || number > maximum)
        return -1;
    *value = (int)number;
    return 0;
}

static int configure_signals(void)
{
    struct sigaction action = {.sa_handler = SIG_DFL};
    if (sigemptyset(&action.sa_mask) == -1 ||
        sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1) {
        perror("sigaction");
        return -1;
    }
    return 0;
}

static int send_text(int socket_fd, const char *text)
{
    size_t length = strlen(text);
    size_t offset = 0;
    while (offset < length) {
        ssize_t count = send(socket_fd, text + offset, length - offset, MSG_NOSIGNAL);
        if (count == -1) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (count == 0) {
            errno = EPIPE;
            return -1;
        }
        offset += (size_t)count;
    }
    return 0;
}

/* One complete line is consumed even when TCP splits or combines messages. */
static int receive_line(int socket_fd, char *line, size_t capacity)
{
    size_t length = 0;
    for (;;) {
        unsigned char character;
        ssize_t count = recv(socket_fd, &character, 1, 0);
        if (count == -1) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (count == 0)
            return length == 0 ? 0 : -2;
        if (character == '\n') {
            if (length > 0 && line[length - 1] == '\r')
                --length;
            line[length] = '\0';
            return 1;
        }
        if (character == '\0' || length + 1 >= capacity)
            return -2;
        line[length++] = (char)character;
    }
}

static int close_socket(int socket_fd)
{
    if (close(socket_fd) == -1) {
        perror("close socket");
        return -1;
    }
    return 0;
}

#endif
