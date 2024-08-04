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
#include <string>
#include <vector>
#include <map>

// Definizione della dimensione massima del messaggio
const size_t k_max_msg = 4096;

// Definizione degli stati della connessione
enum
{
    STATE_REQ = 0, // Stato di richiesta
    STATE_RES = 1, // Stato di risposta
    STATE_END = 2, // Stato di chiusura della connessione
};

// Struttura per gestire le connessioni
struct Conn
{
    int fd = -1;                 // File descriptor della connessione
    uint32_t state = 0;          // Stato della connessione
    size_t rbuf_size = 0;        // Dimensione del buffer di lettura
    uint8_t rbuf[4 + k_max_msg]; // Buffer di lettura
    size_t wbuf_size = 0;        // Dimensione del buffer di scrittura
    size_t wbuf_sent = 0;        // Numero di byte già inviati
    uint8_t wbuf[4 + k_max_msg]; // Buffer di scrittura
};

// Funzione per stampare messaggi di errore
static void msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

// Funzione per terminare il programma in caso di errore critico
static void die(const char *msg)
{
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

// Funzione per impostare un file descriptor in modalità non bloccante
static void fd_set_nb(int fd)
{
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0); // Ottiene i flag del file descriptor
    if (errno)
    {
        die("fcntl error");
        return;
    }

    flags |= O_NONBLOCK; // Aggiunge il flag di non bloccaggio

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags); // Imposta i nuovi flag
    if (errno)
    {
        die("fcntl error");
    }
}

// Funzione per inserire una connessione nella lista di connessioni
static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn)
{
    if (fd2conn.size() <= (size_t)conn->fd)
    {
        fd2conn.resize(conn->fd + 1); // Ridimensiona il vettore se necessario
    }
    fd2conn[conn->fd] = conn; // Inserisce la connessione nel vettore
}

// Funzione per accettare nuove connessioni
static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd)
{
    struct sockaddr_in client_addr = {}; // Struttura per l'indirizzo del client
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen); // Accetta la connessione
    if (connfd < 0)
    {
        msg("accept() error");
        return -1;
    }

    fd_set_nb(connfd);                                              // Imposta la connessione in modalità non bloccante
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn)); // Alloca memoria per la nuova connessione
    if (!conn)
    {
        close(connfd);
        return -1;
    }
    conn->fd = connfd;       // Imposta il file descriptor della connessione
    conn->state = STATE_REQ; // Imposta lo stato della connessione a richiesta
    conn->rbuf_size = 0;     // Inizializza la dimensione del buffer di lettura
    conn->wbuf_size = 0;     // Inizializza la dimensione del buffer di scrittura
    conn->wbuf_sent = 0;     // Inizializza il contatore dei byte inviati
    conn_put(fd2conn, conn); // Inserisce la connessione nel vettore
    return 0;
}

// Prototipi delle funzioni per gestire gli stati
static void state_req(Conn *conn);
static void state_res(Conn *conn);

const size_t k_max_args = 1024;

// funzione per fare parsing della richiesta
static int32_t parse_req(
    const uint8_t *data, size_t len, std::vector<std::string> &out)
{
    if (len < 4)
    {
        return -1; // Controlla che ci siano almeno 4 byte per il numero di stringhe
    }
    uint32_t n = 0;
    memcpy(&n, &data[0], 4); // Copia i primi 4 byte in `n`, il numero di stringhe
    if (n > k_max_args)
    {
        return -1; // Se il numero di stringhe è maggiore del massimo consentito, ritorna errore
    }

    size_t pos = 4; // Posizione iniziale dopo il numero di stringhe
    while (n--)
    {
        if (pos + 4 > len)
        {
            return -1; // Controlla che ci siano abbastanza dati per la lunghezza della stringa
        }
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4); // Copia la lunghezza della stringa in `sz`
        if (pos + 4 + sz > len)
        {
            return -1; // Controlla che ci siano abbastanza dati per la stringa stessa
        }
        out.push_back(std::string((char *)&data[pos + 4], sz)); // Aggiungi la stringa al vettore `out`
        pos += 4 + sz;                                          // Avanza la posizione
    }

    if (pos != len)
    {
        return -1; // Se ci sono dati extra nel buffer, ritorna errore
    }
    return 0; // Ritorna successo
}

