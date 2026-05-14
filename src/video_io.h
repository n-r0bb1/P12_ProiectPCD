#ifndef VIDEO_IO_H
#define VIDEO_IO_H

//Returnează numărul total de cadre pentru videoclipul de pe cale sau -1 în caz de eroare
long video_get_frame_count(const char *path);

//Functie care returneaza numarul total de frames
long video_get_frame_count_impl(const char *path);

#endif // VIDEO_IO_H 