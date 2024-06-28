#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>

// Definizione della dimensione massima del messaggio
const size_t k_max_msg = 4096;

// Definizione degli stati della connessione
enum {
    STATE_REQ = 0, // Stato di richiesta
    STATE_RES = 1, // Stato di risposta
    STATE_END = 2, // Stato di chiusura della connessione
};

// Struttura per gestire le connessioni
struct Conn {
    int fd = -1;            // File descriptor della connessione
    uint32_t state = 0;     // Stato della connessione
    size_t rbuf_size = 0;   // Dimensione del buffer di lettura
    uint8_t rbuf[4 + k_max_msg]; // Buffer di lettura
    size_t wbuf_size = 0;   // Dimensione del buffer di scrittura
    size_t wbuf_sent = 0;   // Numero di byte già inviati
    uint8_t wbuf[4 + k_max_msg]; // Buffer di scrittura
};

// Funzione per stampare messaggi di errore
static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

// Funzione per terminare il programma in caso di errore critico
static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

// Funzione per impostare un file descriptor in modalità non bloccante
static void fd_set_nb(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0); // Ottiene i flag del file descriptor
    if (errno) {
        die("fcntl error");
        return;
    }

    flags |= O_NONBLOCK; // Aggiunge il flag di non bloccaggio

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags); // Imposta i nuovi flag
    if (errno) {
        die("fcntl error");
    }
}

// Funzione per inserire una connessione nella lista di connessioni
static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
    if (fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd + 1); // Ridimensiona il vettore se necessario
    }
    fd2conn[conn->fd] = conn; // Inserisce la connessione nel vettore
}

// Funzione per accettare nuove connessioni
static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
    struct sockaddr_in client_addr = {}; // Struttura per l'indirizzo del client
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen); // Accetta la connessione
    if (connfd < 0) {
        msg("accept() error");
        return -1;
    }

    fd_set_nb(connfd); // Imposta la connessione in modalità non bloccante
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn)); // Alloca memoria per la nuova connessione
    if (!conn) {
        close(connfd);
        return -1;
    }
    conn->fd = connfd; // Imposta il file descriptor della connessione
    conn->state = STATE_REQ; // Imposta lo stato della connessione a richiesta
    conn->rbuf_size = 0; // Inizializza la dimensione del buffer di lettura
    conn->wbuf_size = 0; // Inizializza la dimensione del buffer di scrittura
    conn->wbuf_sent = 0; // Inizializza il contatore dei byte inviati
    conn_put(fd2conn, conn); // Inserisce la connessione nel vettore
    return 0;
}

// Prototipi delle funzioni per gestire gli stati
static void state_req(Conn *conn);
static void state_res(Conn *conn);

// Funzione per gestire una singola richiesta
static bool try_one_request(Conn *conn) {
    if (conn->rbuf_size < 4) {
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4); // Copia la lunghezza del messaggio
    if (len > k_max_msg) {
        msg("too long");
        conn->state = STATE_END; // Imposta lo stato a END se il messaggio è troppo lungo
        return false;
    }
    if (4 + len > conn->rbuf_size) {
        return false;
    }

    printf("client says: %.*s\n", len, &conn->rbuf[4]); // Stampa il messaggio del client

    memcpy(&conn->wbuf[0], &len, 4); // Prepara la risposta
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len); // Copia i dati del messaggio nel buffer di scrittura
    conn->wbuf_size = 4 + len; // Imposta la dimensione del buffer di scrittura

    size_t remain = conn->rbuf_size - 4 - len; // Calcola i byte rimanenti nel buffer di lettura
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain); // Sposta i byte rimanenti all'inizio del buffer
    }
    conn->rbuf_size = remain; // Aggiorna la dimensione del buffer di lettura

    conn->state = STATE_RES; // Imposta lo stato a RES
    state_res(conn); // Gestisce lo stato di risposta

    return (conn->state == STATE_REQ); // Ritorna true se lo stato è tornato a REQ
}

