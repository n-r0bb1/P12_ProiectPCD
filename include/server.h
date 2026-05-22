#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>  /* size_t */

//Adresa Port
#define SERVER_PORT      9090
#define SERVER_BACKLOG      8
#define SERVER_BUF_SIZE  1024

//Numar maxim de muncitori paraleli
#define SERVER_MAX_WORKERS 16

//Functie pentru manage clienti
void server_handle_client(int client_fd, int max_workers);

#endif //SERVER_H 
