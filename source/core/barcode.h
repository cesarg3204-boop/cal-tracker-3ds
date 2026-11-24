#ifndef CT_BARCODE_H
#define CT_BARCODE_H

/* A small reader for the barcodes on food packages: EAN-13, UPC-A (read as
 * EAN-13 with a leading 0) and EAN-8. Works on a grayscale camera picture. */

#define BARCODE_LEN 14 /* up to 13 digits and the NUL */

/* Reads one horizontal line of brightness values (0 = black). Returns 1 and
 * writes the digits when a barcode with a valid check digit is found. */
int barcode_read_row(const unsigned char *row, int width, char out[BARCODE_LEN]);

/* Tries several lines around the middle of a grayscale picture (one byte
 * per pixel, `stride` bytes per line). Needs two lines to agree. */
int barcode_read_image(const unsigned char *gray, int width, int height, int stride, char out[BARCODE_LEN]);

/* The check digit rule shared by EAN-13, UPC-A and EAN-8. */
int barcode_check_ok(const char *digits);

#endif
