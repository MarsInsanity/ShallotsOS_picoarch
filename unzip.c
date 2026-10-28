#include <dirent.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "main.h"
#include "unzip.h"
#include "util.h"
#include "zlib.h"

#define HEADER_SIZE 30
#define CHUNK 65536

#define LE_READ16(buf) ((uint16_t)(((uint8_t *)(buf))[1] << 8 | ((uint8_t *)(buf))[0]))
#define LE_READ32(buf) ((uint32_t)(((uint8_t *)(buf))[3] << 24 | ((uint8_t *)(buf))[2] << 16 | ((uint8_t *)(buf))[1] << 8 | ((uint8_t *)(buf))[0]))

struct file_info {
	uint32_t compressed_size;
	int (*inflate)(FILE *zip, FILE *dest, size_t size);
	char filename[MAX_PATH];
};

static int write_uncompressed(FILE *zip, FILE *dest, size_t size) {
	uint8_t buf[CHUNK];

	while (size) {
		size_t wsize = MIN(size, CHUNK);

		if (size != fread(buf, 1, wsize, zip)) {
			return -1;
		}
		if (size != fwrite(buf, 1, wsize, zip)) {
			return -1;
		}
		size -= wsize;
	}
	return 0;
}

static int write_inflate(FILE *zip, FILE *dest, size_t size) {
	z_stream stream = {0};
	size_t have = 0;
	uint8_t in[CHUNK];
	uint8_t out[CHUNK];
	int ret = -1;

	ret = inflateInit2(&stream, -MAX_WBITS);
	if (ret != Z_OK)
		return ret;

	do {
		size_t insize = MIN(size, CHUNK);

		stream.avail_in = fread(in, 1, insize, zip);
		if (ferror(zip)) {
			(void)inflateEnd(&stream);
			return Z_ERRNO;
		}

		if (!stream.avail_in)
			break;
		stream.next_in = in;

		do {
			stream.avail_out = CHUNK;
			stream.next_out = out;

			ret = inflate(&stream, Z_NO_FLUSH);
			switch(ret) {
			case Z_NEED_DICT:
				ret = Z_DATA_ERROR;
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
				(void)inflateEnd(&stream);
				return ret;
			}

			have = CHUNK - stream.avail_out;
			if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
				(void)inflateEnd(&stream);
				return Z_ERRNO;
			}
		} while (stream.avail_out == 0);

		size -= insize;
	} while (size && ret != Z_STREAM_END);

	(void)inflateEnd(&stream);

	if (!size || ret == Z_STREAM_END) {
		return Z_OK;
	} else {
		return Z_DATA_ERROR;
	}
}

static int find_entry(FILE *zip, const char **extensions, struct file_info *info) {
	int ret = -1;
	uint8_t header[HEADER_SIZE];
	uint32_t next = 0;
	uint16_t file_name_size = 0;
	char extension[10];

	while(1) {
		bool match = false;

		if (next)
			fseek(zip, next, SEEK_CUR);

		if (HEADER_SIZE != fread(header, 1, HEADER_SIZE, zip))
			break;

		if (header[0] != 0x50 || header[1] != 0x4b || header[2] != 0x03 || header[3] != 0x04)
			break;

		if ((uint16_t)(header[6]) & 0x0008) {
			PA_ERROR("Unsupported zip file\n");
			break;
		}

		file_name_size = LE_READ16(&header[26]);
		if (file_name_size >= MAX_PATH)
			break;

		if (file_name_size != fread(info->filename, 1, file_name_size, zip))
			break;

		info->filename[file_name_size] = '\0';
		info->compressed_size = LE_READ32(&header[18]);

		fseek(zip, LE_READ16(&header[28]), SEEK_CUR);

		next = info->compressed_size;

		for (int i = 0; extensions[i]; i++) {
			snprintf(extension, 10, ".%s", extensions[i]);
			if (has_suffix_i(info->filename, extension)) {
				match = true;
				break;
			}
		}

		if (!match)
			continue;

		switch (LE_READ16(&header[8])) {
		case 0: /* Uncompressed */
			info->inflate = write_uncompressed;
		case 8: /* DEFLATE */
			info->inflate = write_inflate;
		}

		ret = 0;
		break;
	}
	return ret;
}

