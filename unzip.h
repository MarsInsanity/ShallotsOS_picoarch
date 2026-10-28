#ifndef __UNZIP_H_
#define __UNZIP_H_

#include <stdbool.h>
#include <stdio.h>

/* Directory for unpacked/patched content: on the SD card when possible. */
const char *unzip_tmp_dir(void);
/* Unpack the ROM inside `zip` to unzip_tmp_dir()/pa-<name>. `stamp`
 * identifies the zip (path, size, mtime): when the file left by a previous
 * launch has the same stamp it is reused (*reused set) instead of unpacked
 * again. Only the most recent unpacked ROM is kept. */
int unzip_tmp(FILE *zip, const char **extensions, const char *stamp,
              char *filename, size_t len, bool *reused);
int unzip(FILE *zip, const char **extensions, FILE *dest);

#endif
