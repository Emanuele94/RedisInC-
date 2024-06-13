#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

void die(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

int main() 
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);  // Crea un socket TCP
    if (fd < 0) {
        die("socket()");
    }

    struct sockaddr_in addr = {};  // Struttura per l'indirizzo del server
    addr.sin_family = AF_INET;  // Famiglia di indirizzi IPv4
    addr.sin_port = htons(1234);  // Porta del server in network byte order (big-endian)
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // Indirizzo IP del server (localhost, 127.0.0.1)

    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));  // Connette al server
    if (rv < 0) {
        die("connect");  // Gestisce l'errore se la connessione fallisce
    }

    // Ottieni l'indirizzo del peer remoto (server) usando getpeername
    struct sockaddr_in peer_addr; // Struttura per memorizzare l'indirizzo del peer remoto
    socklen_t peer_addr_len = sizeof(peer_addr); // Lunghezza della struttura dell'indirizzo del peer
    if (getpeername(fd, (struct sockaddr *)&peer_addr, &peer_addr_len) < 0) {
        die("getpeername"); // Gestisce l'errore se getpeername fallisce
    }
    char peer_ip[INET_ADDRSTRLEN]; // Buffer per memorizzare l'indirizzo IP del peer in formato string
    inet_ntop(AF_INET, &peer_addr.sin_addr, peer_ip, sizeof(peer_ip)); // Converte l'indirizzo IP in formato stringa
    printf("Connected to server at %s:%d\n", peer_ip, ntohs(peer_addr.sin_port)); // Stampa l'indirizzo IP locale e la porta

    // Ottieni l'indirizzo locale del socket usando getsockname
    struct sockaddr_in local_addr; // Struttura per memorizzare l'indirizzo locale del socket
    socklen_t local_addr_len = sizeof(local_addr); // Lunghezza della struttura dell'indirizzo locale
    if (getsockname(fd, (struct sockaddr *)&local_addr, &local_addr_len) < 0) {
        die("getsockname"); // Gestisce l'errore se getsockname fallisce
    }
    char local_ip[INET_ADDRSTRLEN]; // Buffer per memorizzare l'indirizzo IP locale in formato stringa
    inet_ntop(AF_INET, &local_addr.sin_addr, local_ip, sizeof(local_ip)); // Converte l'indirizzo IP in formato stringa
    printf("Local socket address is %s:%d\n", local_ip, ntohs(local_addr.sin_port)); // Stampa l'indirizzo IP locale e la porta


    const char *msg = "hello";  // Messaggio da inviare al server
    ssize_t sent = write(fd, msg, strlen(msg));  // Invia il messaggio al server
    if (sent < 0) {
        die("write");  // Gestisce l'errore se l'invio del messaggio fallisce
    }

    char rbuf[64] = {};  // Buffer per leggere la risposta dal server
    ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);  // Legge la risposta dal server
    if (n < 0) {
        die("read");  // Gestisce l'errore se la lettura della risposta fallisce
    }
    printf("server says: %s\n", rbuf);  // Stampa ciò che il server ha inviato

    close(fd);  // Chiude il socket
    return 0;
}