/* /tmp is RAM on these consoles (ramfs: never swapped, never reclaimed),
 * and a ROM unpacked there stayed in RAM next to the core's own copy; after
 * a crash it stayed until reboot and starved every later game (GTA Advance:
 * out-of-memory kills). The SD card costs no RAM. */
#ifndef UNZIP_SD_DIR
#define UNZIP_SD_DIR "/mnt/sdcard/picoarch/.unzipped"
#endif

const char *unzip_tmp_dir(void) {
	static const char *dir;
	if (!dir) {
		char parent[] = UNZIP_SD_DIR;
		mkdir(dirname(parent), 0755);
		mkdir(UNZIP_SD_DIR, 0755);
		dir = access(UNZIP_SD_DIR, W_OK) == 0 ? UNZIP_SD_DIR : "/tmp";
	}
	return dir;
}

static bool stamp_matches(const char *rom, const char *stamp) {
	char path[MAX_PATH + 8], buf[MAX_PATH + 64];
	snprintf(path, sizeof path, "%s.src", rom);
	FILE *f = fopen(path, "r");
	size_t n = 0;
	if (f) {
		n = fread(buf, 1, sizeof buf - 1, f);
		fclose(f);
	}
	buf[n] = '\0';
	return f && access(rom, R_OK) == 0 && strcmp(buf, stamp) == 0;
}

/* Keep one unpacked ROM: remove the others (a ROM is recognised by its .src
 * stamp; anything else, such as a core's in-place save, stays). */
static void remove_other_roms(const char *dir, const char *keep) {
	DIR *d = opendir(dir);
	struct dirent *e;
	char path[MAX_PATH + 8];
	if (!d)
		return;
	while ((e = readdir(d))) {
		size_t n = strlen(e->d_name);
		if (n < 8 || strncmp(e->d_name, "pa-", 3) || strcmp(e->d_name + n - 4, ".src"))
			continue;
		snprintf(path, sizeof path, "%s/%.*s", dir, (int)(n - 4), e->d_name);
		if (strcmp(path, keep)) {
			remove(path);
			snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
			remove(path);
		}
	}
	closedir(d);
}

int unzip_tmp(FILE *zip, const char **extensions, const char *stamp,
              char *filename, size_t len, bool *reused) {
	int ret = -1;
	struct file_info info = {0};
	FILE *dest = NULL;
	char stamp_path[MAX_PATH + 8] = "";

	*reused = false;
	if (!find_entry(zip, extensions, &info)) {
		/* Deterministic temp name (was mkstemps → random each launch). Cores
		 * that save in-place derive their .srm from the ROM path we hand them;
		 * a randomized name meant a zipped ROM got a different save file every
		 * run, so in-game saves couldn't be reloaded (gpsp_multicore). A stable
		 * name keyed on the inner ROM name keeps the save path constant. */
		const char *dir = unzip_tmp_dir();
		snprintf(filename, len, "%s/pa-%s", dir, basename(info.filename));
		if (strcmp(dir, "/tmp") && stamp_matches(filename, stamp)) {
			*reused = true;
			return 0;
		}
		remove_other_roms(dir, filename);
		snprintf(stamp_path, sizeof stamp_path, "%s.src", filename);
		remove(stamp_path);   /* no stamp until the ROM is complete */
		dest = fopen(filename, "w");

		if (!dest) {
			PA_ERROR("Error creating temporary file for decompression\n");
			goto finish;
		}

		if (info.inflate(zip, dest, info.compressed_size)) {
			PA_ERROR("Error decompressing file\n");
			goto finish;
		}

		ret = 0;
	}

finish:
	if (dest && fclose(dest) != 0)
		ret = -1;
	if (ret == 0 && strcmp(unzip_tmp_dir(), "/tmp")) {
		FILE *s = fopen(stamp_path, "w");
		if (s) {
			fputs(stamp, s);
			fclose(s);
		}
	}
	return ret;
}

int unzip(FILE *zip, const char **extensions, FILE *dest) {
	int ret = -1;
	struct file_info info = {0};

	if(!find_entry(zip, extensions, &info)) {
		if (!info.inflate(zip, dest, info.compressed_size)) {
			ret = 0;
		} else {
			PA_ERROR("Error decompressing file\n");
		}

	}

	return ret;
}
