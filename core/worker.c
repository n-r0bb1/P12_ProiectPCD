#include "worker.h"//Include fisierul header ce contine declaratiile necesare
                       //Acesta furnizeaza prototipul functiei si dependentele asociate
#include "frame_splitter.h"//Include modulul pentru impartirea video-ului in chunk-uri
                            //Fiecare chunk reprezinta o portiune de procesat in paralel
#include "motion_detection.h"//Include functia de detectie a miscarii pe frame-uri
                              //Aceasta este functia executata de fiecare worker
#include <stdio.h>//Include functii de input/output standard (fprintf, etc.)
#include <string.h>//Include functii pentru manipulare stringuri si erori
#include <stdlib.h>//Include functii standard de utilitate generala
#include <unistd.h>//Include apeluri POSIX (fork, pipe, read, write, close)
#include <sys/wait.h>//Include waitpid pentru sincronizarea proceselor copil
#include <errno.h>//Include variabila errno pentru erori sistem

#define WORKER_SLOT_MAX 64//Numarul maxim de workeri simultani permisi
                           //Previne suprautilizarea resurselor sistemului

#define ERRBUF_SIZE     64//Dimensiunea bufferului pentru mesaje de eroare
                           //Folosit la conversia errno in text

// Tipuri distincte pentru file descriptor (evită swap accidental de parametri) 
typedef int PipeReadFd;//Alias pentru descriptor de citire pipe
                       //Ajuta la claritatea codului

typedef int PipeWriteFd;//Alias pentru descriptor de scriere pipe
                        //Previne inversarea accidentala a parametrilor

// Structură care ține evidența unui worker activ 
typedef struct {
    pid_t      pid; //NOLINT //PID-ul procesului copil (worker)
                           //Folosit pentru waitpid si identificare proces
    PipeReadFd read_fd;//Descriptorul de citire al pipe-ului asociat workerului
                       //Prin acesta se primesc rezultatele
} WorkerSlot;

// Colectează rezultatul unui worker terminat și eliberează slotul 
static void slot_collect(WorkerSlot *slots, size_t slot_idx,size_t *active, long *total)
{
    char errbuf[ERRBUF_SIZE];//Buffer local pentru mesaje de eroare
                            //Utilizat la strerror_r

    long partial = 0L;//Rezultatul partial primit de la worker
                       //Initializat cu 0 pentru siguranta

    // Citește rezultatul din pipe 
    ssize_t nread = read(slots[slot_idx].read_fd,&partial,sizeof(partial));//Citeste rezultatul trimis de copil, blocant pana la primirea datelor

                                                   
    // Dacă citirea este validă, adaugă la total 
    if (nread == (ssize_t)sizeof(partial)) {//Verificam ca am primit exact un long complet
        *total += partial;//Adaugam rezultatul la suma globala
    }

    // Închide descriptorul de citire 
    if (close(slots[slot_idx].read_fd) == -1) {//Eliberam resursa pipe
        (void)strerror_r(errno, errbuf, sizeof(errbuf));//Convertim eroarea in text
        (void)fprintf(stderr, "[worker] close read_fd: %s\n", errbuf);//Afisam eroarea
    }

    // Compactare array (mută ultimul element în locul celui eliberat) 
    slots[slot_idx] = slots[*active - 1];//Mutam ultimul worker in slotul gol
                                         //Evitam spatii libere in array

    --(*active);//Scadem numarul de workeri activi
}

// Așteaptă terminarea unui proces copil și colectează rezultatul 
static void wait_one(WorkerSlot *slots, size_t *active, long *total)
{
    char errbuf[ERRBUF_SIZE];//Buffer pentru erori waitpid
    int  status = 0;//Status terminare proces copil

    // Așteaptă orice copil 
    pid_t ret = waitpid(-1, &status, 0);//Asteapta terminarea oricarui proces copil

    if (ret <= 0) {//Verificare eroare waitpid
        (void)strerror_r(errno, errbuf, sizeof(errbuf));//Conversie eroare
        (void)fprintf(stderr, "[worker] waitpid: %s\n", errbuf);//Afisare eroare
        return;//Iesire din functie in caz de eroare
    }

    // Identifică slotul corespunzător PID-ului terminat 
    for (size_t si = 0; si < *active; ++si) {//Parcurgem lista de workeri activi
        if (slots[si].pid == ret) {//Cautam workerul care a terminat
            slot_collect(slots, si, active, total);//Colectam rezultatul lui
            return;//Iesim dupa gasire
        }
    }
}

