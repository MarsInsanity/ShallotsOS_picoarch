/* No-op replacement for the proprietary display driver during user-mode QEMU
 * memory profiles. PicoArch still executes its real R36SX render path. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void *g_render;

static void put_u16(FILE *f, uint16_t v)
{
	fputc(v & 0xff, f);
	fputc(v >> 8, f);
}

static void put_u32(FILE *f, uint32_t v)
{
	put_u16(f, v & 0xffff);
	put_u16(f, v >> 16);
}

static void capture_frame(const uint16_t *pixels, int width, int height, int pitch)
{
	const char *path = getenv("PICOARCH_QEMU_FRAME_CAPTURE");
	if (!path || !*path || !pixels || width <= 0 || height <= 0 || pitch < width * 2)
		return;
	FILE *f = fopen(path, "wb");
	if (!f) { perror("QEMU frame capture"); return; }
	uint32_t row_size = ((uint32_t)width * 3 + 3) & ~3u;
	put_u16(f, 0x4d42); put_u32(f, 54 + row_size * height); put_u32(f, 0);
	put_u32(f, 54); put_u32(f, 40); put_u32(f, width); put_u32(f, height);
	put_u16(f, 1); put_u16(f, 24); put_u32(f, 0); put_u32(f, row_size * height);
	put_u32(f, 2835); put_u32(f, 2835); put_u32(f, 0); put_u32(f, 0);
	uint8_t *row = malloc(row_size);
	if (!row) { fclose(f); return; }
	for (int y = height - 1; y >= 0; --y) {
		const uint16_t *src = (const uint16_t *)((const uint8_t *)pixels + (size_t)y * pitch);
		for (int x = 0; x < width; ++x) {
			uint16_t p = src[x];
			uint8_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
			row[x * 3 + 0] = (b << 3) | (b >> 2);
			row[x * 3 + 1] = (g << 2) | (g >> 4);
			row[x * 3 + 2] = (r << 3) | (r >> 2);
		}
		for (uint32_t i = (uint32_t)width * 3; i < row_size; ++i) row[i] = 0;
		if (fwrite(row, 1, row_size, f) != row_size) break;
	}
	free(row);
	if (fclose(f) == 0)
		fprintf(stderr, "QEMU_FRAME_CAPTURE %s %dx%d pitch=%d RGB565\n", path, width, height, pitch);
}

int video_drivers_init(void) { return 1; }
void video_driver_deinit(void) { }
int video_driver_disp_frame(void *pixels, int width, int height, int pitch)
{
	static unsigned frame_number;
	static unsigned capture_at;
	if (!frame_number) {
		const char *n = getenv("PICOARCH_QEMU_FRAME_CAPTURE_N");
		capture_at = n ? (unsigned)atoi(n) : 1;
		if (!capture_at) capture_at = 1;
	}
	if (++frame_number == capture_at) capture_frame(pixels, width, height, pitch);
	return 0;
}
void fbdev_video_aspect_ratio(int fit) { (void)fit; }
int video_driver_setmode(int a, int b) { (void)a; (void)b; return 0; }
void fbdev_set_enhance(int a, int b, int c, int d, int e)
{
	(void)a; (void)b; (void)c; (void)d; (void)e;
}