// Enum per i codici delle richieste

enum
{
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2, // Chiave non esistente
};

// struttura per lo spazio chiavi

static std::map<std::string, std::string> g_map; // Mappa globale per le chiavi e valori

// funzione do_get

static uint32_t do_get(
    const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    if (!g_map.count(cmd[1]))
    {
        return RES_NX; // Se la chiave non esiste, ritorna RES_NX
    }
    std::string &val = g_map[cmd[1]];
    assert(val.size() <= k_max_msg);
    memcpy(res, val.data(), val.size()); // Copia il valore nel buffer di risposta
    *reslen = (uint32_t)val.size();      // Imposta la lunghezza della risposta
    return RES_OK;                       // Ritorna successo
}

// funzione do_set

static uint32_t do_set(
    const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    (void)res;
    (void)reslen;
    g_map[cmd[1]] = cmd[2]; // Imposta la chiave con il valore
    return RES_OK;          // Ritorna successo
}

// funzione do_del

static uint32_t do_del(
    const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    (void)res;
    (void)reslen;
    g_map.erase(cmd[1]); // Cancella la chiave dalla mappa
    return RES_OK;       // Ritorna successo
}

// funzione cmd_is

static bool cmd_is(const std::string &word, const char *cmd)
{
    return 0 == strcasecmp(word.c_str(), cmd); // Confronta due stringhe ignorando le maiuscole/minuscole
}

// funzione do_request

static int32_t do_request(
    const uint8_t *req, uint32_t reqlen,
    uint32_t *rescode, uint8_t *res, uint32_t *reslen)
{
    std::vector<std::string> cmd;
    if (0 != parse_req(req, reqlen, cmd))
    {
        msg("bad req");
        return -1; // Se il parsing fallisce, ritorna errore
    }
    if (cmd.size() == 2 && cmd_is(cmd[0], "get"))
    {
        *rescode = do_get(cmd, res, reslen); // Esegue `do_get` per il comando `get`
    }
    else if (cmd.size() == 3 && cmd_is(cmd[0], "set"))
    {
        *rescode = do_set(cmd, res, reslen); // Esegue `do_set` per il comando `set`
    }
    else if (cmd.size() == 2 && cmd_is(cmd[0], "del"))
    {
        *rescode = do_del(cmd, res, reslen); // Esegue `do_del` per il comando `del`
    }
    else
    {
        // Se il comando non è riconosciuto
        *rescode = RES_ERR; // Imposta il codice di errore
        const char *msg = "Unknown cmd";
        strcpy((char *)res, msg); // Copia il messaggio di errore nel buffer di risposta
        *reslen = strlen(msg);    // Imposta la lunghezza del messaggio di errore
        return 0;
    }
    return 0; // Ritorna successo
}

// Funzione per gestire una singola richiesta
static bool try_one_request(Conn *conn)
{
    // try to parse a request from the buffer
    if (conn->rbuf_size < 4)
    {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg)
    {
        msg("too long");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size)
    {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }

    // got one request, generate the response.
    uint32_t rescode = 0;
    uint32_t wlen = 0;
    int32_t err = do_request(
        &conn->rbuf[4], len,
        &rescode, &conn->wbuf[4 + 4], &wlen);
    if (err)
    {
        conn->state = STATE_END;
        return false;
    }
    wlen += 4;
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], &rescode, 4);
    conn->wbuf_size = 4 + wlen;

    // remove the request from the buffer.
    // note: frequent memmove is inefficient.
    // note: need better handling for production code.
    size_t remain = conn->rbuf_size - 4 - len;
    if (remain)
    {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;

    // change state
    conn->state = STATE_RES;
    state_res(conn);

    // continue the outer loop if the request was fully processed
    return (conn->state == STATE_REQ);
}

// Funzione per riempire il buffer di lettura
static bool try_fill_buffer(Conn *conn)
{
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = 0;
    do
    {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;      // Calcola la capacità rimanente del buffer
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap); // Legge dati dal file descriptor
    } while (rv < 0 && errno == EINTR); // Riprova se l'errore è EINTR
    if (rv < 0 && errno == EAGAIN)
    {
        return false;
    }
    if (rv < 0)
    {
        msg("read() error");
        conn->state = STATE_END; // Imposta lo stato a END se c'è un errore di lettura
        return false;
    }
    if (rv == 0)
    {
        if (conn->rbuf_size > 0)
        {
            msg("unexpected EOF");
        }
        else
        {
            msg("EOF");
        }
        conn->state = STATE_END; // Imposta lo stato a END se il client chiude la connessione
        return false;
    }

    conn->rbuf_size += (size_t)rv; // Aggiorna la dimensione del buffer di lettura
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    while (try_one_request(conn))
    {
    } // Gestisce le richieste finché ce ne sono nel buffer
    return (conn->state == STATE_REQ); // Ritorna true se lo stato è REQ
}

