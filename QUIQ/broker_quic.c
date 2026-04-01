#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include <msquic.h>

#define ALPN "pubsub"
#define DEFAULT_PORT 4567
#define MAX_SUBSCRIBERS 128
#define RX_BUF_SIZE 4096

const QUIC_API_TABLE* MsQuic = NULL;
HQUIC Registration = NULL;
HQUIC ServerConfiguration = NULL;
HQUIC Listener = NULL;

typedef struct SendContext {
    QUIC_BUFFER Buffer;
    char* Raw;
} SendContext;

typedef struct ServerStreamContext {
    HQUIC Stream;
    bool is_subscriber;
    char topic[128];
    char rxbuf[RX_BUF_SIZE];
    size_t rxlen;
} ServerStreamContext;

typedef struct SubscriberEntry {
    bool used;
    char topic[128];
    HQUIC stream;
    ServerStreamContext* stream_ctx;
} SubscriberEntry;

static SubscriberEntry g_subscribers[MAX_SUBSCRIBERS];
static pthread_mutex_t g_sub_lock = PTHREAD_MUTEX_INITIALIZER;

static void die(const char* msg, QUIC_STATUS status) {
    fprintf(stderr, "%s (0x%x)\n", msg, status);
    exit(1);
}

static void send_text(HQUIC stream, const char* text) {
    SendContext* ctx = (SendContext*)calloc(1, sizeof(SendContext));
    if (!ctx) return;

    ctx->Raw = strdup(text);
    if (!ctx->Raw) {
        free(ctx);
        return;
    }

    ctx->Buffer.Buffer = (uint8_t*)ctx->Raw;
    ctx->Buffer.Length = (uint32_t)strlen(ctx->Raw);

    QUIC_STATUS st = MsQuic->StreamSend(
        stream,
        &ctx->Buffer,
        1,
        QUIC_SEND_FLAG_NONE,
        ctx
    );

    if (QUIC_FAILED(st)) {
        fprintf(stderr, "StreamSend fallo: 0x%x\n", st);
        free(ctx->Raw);
        free(ctx);
    }
}

static void remove_subscriber_by_stream(HQUIC stream) {
    pthread_mutex_lock(&g_sub_lock);
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (g_subscribers[i].used && g_subscribers[i].stream == stream) {
            g_subscribers[i].used = false;
            g_subscribers[i].topic[0] = '\0';
            g_subscribers[i].stream = NULL;
            g_subscribers[i].stream_ctx = NULL;
        }
    }
    pthread_mutex_unlock(&g_sub_lock);
}

static void add_or_update_subscriber(const char* topic, HQUIC stream, ServerStreamContext* ctx) {
    pthread_mutex_lock(&g_sub_lock);

    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (g_subscribers[i].used && g_subscribers[i].stream == stream) {
            strncpy(g_subscribers[i].topic, topic, sizeof(g_subscribers[i].topic) - 1);
            g_subscribers[i].topic[sizeof(g_subscribers[i].topic) - 1] = '\0';
            g_subscribers[i].stream_ctx = ctx;
            pthread_mutex_unlock(&g_sub_lock);
            return;
        }
    }

    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!g_subscribers[i].used) {
            g_subscribers[i].used = true;
            strncpy(g_subscribers[i].topic, topic, sizeof(g_subscribers[i].topic) - 1);
            g_subscribers[i].topic[sizeof(g_subscribers[i].topic) - 1] = '\0';
            g_subscribers[i].stream = stream;
            g_subscribers[i].stream_ctx = ctx;
            break;
        }
    }

    pthread_mutex_unlock(&g_sub_lock);
}

static void publish_to_topic(const char* topic, const char* message) {
    char out[1024];
    snprintf(out, sizeof(out), "MSG|%s|%s\n", topic, message);

    pthread_mutex_lock(&g_sub_lock);
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (g_subscribers[i].used && strcmp(g_subscribers[i].topic, topic) == 0) {
            send_text(g_subscribers[i].stream, out);
        }
    }
    pthread_mutex_unlock(&g_sub_lock);
}

