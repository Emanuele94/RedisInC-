#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>

// Funzione per gestire errori fatali e terminare il programma
void die(const char *message) {
    perror(message);    // Stampa il messaggio di errore specificato con un testo aggiuntivo
    exit(EXIT_FAILURE); // Termina il programma con stato di errore
}

// Funzione per stampare un messaggio di errore
static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg); // Stampa il messaggio di errore su stderr
}

// Funzione per leggere completamente n byte dal file descriptor fd in buf
static int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n); // Legge fino a n byte dal socket
        if (rv <= 0) {
            return -1;  // Errore, o EOF inaspettato
        }
        assert((size_t)rv <= n); // Verifica che il numero di byte letti sia <= n
        n -= (size_t)rv;         // Aggiorna n con i byte rimanenti da leggere
        buf += rv;               // Sposta il puntatore del buffer oltre i byte letti
    }
    return 0;
}

// Funzione per scrivere completamente n byte da buf al file descriptor fd
static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n); // Scrive fino a n byte sul socket
        if (rv <= 0) {
            return -1;  // Errore
        }
        assert((size_t)rv <= n); // Verifica che il numero di byte scritti sia <= n
        n -= (size_t)rv;         // Aggiorna n con i byte rimanenti da scrivere
        buf += rv;               // Sposta il puntatore del buffer oltre i byte scritti
    }
    return 0;
}

const size_t k_max_msg = 4096; // Costante per la dimensione massima del messaggio

// Funzione per inviare una richiesta al server e gestire la risposta
static int32_t query(int fd, const char *text) {
    uint32_t len = (uint32_t)strlen(text); // Calcola la lunghezza del messaggio
    if (len > k_max_msg) {
        return -1;  // Messaggio troppo lungo, gestisce l'errore
    }

    char wbuf[4 + k_max_msg]; // Buffer per l'header (4 byte) e il corpo del messaggio
    memcpy(wbuf, &len, 4);    // Copia la lunghezza del messaggio nell'header (assume little endian)
    memcpy(&wbuf[4], text, len); // Copia il messaggio nel buffer dopo l'header

    if (int32_t err = write_all(fd, wbuf, 4 + len)) {
        return err; // Gestisce eventuali errori durante la scrittura
    }

    // Buffer per la risposta, 4 byte per l'header e k_max_msg per il corpo del messaggio
    char rbuf[4 + k_max_msg + 1];
    errno = 0; // Azzera errno per rilevare eventuali errori

    // Legge l'header (4 byte) dalla connessione
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            msg("EOF"); // Fine del file, nessun dato più da leggere
        } else {
            msg("read() error"); // Errore nella lettura
        }
        return err; // Ritorna l'errore
    }

    memcpy(&len, rbuf, 4); // Copia la lunghezza del messaggio dall'header (assume little endian)
    if (len > k_max_msg) {
        msg("too long"); // Se il messaggio è troppo lungo, stampa un messaggio di errore
        return -1;       // Ritorna -1 per indicare un errore
    }

    // Legge il corpo della risposta (lungo len byte) dalla connessione
    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error"); // Errore nella lettura del corpo della risposta
        return err;          // Ritorna l'errore
    }

    rbuf[4 + len] = '\0'; // Aggiunge un terminatore nullo per trattare il buffer come stringa C

    if (strlen(&rbuf[4]) > 0) {
        printf("server says: %s\n", &rbuf[4]); // Stampa la risposta ricevuta dal server
    }

    return 0; // Operazione completata con successo
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0); // Crea un socket TCP
    if (fd < 0) {
        die("socket()"); // Gestisce l'errore se la creazione del socket fallisce
    }

    struct sockaddr_in addr = {}; // Struttura per l'indirizzo del server
    addr.sin_family = AF_INET;    // Famiglia di indirizzi IPv4
    addr.sin_port = htons(1234);  // Porta del server in network byte order (big-endian)
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Indirizzo IP del server (localhost, 127.0.0.1)

    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr)); // Connette al server
    if (rv < 0) {
        die("connect"); // Gestisce l'errore se la connessione fallisce
    }

    // Ottiene l'indirizzo del peer remoto (server) usando getpeername
    struct sockaddr_in peer_addr; // Struttura per memorizzare l'indirizzo del peer remoto
    socklen_t peer_addr_len = sizeof(peer_addr); // Lunghezza della struttura dell'indirizzo del peer
    if (getpeername(fd, (struct sockaddr *)&peer_addr, &peer_addr_len) < 0) {
        die("getpeername"); // Gestisce l'errore se getpeername fallisce
    }
    char peer_ip[INET_ADDRSTRLEN]; // Buffer per memorizzare l'indirizzo IP del peer in formato stringa
    inet_ntop(AF_INET, &peer_addr.sin_addr, peer_ip, sizeof(peer_ip)); // Converte l'indirizzo IP in formato stringa
    printf("Connected to server at %s:%d\n", peer_ip, ntohs(peer_addr.sin_port)); // Stampa l'indirizzo IP e la porta del peer

    // Ottiene l'indirizzo locale del socket usando getsockname
    struct sockaddr_in local_addr; // Struttura per memorizzare l'indirizzo locale del socket
    socklen_t local_addr_len = sizeof(local_addr); // Lunghezza della struttura dell'indirizzo locale
    if (getsockname(fd, (struct sockaddr *)&local_addr, &local_addr_len) < 0) {
        die("getsockname"); // Gestisce l'errore se getsockname fallisce
    }
    char local_ip[INET_ADDRSTRLEN]; // Buffer per memorizzare l'indirizzo IP locale in formato stringa
    inet_ntop(AF_INET, &local_addr.sin_addr, local_ip, sizeof(local_ip)); // Converte l'indirizzo IP in formato stringa
    printf("Local socket address is %s:%d\n", local_ip, ntohs(local_addr.sin_port)); // Stampa l'indirizzo IP locale e la porta

    // Esegue più richieste al server
    int32_t err = query(fd, "hello1");
    if (err) {
        goto L_DONE; // Salta alla fine se c'è un errore
    }
    err = query(fd, "hello2");
    if (err) {
        goto L_DONE; // Salta alla fine se c'è un errore
    }
    err = query(fd, "hello3");
    if (err) {
        goto L_DONE; // Salta alla fine se c'è un errore
    }

L_DONE:
    close(fd); // Chiude il socket
    return 0;  // Termina il programma con successo
}