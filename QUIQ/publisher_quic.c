#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include <msquic.h>

#define ALPN "pubsub"
#define DEFAULT_PORT 4567

const QUIC_API_TABLE* MsQuic = NULL;
HQUIC Registration = NULL;
HQUIC Configuration = NULL;
HQUIC Connection = NULL;
HQUIC Stream = NULL;

static volatile int g_connected = 0;
static volatile int g_stream_ready = 0;

typedef struct SendContext {
    QUIC_BUFFER Buffer;
    char* Raw;
} SendContext;

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

    QUIC_STATUS st = MsQuic->StreamSend(stream, &ctx->Buffer, 1, QUIC_SEND_FLAG_NONE, ctx);
    if (QUIC_FAILED(st)) {
        fprintf(stderr, "StreamSend fallo: 0x%x\n", st);
        free(ctx->Raw);
        free(ctx);
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
static QUIC_STATUS QUIC_API PublisherStreamCallback(
    HQUIC StreamHandle,
    void* Context,
    QUIC_STREAM_EVENT* Event
) {
    (void)StreamHandle;
    (void)Context;

    switch (Event->Type) {
        case QUIC_STREAM_EVENT_SEND_COMPLETE: {
            SendContext* send_ctx = (SendContext*)Event->SEND_COMPLETE.ClientContext;
            if (send_ctx) {
                free(send_ctx->Raw);
                free(send_ctx);
            }
            break;
        }

        case QUIC_STREAM_EVENT_RECEIVE:
            // Si el broker responde algo, se imprime.
            for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; i++) {
                fwrite(Event->RECEIVE.Buffers[i].Buffer, 1, Event->RECEIVE.Buffers[i].Length, stdout);
            }
            fflush(stdout);
            break;

        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            MsQuic->StreamClose(StreamHandle);
            break;

        default:
            break;
    }

    return QUIC_STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
static QUIC_STATUS QUIC_API PublisherConnectionCallback(
    HQUIC ConnectionHandle,
    void* Context,
    QUIC_CONNECTION_EVENT* Event
) {
    (void)Context;

    switch (Event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED: {
            printf("[PUBLISHER] Conectado al broker QUIC\n");
            fflush(stdout);
            g_connected = 1;

            QUIC_STATUS st = MsQuic->StreamOpen(
                ConnectionHandle,
                QUIC_STREAM_OPEN_FLAG_NONE,
                PublisherStreamCallback,
                NULL,
                &Stream
            );
            if (QUIC_FAILED(st)) die("StreamOpen fallo", st);

            st = MsQuic->StreamStart(Stream, QUIC_STREAM_START_FLAG_IMMEDIATE);
            if (QUIC_FAILED(st)) die("StreamStart fallo", st);

            g_stream_ready = 1;
            break;
        }

        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            MsQuic->ConnectionClose(ConnectionHandle);
            break;

        default:
            break;
    }

    return QUIC_STATUS_SUCCESS;
}

int main(int argc, char** argv) {
    const char* host = "127.0.0.1";
    uint16_t port = DEFAULT_PORT;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = (uint16_t)atoi(argv[2]);

    QUIC_STATUS st = MsQuicOpen2(&MsQuic);
    if (QUIC_FAILED(st)) die("MsQuicOpen2 fallo", st);

    QUIC_REGISTRATION_CONFIG reg_cfg = { "publisher_quic", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
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
        &Configuration
    );
    if (QUIC_FAILED(st)) die("ConfigurationOpen fallo", st);

    QUIC_CREDENTIAL_CONFIG cred = {0};
    cred.Type = QUIC_CREDENTIAL_TYPE_NONE;
    cred.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;

    st = MsQuic->ConfigurationLoadCredential(Configuration, &cred);
    if (QUIC_FAILED(st)) die("ConfigurationLoadCredential fallo", st);

    st = MsQuic->ConnectionOpen(
        Registration,
        PublisherConnectionCallback,
        NULL,
        &Connection
    );
    if (QUIC_FAILED(st)) die("ConnectionOpen fallo", st);

    st = MsQuic->ConnectionStart(
        Connection,
        Configuration,
        QUIC_ADDRESS_FAMILY_UNSPEC,
        host,
        port
    );
    if (QUIC_FAILED(st)) die("ConnectionStart fallo", st);

    while (!g_stream_ready) {
        usleep(10000);
    }

    char topic[128];
    char msg[512];
    char line[1024];

    printf("Tema: ");
    fflush(stdout);
    if (!fgets(topic, sizeof(topic), stdin)) return 0;
    topic[strcspn(topic, "\r\n")] = '\0';

    while (1) {
        printf("Mensaje: ");
        fflush(stdout);
        if (!fgets(msg, sizeof(msg), stdin)) break;
        msg[strcspn(msg, "\r\n")] = '\0';

        snprintf(line, sizeof(line), "PUB|%s|%s\n", topic, msg);
        send_text(Stream, line);
    }

    return 0;
}