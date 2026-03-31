#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 5000
#define BUFFER_SIZE 1024

int main() {
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    char topic[50];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        return 1;
    }

    printf("Conectado al broker TCP.\n");
    printf("Tema a suscribirse: ");
    fgets(topic, sizeof(topic), stdin);
    topic[strcspn(topic, "\n")] = '\0';

    snprintf(buffer, sizeof(buffer), "SUB|%s", topic);
    send(sock, buffer, strlen(buffer), 0);

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int n = read(sock, buffer, sizeof(buffer) - 1);
        if (n <= 0) break;
        printf("Recibido: %s", buffer);
    }

    close(sock);
    return 0;
}