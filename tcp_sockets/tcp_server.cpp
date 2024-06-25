#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <assert.h>
#include <errno.h>

// Funzione per gestire errori fatali e terminare il programma
void die(const char *message) {
    perror(message);    // Stampa il messaggio di errore specificato con un testo aggiuntivo
    exit(EXIT_FAILURE); // Termina il programma con stato di errore
}

// Funzione per gestire messaggi di errore
void msg(const char *message) {
    perror(message); // Stampa il messaggio di errore specificato
}

// Funzione per leggere completamente n byte dal file descriptor fd in buf
static int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n); // Legge fino a n byte dal socket
        if (rv <= 0) {
            return -1; // errore, o EOF inaspettato
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
            return -1; // errore
        }
        assert((size_t)rv <= n); // Verifica che il numero di byte scritti sia <= n
        n -= (size_t)rv;         // Aggiorna n con i byte rimanenti da scrivere
        buf += rv;               // Sposta il puntatore del buffer oltre i byte scritti
    }
    return 0;
}

// Costante per la dimensione massima del messaggio
const size_t k_max_msg = 4096;

// Funzione per gestire una singola richiesta dal client
static int32_t one_request(int connfd) {
    // Buffer per l'header (4 byte) e il corpo del messaggio più un byte nullo
    char rbuf[4 + k_max_msg + 1];
    errno = 0; // Azzera errno per rilevare eventuali errori

    // Legge i primi 4 byte (header) dalla connessione e li memorizza in rbuf
    int32_t err = read_full(connfd, rbuf, 4);
    if (err) {
        // Se c'è un errore nella lettura, controlla errno per determinare il tipo di errore
        if (errno == 0) {
            msg("EOF"); // Fine del file, nessun dato più da leggere
        } else {
            msg("read() error"); // Errore nella lettura
        }
        return err; // Ritorna l'errore
    }

    uint32_t len = 0;      // Variabile per memorizzare la lunghezza del messaggio
    memcpy(&len, rbuf, 4); // Copia i primi 4 byte di rbuf in len, assumendo little endian
    if (len > k_max_msg) {
        msg("too long"); // Se il messaggio è troppo lungo, stampa un messaggio di errore
        return -1;       // Ritorna -1 per indicare un errore
    }

    // Legge il corpo del messaggio dalla connessione e lo memorizza in rbuf a partire dal 5° byte
    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        msg("read() error"); // Se c'è un errore nella lettura, stampa un messaggio di errore
        return err;          // Ritorna l'errore
    }

    // Aggiunge un carattere nullo alla fine del messaggio per renderlo una stringa C valida
    rbuf[4 + len] = '\0';
    // Stampa il messaggio ricevuto dal client
    printf("client says: %s\n", &rbuf[4]);

    // Prepara una risposta usando lo stesso protocollo
    const char reply[] = "world";  // Messaggio di risposta
    char wbuf[4 + sizeof(reply)];  // Buffer per contenere l'header e la risposta
    len = (uint32_t)strlen(reply); // Calcola la lunghezza della risposta
    memcpy(wbuf, &len, 4);         // Copia la lunghezza della risposta nei primi 4 byte di wbuf
    memcpy(&wbuf[4], reply, len);  // Copia la risposta nei byte successivi di wbuf

    // Invia la risposta al client
    return write_all(connfd, wbuf, 4 + len);
}

// Funzione principale del programma
int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0); // Crea un socket TCP
    if (fd < 0) {                             // Se la creazione del socket fallisce
        die("socket()");                      // Gestisce l'errore
    }

    // Imposta l'opzione SO_REUSEADDR per riutilizzare l'indirizzo IP e la porta subito dopo la chiusura del socket
    int val = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
        die("setsockopt()"); // Gestisce l'errore se l'impostazione fallisce
    }

    struct sockaddr_in addr = {};                  // Struttura per l'indirizzo del server
    addr.sin_family = AF_INET;                     // Famiglia di indirizzi IPv4
    addr.sin_port = htons(1234);                   // Porta del server in network byte order (big-endian)
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Indirizzo IP del server (localhost, 127.0.0.1)

    // Collega il socket all'indirizzo specificato
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        die("bind()"); // Gestisce l'errore se il binding fallisce
    }

    // Mette il socket in modalità di ascolto per connessioni in ingresso
    if (listen(fd, SOMAXCONN) < 0) {
        die("listen()"); // Gestisce l'errore se il listen fallisce
    }

    while (1) {
        // Accetta una connessione in entrata
        struct sockaddr_in client_addr = {};                                // Struttura per l'indirizzo del client
        socklen_t addrlen = sizeof(client_addr);                            // Lunghezza della struttura dell'indirizzo del client
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen); // Accetta una connessione in entrata
        if (connfd < 0) {                                                   // Se l'accettazione della connessione fallisce
            msg("accept() error"); // Gestisce l'errore
            continue;              // Salta al prossimo ciclo del loop
        }

        // Gestisce tutte le richieste del client finché la connessione è attiva
        while (1) {
            int32_t err = one_request(connfd);
            if (err) {
                break; // Esce dal loop se c'è un errore
            }
        }

        close(connfd); // Chiude il socket della connessione
    }

    close(fd); // Chiude il socket principale del server
    return 0;  // Termina il programma con successo
}