/*
 * broker_udp.c
 * Broker central del sistema publicacion-suscripcion usando UDP.
 *
 * Responsabilidades:
 *  - Recibir mensajes de publicadores (tipo "PUB <tema> <mensaje>")
 *  - Registrar suscriptores (tipo "SUB <tema>")
 *  - Reenviar cada publicacion a los suscriptores del tema correspondiente
 *
 * Protocolo de mensajes (texto plano):
 *   SUB <tema>               -> registro de suscriptor
 *   PUB <tema> <mensaje>     -> publicacion de un evento
 *
 * Compilar: gcc -o broker_udp broker_udp.c
 * Ejecutar: ./broker_udp
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>   /* inet_ntoa, htons, etc.          */
#include <sys/socket.h>  /* socket(), bind(), recvfrom(), sendto() */
#include <netinet/in.h>  /* sockaddr_in                     */

/* ── Constantes ────────────────────────────────────────── */
#define BROKER_PORT    9090   /* Puerto en el que escucha el broker */
#define MAX_BUFFER     1024   /* Tamano maximo de un datagrama      */
#define MAX_SUBS       100    /* Maximo de suscriptores registrados */
#define MAX_TOPIC_LEN  64     /* Longitud maxima del nombre de tema */

/* ── Estructura de suscriptor ───────────────────────────── */
typedef struct {
    struct sockaddr_in addr;          /* IP y puerto del suscriptor */
    char               topic[MAX_TOPIC_LEN]; /* Tema al que esta suscrito */
    int                active;        /* 1 = activo, 0 = libre       */
} Subscriber;

/* ── Estado global ──────────────────────────────────────── */
static Subscriber subscribers[MAX_SUBS];
static int        sub_count = 0;

/* ── add_subscriber ─────────────────────────────────────── */
/*
 * Registra un nuevo suscriptor si aun no existe.
 * Evita duplicados comparando IP, puerto y tema.
 */
static void add_subscriber(const struct sockaddr_in *addr, const char *topic)
{
    /* Buscar si ya existe */
    for (int i = 0; i < sub_count; i++) {
        if (subscribers[i].active
            && subscribers[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr
            && subscribers[i].addr.sin_port        == addr->sin_port
            && strcmp(subscribers[i].topic, topic) == 0)
        {
            printf("[BROKER] Suscriptor %s:%d ya registrado en '%s'\n",
                   inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), topic);
            return;
        }
    }

    if (sub_count >= MAX_SUBS) {
        fprintf(stderr, "[BROKER] Lista de suscriptores llena.\n");
        return;
    }

    subscribers[sub_count].addr   = *addr;
    strncpy(subscribers[sub_count].topic, topic, MAX_TOPIC_LEN - 1);
    subscribers[sub_count].topic[MAX_TOPIC_LEN - 1] = '\0';
    subscribers[sub_count].active = 1;
    sub_count++;

    printf("[BROKER] Nuevo suscriptor: %s:%d -> tema '%s'\n",
           inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), topic);
}

/* ── forward_message ────────────────────────────────────── */
/*
 * Reenvía el mensaje a todos los suscriptores del tema indicado.
 * Usa sendto() para enviar un datagrama UDP independiente a cada uno.
 */
static void forward_message(int sockfd, const char *topic, const char *message)
{
    int delivered = 0;
    char out[MAX_BUFFER];
    /* Formato del mensaje entregado al suscriptor */
    snprintf(out, MAX_BUFFER, "[%s] %s", topic, message);

    for (int i = 0; i < sub_count; i++) {
        if (subscribers[i].active
            && strcmp(subscribers[i].topic, topic) == 0)
        {
            /* sendto: envia el datagrama directamente a la direccion del suscriptor */
            ssize_t n = sendto(sockfd, out, strlen(out), 0,
                               (struct sockaddr *)&subscribers[i].addr,
                               sizeof(subscribers[i].addr));
            if (n < 0)
                perror("[BROKER] sendto");
            else
                delivered++;
        }
    }

    printf("[BROKER] Tema '%s' -> %d suscriptor(es) notificados: %s\n",
           topic, delivered, message);
}

/* ── main ───────────────────────────────────────────────── */
int main(void)
{
    int                sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t          addr_len = sizeof(client_addr);
    char               buffer[MAX_BUFFER];

    /* 1. Crear socket UDP (SOCK_DGRAM = datagrama, sin conexion) */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    /* 2. Configurar direccion del servidor */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;        /* Acepta en cualquier interfaz */
    server_addr.sin_port        = htons(BROKER_PORT);/* Convierte a orden de red      */

    /* 3. Asociar el socket al puerto (bind) */
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); close(sockfd); exit(EXIT_FAILURE);
    }

    printf("[BROKER UDP] Escuchando en puerto %d...\n", BROKER_PORT);

    /* 4. Bucle principal de recepcion */
    while (1) {
        memset(buffer, 0, MAX_BUFFER);

        /*
         * recvfrom: bloquea hasta recibir un datagrama.
         * Retorna el numero de bytes leidos.
         * client_addr se rellena con el origen del datagrama.
         */
        ssize_t n = recvfrom(sockfd, buffer, MAX_BUFFER - 1, 0,
                             (struct sockaddr *)&client_addr, &addr_len);
        if (n < 0) { perror("recvfrom"); continue; }
        buffer[n] = '\0';

        /* 5. Parsear tipo de mensaje */
        char type[8], topic[MAX_TOPIC_LEN], message[MAX_BUFFER];
        message[0] = '\0';

        /*
         * sscanf parsea: TIPO TEMA [MENSAJE...]
         * El especificador %[^\n] captura hasta fin de linea.
         */
        int fields = sscanf(buffer, "%7s %63s %1023[^\n]", type, topic, message);

        if (fields >= 2) {
            if (strcmp(type, "SUB") == 0) {
                /* Registro de suscriptor: la direccion viene de recvfrom */
                add_subscriber(&client_addr, topic);

            } else if (strcmp(type, "PUB") == 0 && fields == 3) {
                /* Publicacion: reenviar a suscriptores del tema */
                forward_message(sockfd, topic, message);

            } else {
                printf("[BROKER] Mensaje desconocido: %s\n", buffer);
            }
        }
    }

    close(sockfd);
    return 0;
}