// Așteaptă și colectează toate procesele rămase 
static void drain_all(WorkerSlot *slots, size_t *active, long *total)
{
    while (*active > 0) {//Cat timp mai exista workeri activi
        wait_one(slots, active, total);//Asteptam si colectam unul cate unul
    }
}

// Execuție worker în proces copil 
static void child_run(const char *video_path, long start_frame, long end_frame, int threshold, PipeWriteFd write_fd)
{
    char errbuf[ERRBUF_SIZE];//Buffer erori pentru child
    // Rulează detecția de mișcare pe chunk-ul asignat 
    long result = motion_detect_chunk(video_path, start_frame, end_frame, threshold);//Procesare efectiva a segmentului video
    // Trimite rezultatul înapoi prin pipe 
    ssize_t written = write(write_fd, &result, sizeof(result));//Scriem rezultatul in pipe
    if (written != (ssize_t)sizeof(result))//Verificam scriere completa 
    {
           (void)fprintf(stderr, "[worker] write pipe failed\n");//Eroare scriere
    }

    // Închidere pipe write 
    if (close(write_fd) == -1) {//Inchidem capatul de scriere
        (void)strerror_r(errno, errbuf, sizeof(errbuf));//Conversie eroare
        (void)fprintf(stderr, "[worker] close write_fd: %s\n", errbuf);//Afisare eroare
    }

    _Exit(0);//Iesire imediata din procesul copil
}

// Rulează toate chunk-urile în paralel folosind fork + limitare max_workers 
long workers_run(const char *video_path,const FrameChunk *chunks, size_t n_chunks, int threshold, int max_workers)
{
    // Validare input 
    if (!video_path || !chunks || n_chunks == 0 || max_workers <= 0) {//Verificare parametri
        return -1L;//Returnam eroare la input invalid
    }

    // Limitare număr maxim de workeri 
    if (max_workers > WORKER_SLOT_MAX) {//Clamping pe limita maxima
        max_workers = WORKER_SLOT_MAX;//Protejare resurse sistem
    }

    char errbuf[ERRBUF_SIZE];//Buffer erori general
    WorkerSlot slots[WORKER_SLOT_MAX];//Array static pentru workeri

    size_t active = 0;//Numar curent de workeri activi
    long   total  = 0L;//Rezultat final agregat

    // Parcurgere toate chunk-urile 
    for (size_t ci = 0; ci < n_chunks; ++ci) {//Iteram prin toate segmentele video

        // Dacă s-au atins workerii maximi, așteaptă unul să termine 
        while (active >= (size_t)max_workers) {//Control concurenta
            wait_one(slots, &active, &total);//Eliberam slot
        }

        // Creare pipe pentru comunicare părinte–copil 
        int pipefd[2];//Pipe bidirectional unidirectional

        if (pipe(pipefd) == -1) {//Creare pipe sistem
            (void)strerror_r(errno, errbuf, sizeof(errbuf));//Eroare pipe
            (void)fprintf(stderr, "[worker] pipe: %s\n", errbuf);//Afisare

            drain_all(slots, &active, &total);//Cleanup workeri
            return -1L;//Eroare
        }

        // Fork proces worker 
        pid_t pid = fork();//Creare proces copil

        if (pid == -1) {//Verificare eroare fork
            (void)strerror_r(errno, errbuf, sizeof(errbuf));//Eroare fork
            (void)fprintf(stderr, "[worker] fork: %s\n", errbuf);//Afisare

            (void)close(pipefd[0]);//Inchidere capete pipe
            (void)close(pipefd[1]);

            drain_all(slots, &active, &total);//Cleanup
            return -1L;//Eroare
        }

        // === PROCES COPIL === 
        if (pid == 0) {//Cod executat doar in copil

            if (close(pipefd[0]) == -1) {//Copilul nu citeste
                _Exit(1);//Abort rapid
            }

            child_run(video_path,
                      chunks[ci].start_frame,
                      chunks[ci].end_frame,
                      threshold,
                      pipefd[1]);//Executie worker

            _Exit(1);//Fallback siguranta
        }

        // === PROCES PĂRINTE === 

        if (close(pipefd[1]) == -1) {//Parintele nu scrie
            (void)strerror_r(errno, errbuf, sizeof(errbuf));//Eroare close
            (void)fprintf(stderr, "[worker] close write end: %s\n", errbuf);//Afisare
        }

        slots[active].pid     = pid;//Salvare PID worker
        slots[active].read_fd = pipefd[0];//Salvare fd citire

        ++active;//Incrementare workeri activi
    }

    // Așteaptă terminarea tuturor workerilor 
    drain_all(slots, &active, &total);//Colectare finala

    return total;//Returnam suma finala
}