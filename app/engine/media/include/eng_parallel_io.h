/*
 * eng_parallel_io.h - PS5VR: parallel read-ahead for big network files.
 */
#ifndef ENG_PARALLEL_IO_H
#define ENG_PARALLEL_IO_H

#include <libavformat/avio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct eng_pio eng_pio;

/* NULL when the URL is not a big file served with byte ranges (then FFmpeg's
 * own single connection is used as before). */
eng_pio *eng_pio_open(const char *url, const char *headers, const char *user_agent);
AVIOContext *eng_pio_avio(eng_pio *p);
/* Fail the reads that are waiting (playback is being stopped). */
void eng_pio_abort(eng_pio *p);
/* After avformat_close_input(). */
void eng_pio_close(eng_pio *p);

#ifdef __cplusplus
}
#endif

#endif
