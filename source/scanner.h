#ifndef CT_SCANNER_H
#define CT_SCANNER_H

/* Barcode scanning with the outer camera: shows a live picture and reads
 * EAN-13 / UPC-A / EAN-8 codes from it. */

#include <citro2d.h>
#include <stdbool.h>

#include "core/barcode.h"

int scanner_start(void);
void scanner_stop(void);
bool scanner_running(void);
/* Waits for the next camera frame (up to ~1/15 s), updates the preview and
 * tries to read a code. Returns 1 with the digits when one is found. */
int scanner_frame(char out[BARCODE_LEN]);
/* The latest picture, 400 x 240, for the top screen. */
C2D_Image scanner_image(void);

#endif
