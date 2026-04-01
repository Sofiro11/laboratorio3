# README TCP - Sistema Publicador–Suscriptor en C

## 1. Descripción general

Este proyecto implementa la versión TCP de un sistema de comunicación basado en el modelo publicación–suscripción usando sockets en C.

El sistema está compuesto por tres programas:

- `broker_tcp.c`
- `publisher_tcp.c`
- `subscriber_tcp.c`

### Roles de cada componente

- **Broker:** recibe conexiones de publishers y subscribers, registra las suscripciones y reenvía mensajes a los clientes suscritos al tema correspondiente.
- **Publisher:** se conecta al broker y publica mensajes asociados a un tema.
- **Subscriber:** se conecta al broker, se suscribe a un tema y recibe los mensajes publicados en dicho tema.

---

## 2. Aclaración sobre librerías usadas

### No se usan librerías externas

Esta implementación **no utiliza librerías externas ni de terceros**.

Solo se emplean:

- cabeceras estándar del lenguaje C
- cabeceras del sistema para programación de sockets POSIX

### Cabeceras utilizadas

#### En `broker_tcp.c`
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
```

#### En `publisher_tcp.c`
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
```

#### En `subscriber_tcp.c`
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
```

### Conclusión sobre dependencias

El proyecto usa únicamente funciones nativas como:

- `socket()`
- `bind()`
- `listen()`
- `accept()`
- `connect()`
- `read()`
- `send()`
- `close()`
- `select()`
- `inet_pton()`
- `htons()`

Por lo tanto, **no hay frameworks, wrappers, SDKs ni bibliotecas adicionales** para sockets.

---

## 3. Estructura de archivos

```text
broker_tcp.c
publisher_tcp.c
subscriber_tcp.c
README_TCP.md
```

---

## 4. Formato de mensajes

El sistema maneja dos tipos de mensajes:

### 4.1 Suscripción
Formato:
```text
SUB|tema
```

Ejemplo:
```text
SUB|REDES
```

### 4.2 Publicación
Formato:
```text
PUB|tema|mensaje
```

Ejemplo:
```text
PUB|REDES|Handshake TCP iniciado
```

### 4.3 Mensaje reenviado por el broker
Formato:
```text
[tema] mensaje
```

Ejemplo:
```text
[REDES] Handshake TCP iniciado
```

---

## 5. Documentación de `broker_tcp.c`

## 5.1 Propósito

`broker_tcp.c` implementa el broker del sistema. Su función principal es:

- escuchar conexiones TCP entrantes
- aceptar publishers y subscribers
- registrar qué cliente está suscrito a qué tema
- recibir publicaciones
- reenviar cada publicación a los subscribers del tema correcto

---

## 5.2 Constantes definidas

```c
#define PORT 5000
#define MAX_CLIENTS 20
#define BUFFER_SIZE 1024
```

### Explicación
- `PORT`: puerto en el que escucha el broker
- `MAX_CLIENTS`: cantidad máxima de clientes simultáneos
- `BUFFER_SIZE`: tamaño del búfer de recepción y envío

---

## 5.3 Estructura usada

```c
typedef struct {
    int fd;
    int is_subscriber;
    char topic[50];
} Client;
```

### Explicación
Cada cliente conectado se representa con:

- `fd`: descriptor del socket
- `is_subscriber`: indica si el cliente es subscriber
- `topic`: tema al que está suscrito

---

## 5.4 Funcionamiento paso a paso

### 1. Inicialización de la tabla de clientes
Se crea un arreglo de estructuras `Client` y se inicializa para indicar que no hay clientes conectados al inicio.

### 2. Creación del socket servidor
Se llama a:

```c
socket(AF_INET, SOCK_STREAM, 0)
```

Esto crea un socket TCP IPv4.

### 3. Configuración del socket
Se usa:

```c
setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

Esto permite reutilizar el puerto al reiniciar el broker.

### 4. Asociación a puerto y dirección
Se configura la estructura `sockaddr_in` con:

- familia `AF_INET`
- dirección `INADDR_ANY`
- puerto `htons(PORT)`

Luego se ejecuta:

```c
bind(server_fd, (struct sockaddr *)&address, sizeof(address))
```

para asociar el socket al puerto 5000.

### 5. Escucha de conexiones
Se llama a:

```c
listen(server_fd, 10)
```

para dejar al broker en estado de espera de conexiones entrantes.

### 6. Multiplexación con `select()`
El broker usa `select()` para monitorear simultáneamente:

- el socket servidor
- todos los sockets de los clientes conectados

Esto evita crear hilos y permite manejar varios clientes en un solo proceso.

### 7. Aceptación de nuevas conexiones
Cuando el socket servidor tiene actividad, se usa:

```c
accept()
```

para aceptar un nuevo cliente.

El descriptor del nuevo cliente se guarda en el arreglo `clients`.

### 8. Lectura de datos de clientes
Cuando un cliente envía información, se llama a:

```c
read(sd, buffer, BUFFER_SIZE - 1)
```

