#include <assert.h> // Libreria per assert
#include <stdint.h> // Libreria per tipi di dati interi a lunghezza fissa
#include <stdlib.h> // Libreria per funzioni di utilità generale
#include <string.h> // Libreria per funzioni di manipolazione delle stringhe
#include <stdio.h> // Libreria per input/output
#include <errno.h> // Libreria per gestione degli errori
#include <unistd.h> // Libreria per funzioni POSIX
#include <arpa/inet.h> // Libreria per funzioni di conversione di indirizzi di rete
#include <sys/socket.h> // Libreria per funzioni di gestione dei socket
#include <netinet/ip.h> // Libreria per strutture e funzioni di protocollo IP
#include <string> // Libreria per la classe string di C++
#include <vector> // Libreria per il contenitore vector di C++

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg); // Stampa un messaggio di errore su stderr
}

static void die(const char *msg) {
    int err = errno; // Ottiene il valore di errno
    fprintf(stderr, "[%d] %s\n", err, msg); // Stampa il messaggio di errore con il valore di errno
    abort(); // Termina il programma
}

static int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n); // Legge dal file descriptor
        if (rv <= 0) {
            return -1; // Errore o EOF inaspettato
        }
        assert((size_t)rv <= n); // Verifica che rv non sia maggiore di n
        n -= (size_t)rv; // Riduce n del numero di byte letti
        buf += rv; // Avanza il puntatore del buffer
    }
    return 0; // Successo
}

static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n); // Scrive sul file descriptor
        if (rv <= 0) {
            return -1; // Errore
        }
        assert((size_t)rv <= n); // Verifica che rv non sia maggiore di n
        n -= (size_t)rv; // Riduce n del numero di byte scritti
        buf += rv; // Avanza il puntatore del buffer
    }
    return 0; // Successo
}

const size_t k_max_msg = 4096; // Dimensione massima del messaggio

static int32_t send_req(int fd, const std::vector<std::string> &cmd) {
    uint32_t len = 4;
    for (const std::string &s : cmd) {
        len += 4 + s.size(); // Calcola la lunghezza totale del messaggio
    }
    if (len > k_max_msg) {
        return -1; // Errore se il messaggio è troppo lungo
    }

    char wbuf[4 + k_max_msg];
    memcpy(&wbuf[0], &len, 4); // Copia la lunghezza del messaggio nel buffer
    uint32_t n = cmd.size();
    memcpy(&wbuf[4], &n, 4); // Copia il numero di stringhe nel buffer
    size_t cur = 8;
    for (const std::string &s : cmd) {
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[cur], &p, 4); // Copia la lunghezza della stringa nel buffer
        memcpy(&wbuf[cur + 4], s.data(), s.size()); // Copia la stringa nel buffer
        cur += 4 + s.size(); // Avanza il puntatore del buffer
    }
    return write_all(fd, wbuf, 4 + len); // Invia il buffer sul file descriptor
}

static int32_t read_res(int fd) {
    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4); // Legge l'header della risposta
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4); // Copia la lunghezza della risposta
    if (len > k_max_msg) {
        msg("too long");
        return -1; // Errore se la risposta è troppo lunga
    }

    // reply body
    err = read_full(fd, &rbuf[4], len); // Legge il corpo della risposta
    if (err) {
        msg("read() error");
        return err;
    }

    // print the result
    uint32_t rescode = 0;
    if (len < 4) {
        msg("bad response");
        return -1; // Errore se la risposta è troppo corta
    }
    memcpy(&rescode, &rbuf[4], 4); // Copia il codice di risposta
    printf("server says: [%u] %.*s\n", rescode, len - 4, &rbuf[8]); // Stampa il codice di risposta e il messaggio
    return 0; // Successo
}

int main(int argc, char **argv) {
    int fd = socket(AF_INET, SOCK_STREAM, 0); // Crea un socket
    if (fd < 0) {
        die("socket()");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234); // Imposta la porta
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); // Imposta l'indirizzo IP (127.0.0.1)
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr)); // Connette il socket all'indirizzo
    if (rv) {
        die("connect");
    }

    std::vector<std::string> cmd;
    for (int i = 1; i < argc; ++i) {
        cmd.push_back(argv[i]); // Costruisce il comando dal vettore di argomenti
    }
    int32_t err = send_req(fd, cmd); // Invia la richiesta
    if (err) {
        goto L_DONE;
    }
    err = read_res(fd); // Legge la risposta
    if (err) {
        goto L_DONE;
    }

L_DONE:
    close(fd); // Chiude il socket
    return 0; // Termina il programma
}