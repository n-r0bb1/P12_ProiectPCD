#ifndef WORKER_H
#define WORKER_H

#include "frame_splitter.h"

//Creaza max_workers procese copil
//Fiecare proces scrie partea sa de non-miscare 
//Processul parinte colecteaza rezultatele
//Returneaza suma totala de frames fara miscare, sau -1 eroare

long workers_run(const char *video_path, const FrameChunk *chunks, size_t n_chunks,int threshold,int max_workers);

#endif // WORKER_H
