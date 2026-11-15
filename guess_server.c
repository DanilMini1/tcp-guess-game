#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <time.h>
#include "guess_protocol.h"

static void play_game(int client_fd, const char *address)
{
    int secret = rand() % GAME_MAXIMUM + 1;
    unsigned long long attempts = 0;
    char line[LINE_CAPACITY];
    char reply[LINE_CAPACITY];
    snprintf(reply, sizeof(reply), "RANGE %d\n", GAME_MAXIMUM);
    printf("%s:GAME_START range=1..%d\n", address, GAME_MAXIMUM);
    if (send_text(client_fd, reply) == -1) {
        printf("%s:GAME_ABORT send=%s\n", address, strerror(errno));
        return;
    }
    for (;;) {
        int received = receive_line(client_fd, line, sizeof(line));
        if (received != 1) {
            if (received == -1)
                printf("%s:GAME_ABORT recv=%s attempts=%llu\n",
                       address, strerror(errno), attempts);
            else
                printf("%s:GAME_ABORT reason=%s attempts=%llu\n", address,
                       received == 0 ? "client_closed" : "invalid_line", attempts);
            return;
        }
        int guess;
        if (strncmp(line, "GUESS ", 6) != 0 ||
            parse_positive(line + 6, GAME_MAXIMUM, &guess) == -1) {
            printf("%s:ERROR invalid_guess\n", address);
            if (send_text(client_fd, "ERROR invalid_guess\n") == -1)
                printf("%s:ERROR send=%s\n", address, strerror(errno));
            return;
        }
        ++attempts;
        int correct = guess == secret;
        printf("%s:TRY attempt=%llu guess=%d result=%s\n", address,
               attempts, guess, correct ? "correct" : "wrong");
        if (correct)
            snprintf(reply, sizeof(reply), "CORRECT %llu\n", attempts);
        else
            snprintf(reply, sizeof(reply), "WRONG\n");
        if (send_text(client_fd, reply) == -1) {
            printf("%s:GAME_ABORT send=%s\n", address, strerror(errno));
            return;
        }
        if (correct) {
            printf("%s:GAME_END secret=%d attempts=%llu\n", address, secret, attempts);
            return;
        }
    }
}

int main(int argc, char **argv)
{
    int port;
    if (argc != 2 || parse_positive(argv[1], 65535, &port) == -1) {
        fprintf(stderr, "Usage: %s PORT (1..65535)\n", argv[0]);
        return 2;
    }
    if (configure_signals() == -1)
        return 1;
    if (setvbuf(stdout, NULL, _IOLBF, 0) != 0) {
        fputs("Cannot configure server log.\n", stderr);
        return 1;
    }
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == -1) {
        perror("socket");
        return 1;
    }
    int reuse = 1;
    struct sockaddr_in server = {
        .sin_family = AF_INET,
        .sin_port = htons((unsigned short)port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1 ||
        bind(listener, (struct sockaddr *)&server, sizeof(server)) == -1 ||
        listen(listener, 16) == -1) {
        perror("server setup");
        close_socket(listener);
        return 1;
    }
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    for (;;) {
        struct sockaddr_in client;
        socklen_t address_size = sizeof(client);
        int client_fd = accept(listener, (struct sockaddr *)&client, &address_size);
        if (client_fd == -1) {
            if (errno == EINTR)
                continue;
            perror("accept");
            close_socket(listener);
            return 1;
        }
        char address[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &client.sin_addr, address, sizeof(address)) == NULL) {
            perror("inet_ntop");
            close_socket(client_fd);
            continue;
        }
        printf("%s:CONNECT port=%u\n", address, (unsigned int)ntohs(client.sin_port));
        play_game(client_fd, address);
        if (shutdown(client_fd, SHUT_RDWR) == -1 && errno != ENOTCONN)
            printf("%s:ERROR shutdown=%s\n", address, strerror(errno));
        if (close_socket(client_fd) == -1)
            printf("%s:ERROR close_failed\n", address);
        printf("%s:DISCONNECT port=%u\n", address, (unsigned int)ntohs(client.sin_port));
    }
}
