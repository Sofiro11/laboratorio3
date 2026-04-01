# Laboratorio 3 — UDP Pub-Sub: Documentación de funciones de socket

## Sobre las librerías utilizadas

Los archivos fuente **no usan librerías externas de terceros**.  
Solo se incluyen headers estándar del sistema operativo (POSIX/C estándar),
que forman parte de la API del SO y están disponibles en cualquier sistema
Unix/Linux/macOS sin instalación adicional:

| Header           | Propósito                                      |
|------------------|------------------------------------------------|
| `<sys/socket.h>` | Primitivas de socket: `socket()`, `bind()`, `sendto()`, `recvfrom()` |
| `<arpa/inet.h>`  | Conversión de direcciones IP: `inet_aton()`, `inet_ntoa()`, `htons()`, `ntohs()` |
| `<netinet/in.h>` | Estructura `sockaddr_in`, constante `INADDR_ANY` |
| `<stdio.h>`      | Entrada/salida estándar: `printf()`, `fprintf()`, `perror()` |
| `<stdlib.h>`     | Utilidades generales: `exit()`, `atoi()` |
| `<string.h>`     | Manejo de cadenas: `memset()`, `strncpy()`, `strcmp()`, `strlen()` |
| `<unistd.h>`     | Llamadas POSIX: `close()`, `sleep()` |

---

## Documentación punto a punto de cada función de socket

### 1. `socket()`
```c
int socket(int domain, int type, int protocol);
```
- **Header:** `<sys/socket.h>`
- **Descripción:** Crea un nuevo socket y retorna un descriptor de archivo.
- **Parámetros usados:**
  - `domain = AF_INET` → familia de direcciones IPv4
  - `type = SOCK_DGRAM` → socket de datagramas (UDP, sin conexión)
  - `protocol = 0` → el SO elige el protocolo adecuado (UDP para SOCK_DGRAM)
- **Retorno:** descriptor entero ≥ 0 si éxito; -1 si error (se verifica con `perror`)
- **Dónde se usa:** al inicio de `broker_udp.c`, `publisher_udp.c`, `subscriber_udp.c`

---

### 2. `bind()`
```c
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
```
- **Header:** `<sys/socket.h>`
- **Descripción:** Asocia el socket a una dirección IP y puerto locales.
  Sin `bind()`, el SO asigna un puerto efímero aleatorio en cada envío,
  lo que impediría al broker localizar al suscriptor para reenviarle mensajes.
- **Parámetros usados:**
  - `sockfd` → descriptor del socket creado
  - `addr` → puntero a `sockaddr_in` con IP (`INADDR_ANY`) y puerto deseado
  - `addrlen` → tamaño de la estructura
- **Retorno:** 0 si éxito; -1 si error
- **Dónde se usa:** `broker_udp.c` (puerto 9090) y `subscriber_udp.c` (puerto local del suscriptor)

---

### 3. `sendto()`
```c
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
```
- **Header:** `<sys/socket.h>`
- **Descripción:** Envía un datagrama UDP a una dirección de destino específica.
  Cada llamada a `sendto()` es un datagrama independiente; UDP no garantiza
  entrega ni orden.
- **Parámetros usados:**
  - `sockfd` → descriptor del socket
  - `buf` → buffer con el mensaje a enviar (cadena de texto)
  - `len` → longitud del mensaje (`strlen(buffer)`)
  - `flags = 0` → sin flags especiales
  - `dest_addr` → dirección destino (`sockaddr_in` del broker o del suscriptor)
  - `addrlen` → tamaño de la estructura de destino
- **Retorno:** número de bytes enviados; -1 si error
- **Dónde se usa:**
  - `publisher_udp.c` → envía `"PUB <tema> <mensaje>"` al broker
  - `subscriber_udp.c` → envía `"SUB <tema>"` al broker
  - `broker_udp.c` → reenvía mensajes a cada suscriptor registrado

---

### 4. `recvfrom()`
```c
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
```
- **Header:** `<sys/socket.h>`
- **Descripción:** Recibe un datagrama UDP. Bloquea hasta que llega un paquete.
  Rellena `src_addr` con la dirección del remitente, lo que permite al broker
  conocer el IP y puerto del suscriptor sin necesidad de que este se los informe
  explícitamente en el cuerpo del mensaje.
- **Parámetros usados:**
  - `sockfd` → descriptor del socket
  - `buf` → buffer donde se almacena el mensaje recibido
  - `len` → tamaño máximo a leer (`MAX_BUFFER - 1`)
  - `flags = 0` → sin flags especiales
  - `src_addr` → se rellena con IP y puerto del remitente
  - `addrlen` → entrada: tamaño del buffer de dirección; salida: tamaño real
- **Retorno:** número de bytes recibidos; -1 si error
- **Dónde se usa:** `broker_udp.c` y `subscriber_udp.c` en el bucle principal

---

### 5. `htons()` / `ntohs()`
```c
uint16_t htons(uint16_t hostshort);
uint16_t ntohs(uint16_t netshort);
```
- **Header:** `<arpa/inet.h>`
- **Descripción:**
  - `htons` (*host to network short*): convierte un número de 16 bits del orden
    del host (little-endian en x86) al orden de red (big-endian). Obligatorio
    al asignar puertos en `sockaddr_in.sin_port`.
  - `ntohs` (*network to host short*): conversión inversa, usada al imprimir
    el puerto de un cliente recibido por `recvfrom`.
- **Dónde se usa:** en todos los archivos al configurar `sockaddr_in`

---

### 6. `inet_aton()` / `inet_ntoa()`
```c
int   inet_aton(const char *cp, struct in_addr *inp);
char *inet_ntoa(struct in_addr in);
```
- **Header:** `<arpa/inet.h>`
- **Descripción:**
  - `inet_aton`: convierte una cadena IPv4 (`"127.0.0.1"`) a formato binario de
    red y lo almacena en `in_addr`. Retorna 0 si la cadena es inválida.
  - `inet_ntoa`: conversión inversa, de binario a cadena legible. Usada en los
    mensajes de log del broker.
- **Dónde se usa:** `publisher_udp.c` y `subscriber_udp.c` al configurar la
  dirección del broker; `broker_udp.c` al imprimir la IP del suscriptor.

---

### 7. `close()`
```c
int close(int fd);
```
- **Header:** `<unistd.h>`
- **Descripción:** Cierra el descriptor de archivo del socket, liberando los
  recursos del SO asociados.
- **Dónde se usa:** al final de cada programa.

---

## Compilación

```bash
gcc -o broker_udp     broker_udp.c
gcc -o publisher_udp  publisher_udp.c
gcc -o subscriber_udp subscriber_udp.c
```

No se requieren flags adicionales ni librerías externas (`-l`).
