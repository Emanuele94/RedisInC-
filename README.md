![alt text](../misc/img/tcp_server-2024-06-28-155622.png)

# Simple Non-blocking TCP Server

Questo progetto implementa un semplice server TCP non-bloccante utilizzando C++. Il server può gestire più connessioni client simultaneamente senza bloccare, utilizzando la chiamata di sistema `poll`.

## Caratteristiche

- I/O non-bloccante per gestire più connessioni.
- Supporta la comunicazione di base richiesta-risposta.
- Gestisce le connessioni utilizzando una macchina a stati.

## Requisiti

- Compilatore C++ (supporta C++11 o successivi)
- Sistema operativo compatibile con POSIX (Linux, macOS)

## Costruire il Progetto

Per costruire il progetto, compila il file sorgente con un compilatore C++. Ad esempio:

```sh
g++ -o server server.cpp
```

## Eseguire il Server

Esegui il file eseguibile del server compilato. Il server ascolta sulla porta 1234.

```sh
./server
```

## Client TCP di Test

Il progetto include un client TCP per testare il server. Il client invia richieste al server e legge le risposte.

### Costruire il Client

Per costruire il client, compila il file sorgente con un compilatore C++. Ad esempio:

```sh
g++ -o client client.cpp
```

### Eseguire il Client

Esegui il file eseguibile del client con i comandi che desideri inviare al server. Ad esempio:

```sh
./client set key value
./client get key
./client del key
```

## File di Codice

- `server.cpp`: Implementazione del server TCP non-bloccante.
- `client.cpp`: Implementazione del client TCP per testare il server.
