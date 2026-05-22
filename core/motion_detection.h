#ifndef MOTION_DETECTION_H
#define MOTION_DETECTION_H

#ifdef __cplusplus
extern "C" {
#endif

long motion_detect_chunk(const char *video_path, long start_frame,
                         long end_frame, int threshold);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MOTION_DETECTION_H */