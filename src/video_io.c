// video_io.c – wrapper pentru obtinerea metadatelor video (OpenCV) 
#include "video_io.h"//Include header-ul modulului video_io
                     //Acesta contine declaratia functiei si dependintele necesare

// Funcție wrapper care apelează implementarea OpenCV 
long video_get_frame_count(const char *path)//Functie care returneaza numarul de frame-uri dintr-un video
                                           //Primeste ca parametru calea catre fisierul video
{
    // Validare simpla a input-ului 
    if (!path) {//Verificam daca pointerul catre calea video este invalid (NULL)
        return -1L;//Returnam eroare deoarece nu putem procesa un path invalid
    }

    // Delegare catre implementarea reală (OpenCV backend) 
    return video_get_frame_count_impl(path);//Apelam functia reala care foloseste OpenCV
                                            //Aceasta returneaza numarul efectiv de frame-uri din video
}