// Funzione per gestire lo stato di richiesta
static void state_req(Conn *conn)
{
    while (try_fill_buffer(conn))
    {
    } // Riempie il buffer finché ce ne sono dati disponibili
}

// Funzione per svuotare il buffer di scrittura
static bool try_flush_buffer(Conn *conn)
{
    ssize_t rv = 0;
    do
    {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;          // Calcola i byte rimanenti nel buffer di scrittura
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain); // Scrive i dati nel file descriptor
    } while (rv < 0 && errno == EINTR); // Riprova se l'errore è EINTR
    if (rv < 0 && errno == EAGAIN)
    {
        return false;
    }
    if (rv < 0)
    {
        msg("write() error");
        conn->state = STATE_END; // Imposta lo stato a END se c'è un errore di scrittura
        return false;
    }
    conn->wbuf_sent += (size_t)rv; // Aggiorna il contatore dei byte inviati
    assert(conn->wbuf_sent <= conn->wbuf_size);
    if (conn->wbuf_sent == conn->wbuf_size)
    {
        conn->state = STATE_REQ; // Imposta lo stato a REQ se tutti i dati sono stati inviati
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    return true;
}

// Funzione per gestire lo stato di risposta
static void state_res(Conn *conn)
{
    while (try_flush_buffer(conn))
    {
    } // Svuota il buffer finché ce ne sono dati da inviare
}

// Funzione per gestire l'I/O delle connessioni
static void connection_io(Conn *conn)
{
    if (conn->state == STATE_REQ)
    {
        state_req(conn); // Gestisce lo stato di richiesta
    }
    else if (conn->state == STATE_RES)
    {
        state_res(conn); // Gestisce lo stato di risposta
    }
    else
    {
        assert(0); // Se lo stato è sconosciuto, genera un'asserzione
    }
}

// Funzione principale
int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0); // Crea un socket TCP
    if (fd < 0)
    {
        die("socket()");
    }

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)); // Imposta l'opzione SO_REUSEADDR

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);                              // Imposta la porta
    addr.sin_addr.s_addr = htonl(INADDR_ANY);                 // Imposta l'indirizzo IP
    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr)); // Associa il socket all'indirizzo e porta
    if (rv)
    {
        die("bind()");
    }

    rv = listen(fd, SOMAXCONN); // Mette il socket in ascolto per connessioni in ingresso
    if (rv)
    {
        die("listen()");
    }

    std::vector<Conn *> fd2conn; // Vettore per mappare file descriptor alle connessioni
    fd_set_nb(fd);               // Imposta il socket in modalità non bloccante

    std::vector<struct pollfd> poll_args; // Vettore per gli argomenti di poll
    while (true)
    {
        poll_args.clear();
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd); // Aggiunge il socket del server a poll_args

        for (Conn *conn : fd2conn)
        {
            if (!conn)
            {
                continue;
            }
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT; // Imposta l'evento per poll
            pfd.events = pfd.events | POLLERR;                          // Aggiunge l'evento di errore
            poll_args.push_back(pfd);                                   // Aggiunge il file descriptor del client a poll_args
        }

        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000); // Attende gli eventi
        if (rv < 0)
        {
            die("poll()");
        }

        for (size_t i = 1; i < poll_args.size(); ++i)
        {
            if (poll_args[i].revents)
            {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn); // Gestisce l'I/O della connessione
                if (conn->state == STATE_END)
                {
                    fd2conn[conn->fd] = NULL;
                    (void)close(conn->fd); // Chiude la connessione
                    free(conn);            // Libera la memoria della connessione
                }
            }
        }

        if (poll_args[0].revents)
        {
            (void)accept_new_conn(fd2conn, fd); // Accetta una nuova connessione
        }
    }

    return 0;
}