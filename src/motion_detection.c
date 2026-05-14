#include "motion_detection.h"
#include <stdio.h>
#include <opencv2/core/core_c.h> //NOLINT(no-misc-cleaner)
#include <opencv2/imgproc/imgproc_c.h>
#include <opencv2/videoio/videoio_c.h>

long motion_detect_chunk(const char *video_path, long start_frame,long end_frame,int threshold) // Detecteaza numarul de frame-uri fara, miscare din chunk-ul dat
{
  IplImage *prev_gray = NULL; // Frame-ul anterior convertit la grayscale
  IplImage *curr_gray = NULL; // Frame-ul curent convertit la grayscale
  IplImage *diff = NULL;      // Imaginea diferenta dintre frame-uri consecutive
  IplImage *curr_frame =
      NULL;              // Frame-ul curent citit din video (owned by capture)
  CvCapture *cap = NULL; // Handle-ul capturii video OpenCV
  long no_motion_count = 0L; // Contor pentru frame-urile fara miscare detectata
  long idx;                  // Index curent de frame in bucla de procesare
  double max_val; // Valoarea maxima a diferentei dintre doua frame-uri

  if (video_path == NULL || start_frame < 0 || end_frame <= start_frame ||
      threshold < 0) { // Validare parametri de intrare
    return -1L;        // Parametri invalizi – nu se poate continua
  }

  cap = cvCaptureFromFile(video_path); // Deschidere video cu OpenCV C API
  if (cap == NULL) {
    (void)fprintf(stderr, "[motion] cannot open video: %s\n",video_path); // Eroare la deschiderea fisierului video
    return -1L;
  }

  if (!cvSetCaptureProperty(cap,CV_CAP_PROP_POS_FRAMES,(double)start_frame)) 
  { // Mutare la frame-ul de start
    (void)fprintf(stderr, "[motion] seek failed to frame %ld\n",start_frame); // Seek esuat – pozitie invalida
    cvReleaseCapture(&cap);
    return -1L;
  }

  for (idx = start_frame; idx < end_frame;++idx) { // Parcurgere frame-uri din chunk

    curr_frame = cvQueryFrame(cap); // Citire frame curent – owned by capture, nu se elibereaza manual
    if (curr_frame == NULL) { // Final video inainte de end_frame
      break;
    }

    if (curr_gray == NULL) { // Alocare buffer grayscale la primul frame procesat
      curr_gray = cvCreateImage(cvGetSize(curr_frame), IPL_DEPTH_8U, 1); // Buffer grayscale pentru frame-ul curent
      diff = cvCreateImage(cvGetSize(curr_frame), IPL_DEPTH_8U, 1); // Buffer pentru diferenta absoluta
    }
    cvCvtColor( curr_frame, curr_gray,CV_BGR2GRAY); // Conversie la grayscale pentru comparatie simplificata

    if (prev_gray == NULL) { // Primul frame din chunk – nu exista frame anterior
      ++no_motion_count; // Se considera baseline, fara miscare
      prev_gray = cvCloneImage(curr_gray); // Clonare pentru a pastra frame-ul ca referinta
      continue;
    }

    cvAbsDiff(curr_gray, prev_gray,diff); // Diferenta absoluta dintre frame-urile consecutive
    max_val = 0.0; // Resetare valoare maxima inainte de detectie
    cvMinMaxLoc(diff, NULL, &max_val, NULL, NULL,NULL); // Detecteaza intensitatea maxima a schimbarii

    if (max_val <=(double)threshold) 
    { // Diferenta sub prag – considerat fara miscare
      ++no_motion_count;
    }

    cvCopy(curr_gray, prev_gray,NULL); // Actualizare frame anterior pentru urmatoarea iteratie
  }

  if (prev_gray != NULL) {
    cvReleaseImage(&prev_gray);
  } // Eliberare buffer frame anterior
  if (curr_gray != NULL) {
    cvReleaseImage(&curr_gray);
  } // Eliberare buffer frame curent
  if (diff != NULL) {
    cvReleaseImage(&diff);
  }                       // Eliberare buffer diferenta
  cvReleaseCapture(&cap); // Eliberare handle captura video

  return no_motion_count; // Returneaza numarul total de frame-uri fara miscaredin chunk
        
}