static void handle_line(ServerStreamContext* ctx, const char* line) {
    if (strncmp(line, "SUB|", 4) == 0) {
        const char* topic = line + 4;
        ctx->is_subscriber = true;
        strncpy(ctx->topic, topic, sizeof(ctx->topic) - 1);
        ctx->topic[sizeof(ctx->topic) - 1] = '\0';

        add_or_update_subscriber(topic, ctx->Stream, ctx);
        printf("[BROKER] Suscriptor registrado en tema '%s'\n", topic);
        fflush(stdout);
        return;
    }

    if (strncmp(line, "PUB|", 4) == 0) {
        char tmp[1024];
        strncpy(tmp, line, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';

        char* save = NULL;
        char* tok = strtok_r(tmp, "|", &save); // PUB
        tok = strtok_r(NULL, "|", &save);      // topic
        if (!tok) return;
        char topic[128];
        strncpy(topic, tok, sizeof(topic) - 1);
        topic[sizeof(topic) - 1] = '\0';

        tok = strtok_r(NULL, "", &save);       // message restante
        if (!tok) return;

        printf("[BROKER] Publicacion tema='%s' msg='%s'\n", topic, tok);
        fflush(stdout);

        publish_to_topic(topic, tok);
        return;
    }

    printf("[BROKER] Mensaje no reconocido: %s\n", line);
    fflush(stdout);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
static QUIC_STATUS QUIC_API ServerStreamCallback(
    HQUIC Stream,
    void* Context,
    QUIC_STREAM_EVENT* Event
) {
    ServerStreamContext* ctx = (ServerStreamContext*)Context;

    switch (Event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE: {
            for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++) {
                QUIC_BUFFER* b = &Event->RECEIVE.Buffers[i];
                if (ctx->rxlen + b->Length >= sizeof(ctx->rxbuf)) {
                    ctx->rxlen = 0;
                    continue;
                }

                memcpy(ctx->rxbuf + ctx->rxlen, b->Buffer, b->Length);
                ctx->rxlen += b->Length;
                ctx->rxbuf[ctx->rxlen] = '\0';

                char* start = ctx->rxbuf;
                char* nl = NULL;
                while ((nl = strchr(start, '\n')) != NULL) {
                    *nl = '\0';
                    if (*start) {
                        handle_line(ctx, start);
                    }
                    start = nl + 1;
                }

                size_t remain = strlen(start);
                memmove(ctx->rxbuf, start, remain);
                ctx->rxlen = remain;
                ctx->rxbuf[ctx->rxlen] = '\0';
            }
            break;
        }

        case QUIC_STREAM_EVENT_SEND_COMPLETE: {
            SendContext* send_ctx = (SendContext*)Event->SEND_COMPLETE.ClientContext;
            if (send_ctx) {
                free(send_ctx->Raw);
                free(send_ctx);
            }
            break;
        }

        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
            break;

        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            remove_subscriber_by_stream(Stream);
            MsQuic->StreamClose(Stream);
            free(ctx);
            break;

        default:
            break;
    }

    return QUIC_STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
static QUIC_STATUS QUIC_API ServerConnectionCallback(
    HQUIC Connection,
    void* Context,
    QUIC_CONNECTION_EVENT* Event
) {
    (void)Context;

    switch (Event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED:
            printf("[BROKER] Conexion QUIC establecida\n");
            fflush(stdout);
            break;

        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
            ServerStreamContext* stream_ctx = (ServerStreamContext*)calloc(1, sizeof(ServerStreamContext));
            if (!stream_ctx) return QUIC_STATUS_OUT_OF_MEMORY;

            stream_ctx->Stream = Event->PEER_STREAM_STARTED.Stream;
            MsQuic->SetCallbackHandler(
                Event->PEER_STREAM_STARTED.Stream,
                (void*)ServerStreamCallback,
                stream_ctx
            );
            break;
        }

        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            MsQuic->ConnectionClose(Connection);
            break;

        default:
            break;
    }

    return QUIC_STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_LISTENER_CALLBACK)
static QUIC_STATUS QUIC_API ListenerCallback(
    HQUIC ListenerHandle,
    void* Context,
    QUIC_LISTENER_EVENT* Event
) {
    (void)ListenerHandle;
    (void)Context;

    switch (Event->Type) {
        case QUIC_LISTENER_EVENT_NEW_CONNECTION:
            MsQuic->SetCallbackHandler(
                Event->NEW_CONNECTION.Connection,
                (void*)ServerConnectionCallback,
                NULL
            );
            if (QUIC_FAILED(MsQuic->ConnectionSetConfiguration(
                    Event->NEW_CONNECTION.Connection,
                    ServerConfiguration))) {
                return QUIC_STATUS_INTERNAL_ERROR;
            }
            return QUIC_STATUS_SUCCESS;

        default:
            return QUIC_STATUS_SUCCESS;
    }
}

int main(int argc, char** argv) {
    uint16_t port = DEFAULT_PORT;
    const char* cert_file = "server.cert";
    const char* key_file = "server.key";

    if (argc >= 2) port = (uint16_t)atoi(argv[1]);
    if (argc >= 4) {
        cert_file = argv[2];
        key_file = argv[3];
    }

    QUIC_STATUS st = MsQuicOpen2(&MsQuic);
    if (QUIC_FAILED(st)) die("MsQuicOpen2 fallo", st);

    QUIC_REGISTRATION_CONFIG reg_cfg = { "broker_quic", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
    st = MsQuic->RegistrationOpen(&reg_cfg, &Registration);
    if (QUIC_FAILED(st)) die("RegistrationOpen fallo", st);

    QUIC_BUFFER alpn;
    alpn.Buffer = (uint8_t*)ALPN;
    alpn.Length = (uint32_t)(sizeof(ALPN) - 1);

    st = MsQuic->ConfigurationOpen(
        Registration,
        &alpn,
        1,
        NULL,
        0,
        NULL,
        &ServerConfiguration
    );
    if (QUIC_FAILED(st)) die("ConfigurationOpen fallo", st);

    QUIC_CERTIFICATE_FILE cert = { cert_file, key_file };
    QUIC_CREDENTIAL_CONFIG cred = {0};
    cred.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
    cred.CertificateFile = &cert;
    cred.Flags = QUIC_CREDENTIAL_FLAG_NONE;

    st = MsQuic->ConfigurationLoadCredential(ServerConfiguration, &cred);
    if (QUIC_FAILED(st)) die("ConfigurationLoadCredential fallo", st);

    st = MsQuic->ListenerOpen(Registration, ListenerCallback, NULL, &Listener);
    if (QUIC_FAILED(st)) die("ListenerOpen fallo", st);

    QUIC_ADDR addr = {0};
    QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_UNSPEC);
    QuicAddrSetPort(&addr, port);

    st = MsQuic->ListenerStart(Listener, &alpn, 1, &addr);
    if (QUIC_FAILED(st)) die("ListenerStart fallo", st);

    printf("[BROKER] QUIC escuchando en puerto %u\n", port);
    printf("[BROKER] Cert: %s | Key: %s\n", cert_file, key_file);
    fflush(stdout);

    while (1) {
        getchar();
    }

    return 0;
}