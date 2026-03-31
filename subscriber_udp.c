/*
 * subscriber_udp.c
 * Suscriptor del sistema pub-sub usando UDP.
 *
 * Se registra ante el broker enviando un mensaje SUB,
 * y luego espera datagramas entrantes con las actualizaciones.
 *
 * Protocolo:
 *   Envia al broker:  "SUB <tema>"
 *   Recibe del broker: "[<tema>] <mensaje>"
 *
 * IMPORTANTE (UDP): el suscriptor debe hacer bind() en un puerto
 * local ANTES de enviar el SUB, para que el broker conozca su
 * direccion de destino a traves del campo origen del datagrama.
 *
 * Compilar: gcc -o subscriber_udp subscriber_udp.c
 * Ejecutar: ./subscriber_udp <puerto_local> <tema>
 *   Ejemplo: ./subscriber_udp 8001 ColombiaBrasil
 *            ./subscriber_udp 8002 ArgentinaChile
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* ── Constantes ─────────────────────────────────────────── */
#define BROKER_IP    "127.0.0.1"
#define BROKER_PORT  9090
#define MAX_BUFFER   1024

/* ── main ───────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr,
                "Uso: %s <puerto_local> <tema>\n"
                "  Ej: %s 8001 ColombiaBrasil\n",
                argv[0], argv[0]);
        exit(EXIT_FAILURE);
    }

    int         local_port = atoi(argv[1]);
    const char *topic      = argv[2];
    char        buffer[MAX_BUFFER];

    /* 1. Crear socket UDP */
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    /*
     * 2. Bind en un puerto local conocido.
     *
     * Esto es FUNDAMENTAL en UDP: el broker usa recvfrom() para
     * obtener la direccion (IP + puerto) del remitente y luego
     * le enviara los mensajes via sendto(). Sin bind() el SO
     * asigna un puerto efimero distinto en cada envio, por lo
     * que el broker no podria localizar al suscriptor.
     */
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family      = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port        = htons(local_port);

    if (bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind"); close(sockfd); exit(EXIT_FAILURE);
    }

    /* 3. Configurar direccion del broker */
    struct sockaddr_in broker_addr;
    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port   = htons(BROKER_PORT);
    if (inet_aton(BROKER_IP, &broker_addr.sin_addr) == 0) {
        fprintf(stderr, "IP del broker invalida: %s\n", BROKER_IP);
        close(sockfd); exit(EXIT_FAILURE);
    }

    /*
     * 4. Enviar mensaje de suscripcion al broker.
     *
     * El broker recibira este datagrama con recvfrom() y almacenara
     * la direccion origen (local_port) para enviar futuros mensajes.
     */
    int len = snprintf(buffer, MAX_BUFFER, "SUB %s", topic);
    if (sendto(sockfd, buffer, len, 0,
               (struct sockaddr *)&broker_addr, sizeof(broker_addr)) < 0) {
        perror("sendto SUB"); close(sockfd); exit(EXIT_FAILURE);
    }

    printf("[SUBSCRIBER UDP] Suscrito al tema '%s' en puerto local %d\n",
           topic, local_port);
    printf("[SUBSCRIBER UDP] Esperando actualizaciones...\n\n");

    /* 5. Bucle de recepcion de mensajes */
    struct sockaddr_in sender_addr;
    socklen_t          addr_len = sizeof(sender_addr);

    while (1) {
        memset(buffer, 0, MAX_BUFFER);

        /*
         * recvfrom: bloquea hasta recibir un datagrama.
         * sender_addr contendra la IP/puerto del remitente (el broker).
         * En UDP cada datagrama es independiente; puede llegar
         * desordenado o perderse sin notificacion alguna.
         */
        ssize_t n = recvfrom(sockfd, buffer, MAX_BUFFER - 1, 0,
                             (struct sockaddr *)&sender_addr, &addr_len);
        if (n < 0) {
            perror("recvfrom"); continue;
        }
        buffer[n] = '\0';

        printf("[SUBSCRIBER] Actualizacion recibida: %s\n", buffer);
    }

    close(sockfd);
    return 0;
}
