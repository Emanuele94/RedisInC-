#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Funzione per gestire errori fatali e terminare il programma
void die(const char *message)
{
    perror(message); // Stampa il messaggio di errore specificato con un testo aggiuntivo
    exit(EXIT_FAILURE); // Termina il programma con stato di errore
}

// Funzione per gestire messaggi di errore
void msg(const char *message)
{
    perror(message); // Stampa il messaggio di errore specificato
}

// Funzione per gestire la logica di cosa fare quando arriva una connessione
static void do_something(int connfd)
{
    char rbuf[64] = {}; // Buffer per leggere i dati dal client
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1); // Legge i dati dal client
    if (n < 0) // Se la lettura fallisce
    {
        msg("read() error"); // Stampa un messaggio di errore
        return; // Esce dalla funzione
    }
    printf("client says: %s\n", rbuf); // Stampa ciò che il client ha inviato

    char wbuf[] = "world"; // Buffer di risposta da inviare al client
    ssize_t w = write(connfd, wbuf, strlen(wbuf)); // Invia la risposta al client
    if (w < 0) {  // Se l'invio della risposta fallisce
        msg("write() error");  // Stampa un messaggio di errore
    } else if (w != (ssize_t)strlen(wbuf)) {  // Se non tutti i dati sono stati inviati
        msg("incomplete write()");  // Stampa un messaggio di errore
    }    
}


// Funzione principale del programma
int main()
{

    int fd = socket(AF_INET, SOCK_STREAM, 0); // Crea un socket TCP
    if (fd < 0) { // Se la creazione del socket fallisce
        die("socket()"); // Gestisce l'errore
    }

    // Imposta l'opzione SO_REUSEADDR per riutilizzare l'indirizzo IP e la porta subito dopo la chiusura del socket
    int val = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
        die("setsockopt()"); // Gestisce l'errore se l'impostazione fallisce
    }

    // bind, this is the syntax that deals with IPv4 addresses
    struct sockaddr_in addr = {}; // Struttura per l'indirizzo del server
    addr.sin_family = AF_INET; // Famiglia di indirizzi IPv4
    addr.sin_port = htons(1234);  // Porta del server in network byte order (big-endian)
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // Indirizzo IP del server (localhost, 127.0.0.1)

    // Collega il socket all'indirizzo specificato
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
         die("bind()"); // Gestisce l'errore se il binding fallisce
    }

    // Mette il socket in modalità di ascolto per connessioni in ingresso
    if (listen(fd, SOMAXCONN) < 0) {
        die("listen()"); // Gestisce l'errore se il listen fallisce
    }

    while (true)
    {
        // accept
        struct sockaddr_in client_addr = {}; // Struttura per l'indirizzo del client
        socklen_t addrlen = sizeof(client_addr); // Lunghezza della struttura dell'indirizzo del client
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen); // Accetta una connessione in entrata
        if (connfd < 0) // Se l'accettazione della connessione fallisce
        {
            msg("accept() error"); // Gestisce l'errore
            continue; // Salta al prossimo ciclo del loop
        }

        do_something(connfd); // Gestisce la logica di cosa fare con la connessione
        close(connfd); // Chiude il socket della connessione
    }

    close(fd); // Chiude il socket principale del server
    return 0; // Termina il programma con successo
}