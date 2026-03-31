#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define PORT 5000
#define MAX_CLIENTS 20
#define BUFFER_SIZE 1024

typedef struct {
    int fd;
    int is_subscriber;
    char topic[50];
} Client;

int main() {
    int server_fd, new_socket, max_fd, activity, valread;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    Client clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = 0;
        clients[i].is_subscriber = 0;
        clients[i].topic[0] = '\0';
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Broker TCP escuchando en puerto %d...\n", PORT);

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        max_fd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = clients[i].fd;
            if (sd > 0)
                FD_SET(sd, &readfds);
            if (sd > max_fd)
                max_fd = sd;
        }

        activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            continue;
        }

        if (FD_ISSET(server_fd, &readfds)) {
            new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
            if (new_socket < 0) {
                perror("accept");
                continue;
            }

            printf("Nueva conexión: fd=%d\n", new_socket);

            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].fd == 0) {
                    clients[i].fd = new_socket;
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = clients[i].fd;

            if (sd > 0 && FD_ISSET(sd, &readfds)) {
                memset(buffer, 0, BUFFER_SIZE);
                valread = read(sd, buffer, BUFFER_SIZE - 1);

                if (valread <= 0) {
                    printf("Cliente desconectado: fd=%d\n", sd);
                    close(sd);
                    clients[i].fd = 0;
                    clients[i].is_subscriber = 0;
                    clients[i].topic[0] = '\0';
                } else {
                    buffer[valread] = '\0';
                    printf("Recibido: %s\n", buffer);

                    if (strncmp(buffer, "SUB|", 4) == 0) {
                        clients[i].is_subscriber = 1;
                        strncpy(clients[i].topic, buffer + 4, sizeof(clients[i].topic) - 1);
                        clients[i].topic[strcspn(clients[i].topic, "\n")] = '\0';
                        printf("Subscriber fd=%d suscrito a %s\n", sd, clients[i].topic);
                    }
                    else if (strncmp(buffer, "PUB|", 4) == 0) {
                        char temp[BUFFER_SIZE];
                        strcpy(temp, buffer);

                        char *token = strtok(temp, "|");
                        token = strtok(NULL, "|");
                        if (!token) continue;

                        char topic[50];
                        strcpy(topic, token);

                        token = strtok(NULL, "\n");
                        if (!token) continue;

                        char message[BUFFER_SIZE];
                        strcpy(message, token);

                        char out[BUFFER_SIZE];
                        snprintf(out, sizeof(out), "[%s] %s\n", topic, message);

                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (clients[j].fd > 0 &&
                                clients[j].is_subscriber &&
                                strcmp(clients[j].topic, topic) == 0) {
                                send(clients[j].fd, out, strlen(out), 0);
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}