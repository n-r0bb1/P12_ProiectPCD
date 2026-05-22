#ifndef VIDEO_IO_H
#define VIDEO_IO_H

#ifdef __cplusplus
extern "C" {
#endif

//Returnează numărul total de cadre pentru videoclipul de pe cale sau -1 în caz de eroare
long video_get_frame_count(const char *path);

//Functie care returneaza numarul total de frames
long video_get_frame_count_impl(const char *path);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VIDEO_IO_H */