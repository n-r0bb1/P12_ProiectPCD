#ifndef MOTION_DETECTION_H
#define MOTION_DETECTION_H

 // Un frame nu are miscare daca are nu sunt diferente de pixeli intre 2 frames, mai mari de 255
 // Returneaza suma (count) de frames fara miscare, -1 eroare
long motion_detect_chunk(const char *video_path, long start_frame,long end_frame,int threshold);

#endif // MOTION_DETECTION_H 