Si el resultado es `<= 0`, el cliente se considera desconectado.

### 9. Registro de subscribers
Si el mensaje recibido comienza por:

```text
SUB|
```

el broker marca al cliente como subscriber y guarda el tema.

Ejemplo:
```text
SUB|REDES
```

### 10. Procesamiento de publicaciones
Si el mensaje inicia con:

```text
PUB|
```

el broker separa:

- el tema
- el contenido del mensaje

Luego construye el mensaje de salida y lo reenvía a todos los subscribers cuyo tema coincida.

### 11. Reenvío
El broker usa:

```c
send(clients[j].fd, out, strlen(out), 0);
```

para entregar el mensaje a cada subscriber suscrito.

---

## 6. Documentación de `publisher_tcp.c`

## 6.1 Propósito

`publisher_tcp.c` implementa el publicador TCP. Su función es:

- conectarse al broker
- solicitar al usuario un tema
- leer mensajes desde teclado
- enviarlos al broker con formato de publicación

---

## 6.2 Constantes definidas

```c
#define PORT 5000
#define BUFFER_SIZE 1024
```

---

## 6.3 Funcionamiento paso a paso

### 1. Creación del socket cliente
Se crea un socket TCP con:

```c
socket(AF_INET, SOCK_STREAM, 0)
```

### 2. Configuración de la dirección del broker
Se define una estructura `sockaddr_in` con:

- familia `AF_INET`
- puerto 5000
- dirección `127.0.0.1`

La conversión de la IP a binario se hace con:

```c
inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)
```

### 3. Conexión al broker
Se usa:

```c
connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr))
```

para abrir la conexión TCP con el broker.

### 4. Lectura del tema
El programa solicita por consola el tema que usará el publicador.

### 5. Lectura de mensajes
Dentro de un ciclo infinito, el usuario escribe mensajes por teclado.

### 6. Construcción del mensaje
Cada mensaje se arma con el formato:

```text
PUB|tema|mensaje
```

usando `snprintf()`.

### 7. Envío al broker
El mensaje se envía usando:

```c
send(sock, buffer, strlen(buffer), 0)
```

---

## 7. Documentación de `subscriber_tcp.c`

## 7.1 Propósito

`subscriber_tcp.c` implementa el suscriptor TCP. Su función es:

- conectarse al broker
- indicar el tema al que desea suscribirse
- esperar mensajes del broker
- mostrarlos en pantalla

---

## 7.2 Constantes definidas

```c
#define PORT 5000
#define BUFFER_SIZE 1024
```

---

## 7.3 Funcionamiento paso a paso

### 1. Creación del socket cliente
Se crea el socket TCP con:

```c
socket(AF_INET, SOCK_STREAM, 0)
```

### 2. Configuración del broker
Se especifica:

- familia `AF_INET`
- puerto 5000
- dirección `127.0.0.1`

### 3. Conexión al broker
Se llama a:

```c
connect()
```

para conectarse al broker.

### 4. Lectura del tema
El usuario escribe el tema al que quiere suscribirse.

### 5. Construcción de la suscripción
El programa arma el mensaje:

```text
SUB|tema
```

### 6. Envío de la suscripción
La suscripción se envía con:

```c
send(sock, buffer, strlen(buffer), 0)
```

### 7. Recepción continua de mensajes
Luego el subscriber entra en un ciclo donde:

- limpia el búfer
- lee con `read()`
- imprime lo recibido

Si la conexión se cierra, el ciclo termina.

---

## 8. Tabla de funciones usadas

| Función | Archivo(s) | Descripción |
|---|---|---|
| `socket()` | broker, publisher, subscriber | Crea sockets TCP |
| `setsockopt()` | broker | Permite reutilizar el puerto |
| `bind()` | broker | Asocia el socket al puerto 5000 |
| `listen()` | broker | Deja el broker escuchando conexiones |
| `accept()` | broker | Acepta nuevas conexiones |
| `select()` | broker | Monitorea varios sockets |
| `connect()` | publisher, subscriber | Conecta con el broker |
| `read()` | broker, subscriber | Lee datos recibidos |
| `send()` | broker, publisher, subscriber | Envía datos |
| `close()` | los tres | Cierra sockets |
| `inet_pton()` | publisher, subscriber | Convierte IP a binario |
| `htons()` | los tres | Convierte puerto a formato de red |

---

## 9. Compilación

Compilar con:

```bash
gcc broker_tcp.c -o broker_tcp
gcc publisher_tcp.c -o publisher_tcp
gcc subscriber_tcp.c -o subscriber_tcp
```

---



## 10. Conclusiones

- La implementación fue desarrollada completamente en C.
- No se utilizaron librerías externas ni bibliotecas de terceros.
- El sistema usa directamente sockets TCP del sistema operativo.
- El broker centraliza la comunicación y distribuye los mensajes según tema.
- `publisher_tcp.c` publica mensajes.
- `subscriber_tcp.c` recibe los mensajes del tema suscrito.

---
