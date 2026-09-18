#pragma once
#ifndef QR_WINDOW_H
#define QR_WINDOW_H

#include <windows.h>
#include <stdbool.h>

#define WINDOW_W   560
#define WINDOW_H   640
#define QR_AREA    440

typedef struct {
    int         fps;
    int         slice_size;
    int         ecc;        /* 0=Low, 1=Med, 2=Quartile, 3=High */
    const char *prefix;
} QifiSettings;

/* Launch the QR display window. Blocks until window is closed. */
bool qr_window_run(const char *filepath, const QifiSettings *settings);

#endif
