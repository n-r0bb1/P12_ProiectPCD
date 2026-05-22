#include "../include/sclient.h" //Include header-ul clientului, contine declaratiile functiilor de retea

#include <stdio.h> //NOLINT -> librarie pentru input/output standard (printf, fprintf)
#include <stdlib.h> //librarie pentru conversii, exit codes si functii generale
#include <string.h> //NOLINT -> librarie pentru operatii pe stringuri (nu este folosit direct aici, dar inclus)
#include <unistd.h> //NOLINT -> librarie pentru getopt si apeluri POSIX
#include <limits.h> //defineste limitele tipurilor (ex: INT_MAX)

#define USAGE_MSG \
    "Usage: ./client -v <video_path> -c <chunk_size> -t <threshold>\n" //Mesaj standard afisat la utilizare incorecta a programului
#define PATH_BUF_MAX 512 //Dimensiunea maxima a bufferului pentru calea fisierului video

/* Funcție pentru afișarea modului de utilizare */
static void print_usage(void)
{
    (void)fprintf(stderr, USAGE_MSG); //Afiseaza mesajul de utilizare pe stderr (flux de erori)
                                     //Se face cast la void pentru a ignora return value-ul
}

int main(int argc, char *argv[]) //Functia principala a programului client
                                 //Primeste argumentele din linia de comanda
{
    char video_path[PATH_BUF_MAX]; //Buffer pentru stocarea caii fisierului video
    video_path[0] = '\0'; //Initializam sirul ca fiind gol (marcare stare necompletata)

    int chunk_size = 0;   //dimensiunea fragmentelor procesate pe care le trimitem serverului
    int threshold  = -1;  //pragul de detectie (valoare minima pentru decizie)

    int opt = 0; //variabila folosita de getopt pentru optiunea curenta

    // Parsare argumente din linia de comandă
    // getopt folosește stare globală -> NOLINT
    while ((opt = getopt(argc, argv, "v:c:t:")) != -1) { /* NOLINT */
        //Parcurgem toate argumentele primite si extragem optiunile -v, -c, -t

        switch (opt) { //Selectam comportamentul in functie de optiunea citita

        //-v <video_path> 
        case 'v':
            // Copiere sigură a stringului video_path 
            if (snprintf(video_path, sizeof(video_path),
                         "%s", optarg /* NOLINT(misc-include-cleaner) */) < 0) {
                //Copiaza sigur calea video in bufferul local folosind snprintf
                //Previne overflow-ul de buffer

                (void)fprintf(stderr, "[client] video path too long\n"); //Afiseaza eroare daca path-ul este prea mare
                return 1; //Oprire executie cu cod de eroare
            }
            break;

        // -c <chunk_size>
        case 'c': {
            char *endptr = NULL; //pointer folosit pentru validarea conversiei string → numar

            /* Conversie string → număr */
            long val = strtol(optarg /* NOLINT */, &endptr, 10);
            //Convertim argumentul primit la numar intreg (baza 10)

            // Validare: trebuie să fie număr pozitiv
            if (endptr == optarg /* NOLINT */ || *endptr != '\0'
                || val <= 0 || val > INT_MAX) {
                //Verificam:
                // daca nu s-a facut conversia
                // daca exista caractere invalide
                // daca valoarea nu este pozitiva
                // daca depaseste limita unui int

                (void)fprintf(stderr,
                              "[client] -c must be a positive integer\n"); //Eroare pentru chunk_size invalid
                print_usage(); //Afisam instructiunile corecte de utilizare
                return 1; //Oprire executie
            }

            chunk_size = (int)val; //Cast la int dupa validare si stocare valoare
            break;
        }

        // -t <threshold>
        case 't': {
            char *endptr = NULL; //pointer pentru verificarea conversiei

            // Conversie string → număr
            long val = strtol(optarg /*NOLINT*/, &endptr, 10);
            //Conversie threshold din string in numar intreg

            //Validare: număr întreg >= 0 
            if (endptr == optarg /* NOLINT */ || *endptr != '\0'
                || val < 0 || val > INT_MAX) {
                //Verificam:
                // - conversie valida
                // - fara caractere extra
                // - valoare nenegativa
                // - incadrare in limitele int

                (void)fprintf(stderr,"[client] -t must be a non-negative integer\n"); //Eroare pentru threshold invalid
                print_usage(); //Afisam modul corect de utilizare
                return 1; //Oprire executie
            }

            threshold = (int)val; //Salvam valoarea validata
            break;
        }

        //Argument necunoscut 
        default:
            print_usage(); //Daca optiunea nu este recunoscuta, afisam usage
            return 1; //Oprire program
        }
    }

    // Verificare că toate argumentele obligatorii au fost primite 
    if (video_path[0] == '\0' || chunk_size == 0 || threshold < 0) {
        //Verificam daca lipseste vreun argument necesar:
        // - path necompletat
        // - chunk_size invalid
        // - threshold invalid

        (void)fprintf(stderr, "[client] all arguments are mandatory\n"); //Eroare generala pentru lipsa argumente
        print_usage(); //Afisam instructiuni
        return 1; //Oprire executie
    }

    // Apel către logica de client TCP 
    long no_motion = 0L; //variabila unde va fi stocat rezultatul primit de la server

    if (client_run(CLIENT_SERVER_HOST, CLIENT_SERVER_PORT,
                   video_path, chunk_size, threshold,
                   &no_motion) != 0) {
        //Apelam functia principala de comunicare cu serverul TCP
        //Trimitem:
        // - host
        // - port
        // - path video
        // - chunk size
        // - threshold
        // - pointer pentru rezultat

        (void)fprintf(stderr, "[client] request failed\n"); //Eroare daca comunicarea esueaza
        return 1; //Oprire executie
    }

    // Afișarea rezultatului primit de la server
    (void)printf("RES|%ld\n", no_motion); //Afisam rezultatul final primit de la server

    return 0; //Executie terminata cu succes
}