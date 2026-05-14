#include "../include/server.h"   //Header principal pentru server (functii de procesare clienti, logica server)
#include "../include/config.h"   //Header pentru configuratie (structura ServerConfig + functii de load config)

#include <stdio.h>  //NOLINT
#include <string.h>  //Functii pentru manipulare siruri de caractere (memset, etc.)
#include <stdint.h>  //Tipuri fixe de date (uint16_t etc.)
#include <stdlib.h>  //Functii standard (exit, malloc, etc. daca ar fi necesare)
#include <unistd.h>  //NOLINT //Functii POSIX (close, access, fork, getopt)
#include <errno.h>   //Pentru coduri de eroare globale ale sistemului
#include <sys/socket.h> //API pentru socket-uri TCP/UDP
#include <netinet/in.h>  //Structuri pentru internet sockets (sockaddr_in)

#include <arpa/inet.h>   //Conversii IP (inet_ntop)
#include <sys/wait.h>    //Pentru waitpid (gestionare procese copil)

#define USAGE_MSG  "Usage: ./server -f <config_file>\n" //Mesaj afisat daca utilizatorul nu da parametrii corecti
#define ERRBUF_SIZE 64  //Dimensiune buffer pentru mesaje de eroare convertite din errno
#define CFG_PATH_MAX 512 //Dimensiune maxima pentru path-ul fisierului de configurare

// Afișează modul de utilizare al serverului 
static void print_usage(void)
{
    (void)fprintf(stderr, USAGE_MSG); //Afisam mesajul de utilizare pe stderr (flux de erori)
}

// Curăță procesele copil terminate (evită zombie processes) 
static void reap_children(void)
{
    int status = 0; //Variabila in care se stocheaza statusul proceselor copil terminate

    // Recoltează toate procesele copil terminate fără blocare
    while (waitpid(-1, &status, WNOHANG) > 0) {
        /* waitpid(-1, ...) => orice proces copil
           WNOHANG => nu blocheaza serverul, doar verifica
           bucla continua pana nu mai sunt copii terminati */
    }
}

// Inițializează socket-ul serverului (bind + listen) 
static int setup_server_socket(int *out_fd)
{
    char errbuf[ERRBUF_SIZE]; //Buffer pentru conversia erorilor errno in string lizibil

    // Creare socket TCP 
    int sfd = socket(AF_INET, SOCK_STREAM, 0); //AF_INET = IPv4, SOCK_STREAM = TCP
    if (sfd == -1) { //Verificam daca socket-ul a esuat
        (void)strerror_r(errno, errbuf, sizeof(errbuf)); //Transformam errno in mesaj text
        (void)fprintf(stderr, "[server] socket: %s\n", errbuf); //Afisam eroarea
        return -1; //Semnalam esec
    }

    // Setare SO_REUSEADDR pentru a evita "Address already in use" 
    int optval = 1; //Activam optiunea
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, //NOLINT
                   &optval, sizeof(optval)) == -1) {
        (void)strerror_r(errno, errbuf, sizeof(errbuf)); //Convertim eroarea
        (void)fprintf(stderr, "[server] setsockopt: %s\n", errbuf); //Afisam eroarea
        (void)close(sfd); //Inchidem socket-ul partial creat
        return -1;
    }

    // Configurare adresă server
    struct sockaddr_in addr; //Structura pentru adresa IPv4 a serverului
    addr.sin_family      = AF_INET; //Familia IPv4
    addr.sin_port        = htons((uint16_t)SERVER_PORT); //Port convertit in network byte order
    addr.sin_addr.s_addr = htonl(INADDR_ANY); //Accepta conexiuni pe orice interfata

    /* Inițializare padding */
    (void)memset(addr.sin_zero, 0, sizeof(addr.sin_zero)); //Curata campurile nefolosite

    // Bind socket la port 
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) { //Leaga socket-ul de port
        (void)strerror_r(errno, errbuf, sizeof(errbuf)); //Eroare bind
        (void)fprintf(stderr, "[server] bind: %s\n", errbuf);
        (void)close(sfd); //Curata resursa
        return -1;
    }

    // Ascultă conexiuni 
    if (listen(sfd, SERVER_BACKLOG) == -1) { //Punem socket-ul in mod listen
        (void)strerror_r(errno, errbuf, sizeof(errbuf));
        (void)fprintf(stderr, "[server] listen: %s\n", errbuf);
        (void)close(sfd);
        return -1;
    }

    *out_fd = sfd; //Returnam descriptorul socket-ului prin pointer
    return 0; //Succes
}

