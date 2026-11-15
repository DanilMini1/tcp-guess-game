#define _POSIX_C_SOURCE 200809L
#include <netdb.h>
#include "guess_protocol.h"

static int connect_server(const char *host, const char *service)
{
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM,
                             .ai_flags = AI_NUMERICSERV};
    struct addrinfo *addresses;
    int error = getaddrinfo(host, service, &hints, &addresses);
    if (error != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        return -1;
    }
    int connection = -1;
    int saved_error = ECONNREFUSED;
    for (struct addrinfo *address = addresses; address != NULL; address = address->ai_next) {
        int socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd == -1) {
            saved_error = errno;
            continue;
        }
        if (connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0) {
            connection = socket_fd;
            break;
        }
        saved_error = errno;
        close_socket(socket_fd);
    }
    freeaddrinfo(addresses);
    if (connection == -1) {
        errno = saved_error;
        perror("connect");
    }
    return connection;
}

static int play_game(int connection)
{
    char line[LINE_CAPACITY];
    int maximum;
    if (receive_line(connection, line, sizeof(line)) != 1 ||
        strncmp(line, "RANGE ", 6) != 0 ||
        parse_positive(line + 6, INT_MAX, &maximum) == -1) {
        fputs("Invalid or missing server greeting.\n", stderr);
        return 1;
    }
    printf("RANGE 1..%d\n", maximum);
    for (int guess = 1; ; ++guess) {
        char request[LINE_CAPACITY];
        snprintf(request, sizeof(request), "GUESS %d\n", guess);
        if (send_text(connection, request) == -1) {
            perror("send guess");
            return 1;
        }
        if (receive_line(connection, line, sizeof(line)) != 1) {
            fputs("Server disconnected or sent an invalid reply.\n", stderr);
            return 1;
        }
        if (strcmp(line, "WRONG") == 0) {
            printf("TRY attempt=%d guess=%d result=wrong\n", guess, guess);
        } else if (strncmp(line, "CORRECT ", 8) == 0) {
            int attempts;
            if (parse_positive(line + 8, INT_MAX, &attempts) == -1 || attempts != guess) {
                fputs("Invalid attempt count from server.\n", stderr);
                return 1;
            }
            printf("TRY attempt=%d guess=%d result=correct\n", attempts, guess);
            printf("RESULT number=%d attempts=%d\n", guess, attempts);
            return 0;
        } else {
            fprintf(stderr, "Unexpected server reply: %s\n", line);
            return 1;
        }
        if (guess == maximum) {
            fputs("Server rejected every number in the advertised range.\n", stderr);
            return 1;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s HOST:PORT\n", argv[0]);
        return 2;
    }
    const char *separator = strrchr(argv[1], ':');
    char host[256];
    int port;
    if (separator == NULL || separator == argv[1] ||
        strchr(argv[1], ':') != separator ||
        (size_t)(separator - argv[1]) >= sizeof(host) ||
        parse_positive(separator + 1, 65535, &port) == -1) {
        fputs("Expected an IPv4 host and port in HOST:PORT format.\n", stderr);
        return 2;
    }
    size_t host_length = (size_t)(separator - argv[1]);
    memcpy(host, argv[1], host_length);
    host[host_length] = '\0';
    char service[6];
    snprintf(service, sizeof(service), "%d", port);
    if (configure_signals() == -1)
        return 1;
    int connection = connect_server(host, service);
    if (connection == -1)
        return 1;
    printf("CONNECTED server=%s:%d\n", host, port);
    int result = play_game(connection);
    if (close_socket(connection) == -1)
        result = 1;
    return result;
}
