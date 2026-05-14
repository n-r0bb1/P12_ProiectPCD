#include "../include/sclient.h" // Include header-ul clientului (declarații funcții și structuri) 
#include "../protocol/proto.h" // Include protocolul (request/response + serializare/parsing) 

#include <stdio.h> // Funcții standard I/O (fprintf pentru erori) 
#include <string.h> // Funcții pentru stringuri (strlen, memset, snprintf) 
#include <stdint.h> // Tipuri fixe (uint16_t) 
#include <unistd.h> // API POSIX (read, write, close) 
#include <errno.h> // Coduri de eroare sistem (errno) 
#include <sys/socket.h> // Socket API (socket, connect) 
#include <netinet/in.h> // Structuri pentru IPv4 (sockaddr_in) 
#include <arpa/inet.h> // Conversie IP (inet_pton) 

#define ERRBUF_SIZE 64 // Dimensiunea bufferului pentru mesaje de eroare 

// Tip alias pentru file descriptor 
typedef int SockFd; // Alias semantic pentru socket fd 

// Funcție care citește o linie de pe socket (până la '\n') 
static ssize_t read_line(SockFd sock_fd, char *buf, size_t bufsz)
{
    size_t  pos = 0; // Poziția curentă în bufferul de output 
    ssize_t nr  = 0; // Numărul de bytes citiți la fiecare read 
    char    ch  = '\0'; // Caracter temporar citit din socket 

    // Citește byte cu byte până la newline sau buffer plin 
    while (pos + 1 < bufsz) { // Lăsăm loc pentru '\0' 
        nr = read(sock_fd, &ch, 1); // Citim exact 1 byte 

        if (nr == 0) { // Serverul a închis conexiunea 
            break; // Ieșim din buclă 
        }
        if (nr < 0) { // Eroare la read 
            return -1; // Returnăm eroare 
        }

        buf[pos++] = ch; // Salvăm caracterul citit 

        if (ch == '\n') { // Sfârșit de linie 
            break; // Oprim citirea 
        }
    }

    buf[pos] = '\0'; // Terminator de string C 
    return (ssize_t)pos; // Returnăm lungimea citită 
}

// Aliasuri de tipuri pentru claritate semantică 
typedef int ChunkSize; // Dimensiunea unui chunk video 
typedef int Threshold; // Prag de detecție mișcare 

int client_run(const char *host, int port,
               const char *video_path, ChunkSize chunk_size,
               Threshold threshold, long *no_motion_out)
{
    char errbuf[ERRBUF_SIZE]; // Buffer pentru mesaje de eroare 

    // Verificare parametri de intrare 
    if (!host || !video_path || !no_motion_out || port <= 0) {
        return -1; // Parametri invalizi 
    }

    // Construire request (structura trimisă serverului) 
    ProtoRequest req = {"", 0, 0}; // Inițializare request 

    // Copiere path video în request 
    if (snprintf(req.video_path, sizeof(req.video_path),
                 "%s", video_path) < 0) {
        return -1; // Eroare la copiere 
    }

    // Setăm parametrii de procesare 
    req.chunk_size = chunk_size; // Dimensiune chunk 
    req.threshold  = threshold; // Prag detecție 

    // Serializare request în buffer de rețea 
    char req_buf[PROTO_BUF_SIZE]; // Buffer serializat 
    if (proto_serialize_request(&req, req_buf, sizeof(req_buf)) != 0) {
        (void)fprintf(stderr, "[client] serialize request failed\n"); // Eroare serializare 
        return -1; // Ieșire 
    }

    // Creare socket TCP 
    SockFd sfd = socket(AF_INET, SOCK_STREAM, 0); // Socket IPv4 TCP 
    if (sfd == -1) {
        (void)strerror_r(errno, errbuf, sizeof(errbuf)); // Convertim eroarea 
        (void)fprintf(stderr, "[client] socket: %s\n", errbuf); // Afișare 
        return -1; // Eroare socket 
    }

    // Configurare adresă server 
    struct sockaddr_in srv_addr; // Structură adresă server 
    srv_addr.sin_family = AF_INET; // IPv4 
    srv_addr.sin_port   = htons((uint16_t)port); // Port în network order 

    // Inițializare câmpuri nefolosite 
    (void)memset(srv_addr.sin_zero, 0, sizeof(srv_addr.sin_zero)); // Zero padding 

    // Conversie IP din string în format binar 
    if (inet_pton(AF_INET, host, &srv_addr.sin_addr) != 1) {
        (void)fprintf(stderr,
                      "[client] invalid host address: %s\n", host); // IP invalid 
        (void)close(sfd); // Închidem socket 
        return -1; // Eroare 
    }

    // Conectare la server 
    if (connect(sfd, (struct sockaddr *)&srv_addr,
                sizeof(srv_addr)) == -1) {
        (void)strerror_r(errno, errbuf, sizeof(errbuf)); // Eroare connect 
        (void)fprintf(stderr, "[client] connect: %s\n", errbuf);
        (void)close(sfd); // Cleanup 
        return -1; // Fail 
    }

    // Trimitere request către server 
    size_t  req_len = strlen(req_buf); // Lungime request 
    ssize_t sent    = write(sfd, req_buf, req_len); // Trimitere date 

    // Verificare trimitere completă 
    if (sent != (ssize_t)req_len) {
        (void)strerror_r(errno, errbuf, sizeof(errbuf)); // Eroare write 
        (void)fprintf(stderr, "[client] write: %s\n", errbuf);
        (void)close(sfd); // Închidere socket 
        return -1; // Eroare 
    }

    // Buffer răspuns server 
    char res_buf[PROTO_BUF_SIZE]; // Buffer pentru răspuns 

    // Citire răspuns 
    if (read_line(sfd, res_buf, sizeof(res_buf)) <= 0) {
        (void)fprintf(stderr, "[client] read response failed\n"); // Eroare read 
        (void)close(sfd); // Cleanup 
        return -1; // Fail 
    }

    // Închidere socket 
    if (close(sfd) == -1) {
        (void)strerror_r(errno, errbuf, sizeof(errbuf)); // Eroare close 
        (void)fprintf(stderr, "[client] close: %s\n", errbuf);
    }

    // Parsare răspuns 
    ProtoResponse res = {0}; // Struct răspuns 
    if (proto_parse_response(res_buf, &res) != 0) {
        (void)fprintf(stderr,
                      "[client] malformed response: %s\n", res_buf); // Format invalid 
        return -1; // Eroare parsing 
    }

    // Returnăm rezultatul final 
    *no_motion_out = res.no_motion_count; // Salvăm output-ul 

    return 0; // Succes 
}