//Loop principal: acceptă conexiuni și creează procese copil 
static void accept_loop(int server_fd, int max_workers)
{
    char errbuf[ERRBUF_SIZE]; //Buffer pentru erori sistem
    char client_ip[INET_ADDRSTRLEN]; //Buffer pentru IP-ul clientului in format text

    for (;;) { //Loop infinit -> server permanent activ

        // Curățare procese zombie 
        reap_children(); //Eliberam procesele copil terminate

        struct sockaddr_in client_addr; //Structura pentru client conectat
        socklen_t client_len = sizeof(client_addr); //Dimensiunea structurii

        (void)memset(&client_addr, 0, sizeof(client_addr)); //Initializare curata

        // Acceptare conexiune client
        int cfd = accept(server_fd,
                         (struct sockaddr *)&client_addr,
                         &client_len); //Blocant pana vine client

        if (cfd == -1) { //Verificare eroare accept
            if (errno == EINTR) { //Intrerupere semnal -> retry
                continue;
            }

            (void)strerror_r(errno, errbuf, sizeof(errbuf));
            (void)fprintf(stderr, "[server] accept: %s\n", errbuf);
            continue;
        }

        // Afișare IP client 
        if (inet_ntop(AF_INET, &client_addr.sin_addr,
                      client_ip, sizeof(client_ip)) != NULL) {
            (void)fprintf(stdout,
                          "[server] accepted connection from %s\n", client_ip);
        }

        // Fork pentru a crea proces copil 
        pid_t pid = fork();//NOLINT //Creem proces copil pentru fiecare client

        if (pid == -1) //NOLINT //Eroare la fork
        {
            (void)strerror_r(errno, errbuf, sizeof(errbuf));
            (void)fprintf(stderr, "[server] fork: %s\n", errbuf);
            (void)close(cfd); //Inchidem conexiunea daca nu putem crea copil
            continue;
        }
        // === PROCES COPIL === 
        if (pid == 0) {

            /* Copilul nu mai are nevoie de server socket */
            if (close(server_fd) == -1) { //Inchidem socket-ul server in copil
                _Exit(1); //iesire imediata daca esueaza
            }

            // Procesare client 
            server_handle_client(cfd, max_workers); //Functia principala de procesare client

            // Închidere conexiune client 
            if (close(cfd) == -1) { //Inchidem conexiunea client
                _Exit(1);
            }

            _Exit(0); //Terminare proces copil
        }

        // === PROCES PĂRINTE === 

        // Părintele închide descriptorul clientului 
        if (close(cfd) == -1) { //Parintele nu mai are nevoie de socket client
            (void)strerror_r(errno, errbuf, sizeof(errbuf));
            (void)fprintf(stderr, "[server] close cfd: %s\n", errbuf);
        }
    }
}
// Entry point server
int main(int argc, char *argv[])
{
    char cfg_path[CFG_PATH_MAX]; //Buffer pentru path-ul fisierului de configurare
    cfg_path[0] = '\0'; //Initializare ca string gol

    int opt = 0; //Variabila pentru optiunile getopt

    // Parsare argumente linie comandă (-f config file) 
    while ((opt = getopt(argc, argv, "f:")) != -1) { //NOLINT
        switch (opt) {

        case 'f':
            /* Copiere path config */
            if (snprintf(cfg_path, sizeof(cfg_path), "%s", optarg /* NOLINT(misc-include-cleaner) */) < 0) {
                (void)fprintf(stderr, "[server] config path too long\n");
                return 1;
            }
            break;

        default:
            print_usage(); //Afisam modul corect de utilizare
            return 1;
        }
    }

    // Verificare existență argument config 
    if (cfg_path[0] == '\0') { //Daca nu s-a setat path-ul
        print_usage();
        return 1;
    }

    // Verificare acces la fișier config 
    if (access(cfg_path, R_OK) != 0) { //Verificam daca fisierul poate fi citit
        (void)fprintf(stderr,
                      "[server] cannot read config file: %s\n", cfg_path);
        return 1;
    }

    // Încărcare configurație server 
    ServerConfig cfg = {0, 0, 0}; //Structura initializata cu valori 0
    if (config_load(cfg_path, &cfg) != 0) { //Incarcam configuratia din fisier
        (void)fprintf(stderr,
                      "[server] failed to load config: %s\n", cfg_path);
        return 1;
    }

    /* Log configurare încărcată */
    (void)fprintf(stdout,
                  "[server] config: max_workers=%d chunk_size=%d threshold=%d\n",
                  cfg.max_workers, cfg.chunk_size, cfg.threshold);

    // Inițializare socket server 
    int server_fd = -1; //Descriptor server initial invalid
    if (setup_server_socket(&server_fd) != 0) { //Creare socket + bind + listen
        return 1;
    }

    (void)fprintf(stdout, "[server] listening on port %d\n", SERVER_PORT);

    // Start accept loop (server rulează permanent) 
    accept_loop(server_fd, cfg.max_workers); //Loop infinit de server

    // Închidere server (practic nu ajunge aici decât la exit forțat) 
    (void)close(server_fd); //Curatare finala socket server
    return 0; //Exit succes program
}