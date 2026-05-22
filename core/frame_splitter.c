#include "frame_splitter.h"//Header-ul propriu al modulului frame_splitter
                           //Contine definitiile pentru FrameChunk, constante si prototipul functiei

#include <stddef.h>//Librarie standard C folosita pentru tipul size_t

int frame_splitter_split(long total_frames, int chunk_size, FrameChunk *chunks, size_t *n_chunks)
{
    size_t count = 0;//Variabila folosita pentru a tine evidenta numarului de chunks create
                    //Aceasta creste cu 1 la fiecare chunk generat si la final reprezinta rezultatul functiei
    long start = 0L;//Variabila care reprezinta frame-ul de inceput pentru chunk-ul curent
                    //Initial pornim de la frame-ul 0, adica inceputul videoclipului
    if (total_frames <= 0 || chunk_size <= 0 || !chunks || !n_chunks)//Verificam validitatea tuturor parametrilor de intrare
                                                                      //total_frames trebuie sa fie un numar pozitiv de frame-uri
                                                                      //chunk_size trebuie sa fie un numar pozitiv diferit de 0
                                                                      //chunks trebuie sa pointeze catre un vector valid unde vom stoca rezultatele
                                                                      //n_chunks trebuie sa fie un pointer valid unde vom scrie rezultatul final
    {
        return -1;//Returnam eroare deoarece nu putem continua procesarea cu date invalide
    }

    while (start < total_frames)//Parcurgem toate frame-urile videoclipului pana cand ajungem la final
                                //Fiecare iteratie produce un nou chunk
    {
        if (count >= SPLITTER_MAX_CHUNKS)//Verificam daca am depasit numarul maxim de chunks permis de sistem
                                         //Aceasta este o masura de siguranta pentru a evita scrierea in afara memoriei
        {
            return -1;//Oprim executia si returnam eroare pentru a preveni coruperea datelor
        }

        long end = start + (long)chunk_size;//Calculam frame-ul de final al chunk-ului curent
                                           //Practic adaugam dimensiunea chunk-ului la pozitia de start

        if (end > total_frames)//Daca intervalul calculat depaseste numarul total de frame-uri disponibile
        {
            end = total_frames;//ajustam finalul astfel incat ultimul chunk sa se termine exact la finalul videoclipului
                               //acest lucru evita referintele in afara limitelor
        }

        chunks[count].start_frame = start;//Salvam frame-ul de inceput in structura chunk-ului curent
                                         //Acesta marcheaza unde incepe segmentul video

        chunks[count].end_frame   = end;//Salvam frame-ul de final in structura chunk-ului curent
                                        //Acesta marcheaza unde se termina segmentul video

        ++count;//Incrementam numarul total de chunks generate pana in acest moment
                //Acest contor este folosit si ca index in vectorul chunks

        start = end;//Mutam punctul de start pentru urmatorul chunk la finalul celui curent
                   //Astfel asiguram ca segmentele sunt consecutive si nu se suprapun
    }

    *n_chunks = count;//Scriem rezultatul final in variabila primita prin pointer
                     //Aceasta indica numarul total de chunks generate de functie

    return 0;//Returnam 0 pentru a semnala ca operatia s-a executat cu succes fara erori
}