// Funzione per riempire il buffer di lettura
static bool try_fill_buffer(Conn *conn) {
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = 0;
    do {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size; // Calcola la capacità rimanente del buffer
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap); // Legge dati dal file descriptor
    } while (rv < 0 && errno == EINTR); // Riprova se l'errore è EINTR
    if (rv < 0 && errno == EAGAIN) {
        return false;
    }
    if (rv < 0) {
        msg("read() error");
        conn->state = STATE_END; // Imposta lo stato a END se c'è un errore di lettura
        return false;
    }
    if (rv == 0) {
        if (conn->rbuf_size > 0) {
            msg("unexpected EOF");
        } else {
            msg("EOF");
        }
        conn->state = STATE_END; // Imposta lo stato a END se il client chiude la connessione
        return false;
    }

    conn->rbuf_size += (size_t)rv; // Aggiorna la dimensione del buffer di lettura
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    while (try_one_request(conn)) {} // Gestisce le richieste finché ce ne sono nel buffer
    return (conn->state == STATE_REQ); // Ritorna true se lo stato è REQ
}

// Funzione per gestire lo stato di richiesta
static void state_req(Conn *conn) {
    while (try_fill_buffer(conn)) {} // Riempie il buffer finché ce ne sono dati disponibili
}

// Funzione per svuotare il buffer di scrittura
static bool try_flush_buffer(Conn *conn) {
    ssize_t rv = 0;
    do {
        size_t remain = conn->wbuf_size - conn->wbuf_sent; // Calcola i byte rimanenti nel buffer di scrittura
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain); // Scrive i dati nel file descriptor
    } while (rv < 0 && errno == EINTR); // Riprova se l'errore è EINTR
    if (rv < 0 && errno == EAGAIN) {
        return false;
    }
    if (rv < 0) {
        msg("write() error");
        conn->state = STATE_END; // Imposta lo stato a END se c'è un errore di scrittura
        return false;
    }
    conn->wbuf_sent += (size_t)rv; // Aggiorna il contatore dei byte inviati
    assert(conn->wbuf_sent <= conn->wbuf_size);
    if (conn->wbuf_sent == conn->wbuf_size) {
        conn->state = STATE_REQ; // Imposta lo stato a REQ se tutti i dati sono stati inviati
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    return true;
}

// Funzione per gestire lo stato di risposta
static void state_res(Conn *conn) {
    while (try_flush_buffer(conn)) {} // Svuota il buffer finché ce ne sono dati da inviare
}

// Funzione per gestire l'I/O delle connessioni
static void connection_io(Conn *conn) {
    if (conn->state == STATE_REQ) {
        state_req(conn); // Gestisce lo stato di richiesta
    } else if (conn->state == STATE_RES) {
        state_res(conn); // Gestisce lo stato di risposta
    } else {
        assert(0); // Se lo stato è sconosciuto, genera un'asserzione
    }
}

// Funzione principale
int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0); // Crea un socket TCP
    if (fd < 0) {
        die("socket()");
    }

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)); // Imposta l'opzione SO_REUSEADDR

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234); // Imposta la porta
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // Imposta l'indirizzo IP
    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr)); // Associa il socket all'indirizzo e porta
    if (rv) {
        die("bind()");
    }

    rv = listen(fd, SOMAXCONN); // Mette il socket in ascolto per connessioni in ingresso
    if (rv) {
        die("listen()");
    }

    std::vector<Conn *> fd2conn; // Vettore per mappare file descriptor alle connessioni
    fd_set_nb(fd); // Imposta il socket in modalità non bloccante

    std::vector<struct pollfd> poll_args; // Vettore per gli argomenti di poll
    while (true) {
        poll_args.clear();
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd); // Aggiunge il socket del server a poll_args

        for (Conn *conn : fd2conn) {
            if (!conn) {
                continue;
            }
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT; // Imposta l'evento per poll
            pfd.events = pfd.events | POLLERR; // Aggiunge l'evento di errore
            poll_args.push_back(pfd); // Aggiunge il file descriptor del client a poll_args
        }

        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000); // Attende gli eventi
        if (rv < 0) {
            die("poll()");
        }

        for (size_t i = 1; i < poll_args.size(); ++i) {
            if (poll_args[i].revents) {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn); // Gestisce l'I/O della connessione
                if (conn->state == STATE_END) {
                    fd2conn[conn->fd] = NULL;
                    (void)close(conn->fd); // Chiude la connessione
                    free(conn); // Libera la memoria della connessione
                }
            }
        }

        if (poll_args[0].revents) {
            (void)accept_new_conn(fd2conn, fd); // Accetta una nuova connessione
        }
    }

    return 0;
}