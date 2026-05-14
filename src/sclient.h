#ifndef SCLIENT_H
#define SCLIENT_H

// Default addresa server 
#define CLIENT_SERVER_HOST  "127.0.0.1"
#define CLIENT_SERVER_PORT  9090

//Conectare la server, trimitere request, primire raspuns
//Returneaza 0 pt success, -1 la eroare

int client_run(const char *host, int port, const char *video_path, int chunk_size, int threshold, long *no_motion_out);

#endif // SCLIENT_H
