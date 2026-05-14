#ifndef FRAME_SPLITTER_H
#define FRAME_SPLITTER_H

#include <stddef.h>

#define SPLITTER_MAX_CHUNKS 64

typedef struct {
    long start_frame;
    long end_frame;   
} FrameChunk;

 //Imparte numarul total de frames 
 //Seteaza numarul de n_chunks
 //Returneaza 0 pt succes, -1 pt param gresiti
int frame_splitter_split(long total_frames, int chunk_size, FrameChunk *chunks,size_t *n_chunks);

#endif // FRAME_SPLITTER_H 
