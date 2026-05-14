/* video_io_cv.c – OpenCV VideoCapture query pentru numarul de frame-uri (C API) */
#include "video_io.h"
#include <stdio.h>

#include <opencv2/videoio/videoio_c.h> //NOLINT(no-misc-cleaner)

long video_get_frame_count_impl(const char *path) //Returneaza numarul total de frame-uri dintr-un video
{
    CvCapture *cap = NULL; //Handle-ul capturii video OpenCV
    double fc;             //Numarul de frame-uri citit din metadata video

    if (path == NULL) { //Validare input – pointer invalid
        return -1L;
    }

    cap = cvCaptureFromFile(path); //Deschidere fisier video cu OpenCV C API

    if (cap == NULL) { //Verificare daca video-ul a fost deschis corect
        (void)fprintf(stderr, "[video_io] cannot open: %s\n", path); //Eroare la deschiderea fisierului
        return -1L;
    }

    fc = cvGetCaptureProperty(cap, CV_CAP_PROP_FRAME_COUNT); //Citire metadata: numarul total de frame-uri

    cvReleaseCapture(&cap); //Eliberare resurse dupa citirea metadatei

    if (fc < 0.0) { //Validare rezultat – valoare negativa indica eroare
        return -1L;
    }

    return (long)fc; //Conversie la long si returnare
}