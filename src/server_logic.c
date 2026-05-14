// server_logic.c – handler per client: primeste request, proceseazs, trimite raspuns 
#include "../include/server.h"      //Header principal al serverului, contine declaratii pentru handler si configurari
#include "../protocol/proto.h"      //Protocol de comunicare client-server (request/response + serializare/parsing)
#include "../core/video_io.h"       //Functii pentru lucrul cu fisiere video (ex: numarare frame-uri)
#include "../core/frame_splitter.h" //Logica de impartire a video-ului in chunk-uri pentru procesare paralela
#include "../core/worker.h"         //Logica de procesare paralela (workers / fork / thread)

#include <stdio.h>   //I/O standard (printf, fprintf)
#include <string.h>  //Operatii pe stringuri (strlen, etc.)
#include <unistd.h>  //read, write (operatii POSIX pe socket-uri)
#include <errno.h>   //coduri de eroare sistem

#define ERRBUF_SIZE 64 //Dimensiunea bufferului folosit pentru mesaje de eroare formatate

// Tip alias pentru file descriptor client (claritate + evitarea swap-urilor gresite) 
typedef int ClientFd; //Definim un alias pentru int pentru a clarifica faptul ca este socket client

// Citește o linie terminată cu '\n' de pe socket 
static ssize_t read_line(ClientFd sock_fd, char *buf, size_t bufsz)
{
    size_t  pos = 0; //pozitia curenta in buffer (unde scriem datele citite)
    ssize_t nr  = 0; //numarul de bytes cititi la fiecare apel read
    char    ch  = '\0'; //variabila temporara pentru caracterul citit

    // Citește byte cu byte până la newline sau buffer plin 
    while (pos + 1 < bufsz) { //ne asiguram ca lasam loc pentru '\0'
        nr = read(sock_fd, &ch, 1); //citire unui singur byte din socket

        if (nr == 0) { // conexiune inchisa de client
            break; //iesim din bucla daca clientul a inchis conexiunea
        }
        if (nr < 0) { // eroare la citire
            return -1; //intoarcem eroare catre apelant
        }

        buf[pos++] = ch; //stocam caracterul citit in buffer

        if (ch == '\n') { // sfarsit de mesaj
            break; //oprim citirea la newline
        }
    }

    buf[pos] = '\0'; //terminam stringul in mod sigur (null-terminated)
    return (ssize_t)pos; //returnam numarul de caractere citite
}

// Handler principal pentru un client conectat 
void server_handle_client(ClientFd client_fd, int max_workers)
{
    char errbuf[ERRBUF_SIZE]; //buffer pentru mesajele de eroare formatate (strerror_r)

    //Citire request de la client --- 
    char req_buf[SERVER_BUF_SIZE]; //buffer pentru request-ul primit de la client

    // Citește mesajul complet trimis de client 
    ssize_t nr = read_line(client_fd, req_buf, sizeof(req_buf)); //citire request din socket
    if (nr <= 0) { //daca nu s-a citit nimic sau a aparut eroare
        (void)fprintf(stderr, "[server] failed to read request\n"); //log eroare
        return; //iesire din handler (client invalid sau deconectat)
    }

    // Parsează request-ul în structură internă 
    ProtoRequest req = {"", 0, 0}; //structura in care vom salva datele parsate din request
    if (proto_parse_request(req_buf, &req) != 0) { //incercam sa interpretam request-ul
        (void)fprintf(stderr, "[server] malformed request: %s\n", req_buf); //log request invalid
        return; //iesire daca formatul este gresit
    }

    // Log request primit 
    (void)fprintf(stdout,"[server] REQ video=%s chunk=%d threshold=%d\n",req.video_path, req.chunk_size, req.threshold); //afisam detalii despre request pentru debug

    //Obținere număr total de frame-uri din video --- 
    long total_frames = video_get_frame_count(req.video_path); //obtinem numarul total de frame-uri din video
    if (total_frames <= 0) { //verificam daca video-ul este valid
        (void)fprintf(stderr, "[server] cannot get frame count for %s\n",
                      req.video_path); //log eroare la citire video
        return; //iesire daca video-ul nu poate fi procesat
    }

    //Împărțire video în bucăți (chunks) --- 
    FrameChunk chunks[SPLITTER_MAX_CHUNKS]; //vector pentru chunk-urile rezultate din impartire
    size_t     n_chunks = 0; //numarul de chunk-uri generate

    if (frame_splitter_split(total_frames, req.chunk_size,
                             chunks, &n_chunks) != 0) { //impartim frame-urile in bucati
        (void)fprintf(stderr, "[server] frame split failed\n"); //log eroare
        return; //iesire daca impartirea esueaza
    }

    //Execuție workers (procesare paralelă / fork-based) --- 
    long no_motion = workers_run(req.video_path,chunks, n_chunks,req.threshold,max_workers); //rulam workers pentru procesare paralela a chunk-urilor

    // Verificare rezultat workers 
    if (no_motion < 0) { //verificam daca procesarea a esuat
        (void)fprintf(stderr, "[server] workers failed\n"); //log eroare workers
        return; //iesire daca procesarea nu a reusit
    }

    // Log rezultat final 
    (void)fprintf(stdout, "[server] no_motion_frames=%ld\n", no_motion); //afisam rezultatul final obtinut

    //Construire și trimitere răspuns către client --- 

    // Serializare rezultat în format protocol 
    char res_buf[SERVER_BUF_SIZE]; //buffer pentru raspunsul serializat
    ProtoResponse res = {no_motion}; //structura de raspuns care contine rezultatul

    if (proto_serialize_response(&res, res_buf, sizeof(res_buf)) != 0) { //convertim structura in format text/network
        (void)fprintf(stderr, "[server] serialize response failed\n"); //log eroare serializare
        return; //iesire daca serializarea esueaza
    }

    // Trimitere răspuns prin socket 
    size_t  to_send = strlen(res_buf); //calculam dimensiunea raspunsului de trimis
    ssize_t sent    = write(client_fd, res_buf, to_send); //trimitem efectiv datele catre client

    // Verificare trimitere completă 
    if (sent != (ssize_t)to_send) { //verificam daca s-au trimis toti bytes
        (void)strerror_r(errno, errbuf, sizeof(errbuf)); //convertim eroarea sistem in mesaj text
        (void)fprintf(stderr, "[server] write response failed: %s\n", errbuf); //log eroare trimitere
    }
}