/*
 * publisher_udp.c
 * Publicador del sistema pub-sub usando UDP.
 *
 * Envia eventos de un partido al broker mediante datagramas UDP.
 * El broker redirige cada mensaje a los suscriptores registrados.
 *
 * Protocolo enviado al broker:
 *   "PUB <tema> <mensaje>"
 *
 * Compilar: gcc -o publisher_udp publisher_udp.c
 * Ejecutar: ./publisher_udp <tema>
 *   Ejemplo: ./publisher_udp "ColombiaBrasil"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* ── Constantes ─────────────────────────────────────────── */
#define BROKER_IP      "127.0.0.1"  /* IP del broker (localhost en pruebas) */
#define BROKER_PORT    9090         /* Puerto del broker                     */
#define MAX_BUFFER     1024

/* ── Eventos de ejemplo ──────────────────────────────────── */
/*
 * Lista de 11 eventos deportivos para cumplir el minimo de 10 mensajes
 * requerido por el laboratorio.
 */
static const char *EVENTS[] = {
    "Inicio del partido",
    "Gol de Equipo A al minuto 15",
    "Tarjeta amarilla al numero 7 de Equipo B al minuto 22",
    "Cambio: jugador 10 entra por jugador 20 al minuto 30",
    "Gol de Equipo B al minuto 35",
    "Final del primer tiempo: 1-1",
    "Inicio del segundo tiempo",
    "Gol de Equipo A al minuto 60",
    "Tarjeta roja al numero 5 de Equipo B al minuto 75",
    "Gol de Equipo A al minuto 88",
    "Final del partido: Equipo A gana 3-1"
};

#define NUM_EVENTS  (int)(sizeof(EVENTS) / sizeof(EVENTS[0]))

/* ── main ───────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <tema>\n  Ej: %s ColombiaBrasil\n",
                argv[0], argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *topic = argv[1];

    /* 1. Crear socket UDP */
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    /* 2. Configurar direccion del broker */
    struct sockaddr_in broker_addr;
    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family      = AF_INET;
    broker_addr.sin_port        = htons(BROKER_PORT);

    /*
     * inet_aton convierte la cadena IP a formato binario de red.
     * Retorna 0 si la cadena es invalida.
     */
    if (inet_aton(BROKER_IP, &broker_addr.sin_addr) == 0) {
        fprintf(stderr, "IP del broker invalida: %s\n", BROKER_IP);
        close(sockfd); exit(EXIT_FAILURE);
    }

    printf("[PUBLISHER UDP] Tema: '%s' | Broker: %s:%d\n",
           topic, BROKER_IP, BROKER_PORT);
    printf("[PUBLISHER UDP] Enviando %d eventos...\n\n", NUM_EVENTS);

    /* 3. Enviar cada evento como un datagrama independiente */
    char buffer[MAX_BUFFER];

    for (int i = 0; i < NUM_EVENTS; i++) {
        /*
         * Formato del mensaje: "PUB <tema> <evento>"
         * El broker parseara este formato para enrutar el mensaje.
         */
        int len = snprintf(buffer, MAX_BUFFER, "PUB %s %s", topic, EVENTS[i]);

        /*
         * sendto: envia el datagrama al broker.
         * UDP no establece conexion; cada llamada es independiente.
         * No hay garantia de entrega (no confiable por diseno).
         */
        ssize_t sent = sendto(sockfd, buffer, len, 0,
                              (struct sockaddr *)&broker_addr,
                              sizeof(broker_addr));
        if (sent < 0)
            perror("[PUBLISHER] sendto");
        else
            printf("[PUBLISHER] (#%02d) Enviado -> %s\n", i + 1, EVENTS[i]);

        sleep(1);  /* Pausa de 1 segundo entre eventos */
    }

    printf("\n[PUBLISHER UDP] Todos los eventos enviados.\n");
    close(sockfd);
    return 0;
}
