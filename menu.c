#include <sys/stat.h>
#include "core.h"
#include "main.h"
#include "menu.h"
#include "menu_font.h"
#include "frogui_settings.h"
#include "i18n.h"
#include "options.h"
#include "overrides.h"
#include "plat.h"
#include "scale.h"
#include "util.h"

#ifdef PLATFORM_SF3000
extern int sf3000_is_r36sx(void);
#endif

static int drew_alt_bg = 0;

static char cores_path[MAX_PATH];
static struct dirent **corelist = NULL;
static int corelist_len = 0;

#define MENU_ALIGN_LEFT 0
#define MENU_X2 0

#define MENU_ITEMS_PER_PAGE 11

typedef enum
{
	MA_NONE = 1,
	MA_MAIN_RESUME_GAME,
	MA_MAIN_SAVE_STATE,
	MA_MAIN_LOAD_STATE,
	MA_MAIN_DISC_CTRL,
	MA_MAIN_CHEATS,
	MA_MAIN_CORE_SEL,
	MA_MAIN_CONTENT_SEL,
	MA_MAIN_RESET_GAME,
	MA_MAIN_CREDITS,
	MA_MAIN_EXIT,
	MA_OPT_CORE_OPTS,
	MA_OPT_SAVECFG,
	MA_OPT_SAVECFG_GAME,
	MA_OPT_RMCFG_GAME,
	MA_CTRL_PLAYER1,
	MA_CTRL_EMU,
	MA_VID_FX,
	MA_VID_BLANK,
	MA_VID_SCALE_SIZE,
	MA_VID_ASPECT,
	MA_VID_FILTER,
	MA_VID_SHARPNESS,
} menu_id;

me_bind_action me_ctrl_actions[] =
{
	{ "UP       ",  1 << RETRO_DEVICE_ID_JOYPAD_UP,    "UP" },
	{ "DOWN     ",  1 << RETRO_DEVICE_ID_JOYPAD_DOWN,  "DOWN" },
	{ "LEFT     ",  1 << RETRO_DEVICE_ID_JOYPAD_LEFT,  "LEFT" },
	{ "RIGHT    ",  1 << RETRO_DEVICE_ID_JOYPAD_RIGHT, "RIGHT" },
	{ "A BUTTON ",  1 << RETRO_DEVICE_ID_JOYPAD_A,     "A BUTTON" },
	{ "B BUTTON ",  1 << RETRO_DEVICE_ID_JOYPAD_B,     "B BUTTON" },
	{ "X BUTTON ",  1 << RETRO_DEVICE_ID_JOYPAD_X,     "X BUTTON" },
	{ "Y BUTTON ",  1 << RETRO_DEVICE_ID_JOYPAD_Y,     "Y BUTTON" },
	{ "START    ",  1 << RETRO_DEVICE_ID_JOYPAD_START, "START" },
	{ "SELECT   ",  1 << RETRO_DEVICE_ID_JOYPAD_SELECT,"SELECT" },
	{ "L BUTTON ",  1 << RETRO_DEVICE_ID_JOYPAD_L,     "L BUTTON" },
	{ "R BUTTON ",  1 << RETRO_DEVICE_ID_JOYPAD_R,     "R BUTTON" },
	{ "L2 BUTTON ", 1 << RETRO_DEVICE_ID_JOYPAD_L2,    "L2 BUTTON" },
	{ "R2 BUTTON ", 1 << RETRO_DEVICE_ID_JOYPAD_R2,    "R2 BUTTON" },
	{ NULL,       0,                                    NULL }
};

/* Must be a superset of all possible actions. This is used when
 * saving config, and if an entry isn't here, the saver won't see
 * it. */
me_bind_action emuctrl_actions[] =
{
	{ "Save State       ", 1 << EACTION_SAVE_STATE, "Save State" },
	{ "Load State       ", 1 << EACTION_LOAD_STATE, "Load State" },
	{ "Toggle FPS/CPU%  ", 1 << EACTION_TOGGLE_HUD, "Toggle FPS/CPU%" },
	{ "Toggle FF        ", 1 << EACTION_TOGGLE_FF, "Toggle FF" },
	// { "Take Screenshot  ", 1 << EACTION_SCREENSHOT },
	{ NULL,                0, NULL }
};

static int emu_check_save_file(int slot, int *time)
{
	char fname[MAX_PATH];
	struct stat status;
	int ret;

	state_file_name(fname, sizeof(fname), slot);

	ret = stat(fname, &status);
	if (ret != 0)
		return 0;

	return 1;
}

static int emu_save_load_game(int load, int unused)
{
	int ret;

	if (load)
		ret = pa_state_read();
	else
		ret = pa_state_write();

	return ret;
}

// RGB565
static unsigned short fname2color(const char *fname)
{
	return 0xFFFF;
}

/* MinUI draw helpers — defined later in this file, but forward-declared here so
 * the savestate menu (inside the libpicofe/menu.c included below) can use them. */
static void minui_fill(int x, int y, int w, int h, uint16_t c);
static void minui_border(int x, int y, int w, int h, int t, uint16_t c);
static void minui_thumb(const uint16_t *src, int sw, int sh, int spp,
                        int dx, int dy, int dw, int dh);

/* Miyoo-style drawing of every me_loop list (Options and its pages), defined
 * after the main menu's helpers below; me_draw uses it once the TTF is up. */
static void me_draw_miyoo(const menu_entry *entries, int sel, void (*draw_more)(void));
struct me_bind_action;
static void keys_draw_miyoo(const struct me_bind_action *opts, int opt_cnt, int player_idx,
                            int sel, int dev_id, int is_bind);

#include "libpicofe/menu.c"

static void draw_menu_message(const char *msg, void (*draw_more)(void))  __attribute__((unused));

static const char *mgn_saveloadcfg(int id, int *offs)
{
	return "";
}

static int mh_restore_defaults(int id, int keys)
{
	set_defaults();
	menu_update_msg("defaults restored");
	return 1;
}

static int mh_savecfg(int id, int keys)
{
	if (save_config(id == MA_OPT_SAVECFG_GAME ? 1 : 0) == 0)
		menu_update_msg("config saved");
	else
		menu_update_msg("failed to write config");

	return 1;
}

static int mh_rmcfg(int id, int keys)
{
	if (remove_config(id == MA_OPT_RMCFG_GAME ? 1 : 0) == 0)
		menu_update_msg("config removed");
	else
		menu_update_msg("failed to remove config");

	return 1;
}

static void draw_src_bg(void) {
	memcpy(g_menubg_ptr, g_menubg_src_ptr, g_menubg_src_h * g_menubg_src_pp * sizeof(uint16_t));
	/* darker=1 (x0.375) instead of x0.5: dim the menu background a bit more so
	 * text stays readable on bright scenes. Same for every menu (all copy this). */
	menu_darken_bg(g_menubg_ptr, g_menubg_src_ptr, g_menubg_src_h * g_menubg_src_pp, 1);
}

static int mh_set_core(int id, int keys) {
	if (corelist && id < corelist_len)
		snprintf(core_path, sizeof(core_path), "%s/%s", cores_path, corelist[id]->d_name);

	return 1;
}

static int core_selector(const struct dirent *ent) {
	return has_suffix_i(ent->d_name, "_libretro.so");
}

static int menu_loop_core_page(int offset, int keys) {
	static int sel = 0;
	menu_entry e_menu_cores[MENU_ITEMS_PER_PAGE + 2] = {0}; /* +2 for Next, NULL */
	size_t menu_idx = 0;
	int i;
	char names[MENU_ITEMS_PER_PAGE][MAX_PATH];

	for (i = offset, menu_idx = 0; i < corelist_len && menu_idx < MENU_ITEMS_PER_PAGE; i++) {
		menu_entry *option;
		struct dirent *ent = corelist[i];
		option = &e_menu_cores[menu_idx];
		core_extract_name(ent->d_name, names[menu_idx], sizeof(names[menu_idx]));

		option->name = names[menu_idx];
		option->beh = MB_OPT_CUSTOM;
		option->id = i;
		option->enabled = 1;
		option->selectable = 1;
		option->handler = mh_set_core;
		menu_idx++;
	}

	if (i < corelist_len) {
		menu_entry *option;
		option = &e_menu_cores[menu_idx];
		option->name = "Next page";
		option->beh = MB_OPT_CUSTOM;
		option->id = i;
		option->enabled = 1;
		option->selectable = 1;
		option->handler = menu_loop_core_page;
	}

	return me_loop(e_menu_cores, &sel);
}

int menu_select_core(void) {
	int ret = -1;
	getcwd(cores_path, MAX_PATH);

	corelist_len = scandir(cores_path, &corelist, core_selector, alphasort);
	if (!corelist_len) return -1;

	plat_video_menu_enter(1);

	if (menu_loop_core_page(0, 0) < 0)
		goto finish;

	if (core_path[0] == '\0')
		goto finish;

	ret = 0;
finish:
	/* wait until menu, ok, back is released */
	while (in_menu_wait_any(NULL, 50) & (PBTN_MENU|PBTN_MOK|PBTN_MBACK))
		;

	plat_video_menu_leave();

	if (corelist_len > 0) {
		while (corelist_len--)
			free(corelist[corelist_len]);
		free(corelist);
		corelist = NULL;
	}
	return ret;
}

int hidden_file_filter(struct dirent **namelist, int count, const char *basedir) {
	int newcount = 0;

	for (int i = 0; i < count; i++) {
		if (namelist[i]->d_name[0] == '.' && namelist[i]->d_name[1] != '.') {
			free(namelist[i]);
			namelist[i] = NULL;
		}
	}

	for (int i = 0; i < count; i++) {
		if (namelist[i] != NULL)
			namelist[newcount++] = namelist[i];
	}

	return newcount;
}

const char *select_content(void) {
	const char *fname = NULL;
	char content_path[MAX_PATH];
	const char **extensions = core_extensions();
	const char **exts_with_zip = NULL;
	int i = 0, size = 0;

	if (content && content->path) {
		strncpy(content_path, content->path, sizeof(content_path) - 1);
	} else if (getenv("CONTENT_DIR")) {
		strncpy(content_path, getenv("CONTENT_DIR"), sizeof(content_path) - 1);
#ifdef CONTENT_DIR
	} else {
		strncpy(content_path, CONTENT_DIR, sizeof(content_path) - 1);
#else
	} else if (getenv("HOME")) {
		strncpy(content_path, getenv("HOME"), sizeof(content_path) - 1);
#endif
	}

	if (extensions) {
		for (size = 0; extensions[size]; size++)
			;
	}

	exts_with_zip = calloc(size + 2, sizeof (char *)); /* add 2 for "zip", NULL */

	if (exts_with_zip) {
		for (i = 0; extensions[i]; i++) {
			exts_with_zip[i] = extensions[i];
		}
		exts_with_zip[i] = "zip";
	} else {
		exts_with_zip = extensions;
	}

	fname = menu_loop_romsel(content_path, sizeof(content_path), exts_with_zip, hidden_file_filter);

	if (exts_with_zip != extensions)
		free(exts_with_zip);

	return fname;
}

int menu_select_content(char *filename, size_t len) {
	const char *fname = NULL;
	int ret = -1;

	plat_video_menu_enter(1);
	fname = select_content();
	if (!fname)
		goto finish;

	strncpy(filename, fname, len - 1);
	if (g_autostateld_opt)
		resume_slot = 0;
	ret = 0;

finish:
        /* wait until menu, ok, back is released */
	while (in_menu_wait_any(NULL, 50) & (PBTN_MENU|PBTN_MOK|PBTN_MBACK))
		;

	plat_video_menu_leave();
	return ret;
}

static int menu_loop_select_content(int id, int keys) {
	const char *fname = select_content();

	if (fname == NULL)
		return -1;

	core_unload_content();

	content = content_init(fname);
	if (!content) {
		PA_ERROR("Couldn't allocate memory for content\n");
		quit(-1);
	}

	set_defaults();

	if (core_load_content(content)) {
		quit(-1);
	}

	load_config();
	load_config_keys();

	if (g_autostateld_opt) {
		resume_slot = 0;
		state_resume();
	}

	return 1;
}

static int menu_loop_disc(int id, int keys)
{
	static int sel = 0;
	menu_entry e_menu_disc_options[2] = {0};
	unsigned disc = disc_get_index() + 1;
	menu_entry *option = &e_menu_disc_options[0];

	option->name = "Disc";
	option->beh = MB_OPT_RANGE;
	option->var = &disc;
	option->min = 1;
	option->max = disc_get_count();
	option->enabled = 1;
	option->need_to_save = 1;
	option->selectable = 1;

	me_loop(e_menu_disc_options, &sel);

	if (disc_get_index() + 1 != disc)
		disc_switch_index(disc - 1);

	return 0;
}

static int menu_loop_cheats_page(int offset, int keys) {
	static int sel = 0;
	menu_entry *e_menu_cheats;
	size_t i, menu_idx;

	/* cheats + 2 for possible "Next page" +  NULL */
	e_menu_cheats = (menu_entry *)calloc(cheats->count + 2, sizeof(menu_entry));

	if (!e_menu_cheats) {
		PA_ERROR("Error allocating cheats\n");
		return 0;
	}

	for (i = offset, menu_idx = 0; i < cheats->count && menu_idx < MENU_ITEMS_PER_PAGE; i++) {
		struct cheat *cheat = &cheats->cheats[i];
		menu_entry *option;

		option = &e_menu_cheats[menu_idx];

		option->name = cheat->name;
		option->beh = MB_OPT_ONOFF;
		option->var = &cheat->enabled;
		option->enabled = 1;
		option->mask = 1;
		option->need_to_save = 1;
		option->selectable = 1;
		option->help = cheat->info;
		menu_idx++;
	}

	if (i < cheats->count) {
		menu_entry *option;
		option = &e_menu_cheats[menu_idx];
		option->name = "Next page";
		option->beh = MB_OPT_CUSTOM;
		option->id = i;
		option->enabled = 1;
		option->selectable = 1;
		option->handler = menu_loop_cheats_page;
	}

	me_loop(e_menu_cheats, &sel);
	free(e_menu_cheats);

	return 0;
}

static int menu_loop_cheats(int id, int keys)
{
	int ret = menu_loop_cheats_page(0, keys);
	core_apply_cheats(cheats);
	return ret;
}

#define CORE_OPTION_LABEL_CHARS 23
#define CORE_OPTION_VALUE_CHARS 10

/* Format a field for the narrow two-column emulator-options view. Short text
 * stays still, unselected overflow gets an ellipsis, and selected overflow
 * loops after a brief pause so every character can be read. */
static void core_option_field(char *dst, size_t dst_size, const char *src,
	int selected, unsigned int elapsed_ms)
{
	size_t visible;
	size_t len = src ? strlen(src) : 0;

	if (!dst_size)
		return;
	visible = dst_size - 1;
	if (len <= visible) {
		snprintf(dst, dst_size, "%s", src ? src : "");
		return;
	}

	if (!selected) {
		size_t keep = visible > 3 ? visible - 3 : 0;
		memcpy(dst, src, keep);
		memcpy(dst + keep, "...", visible - keep);
		dst[visible] = '\0';
		return;
	}

	/* Show the beginning for 250ms, then move one character every 70ms.
	 * Three spaces make the wrap point clear. */
	size_t pos = elapsed_ms < 250 ? 0 : 1 + (elapsed_ms - 250) / 70;
	size_t cycle = len + 3;
	for (size_t i = 0; i < visible; i++) {
		size_t at = (pos + i) % cycle;
		dst[i] = at < len ? src[at] : ' ';
	}
	dst[visible] = '\0';
}

static void core_options_draw(menu_entry *menu,
	struct core_option_entry **page_entries, size_t page_count, int sel,
	unsigned int selected_since, char names[][CORE_OPTION_LABEL_CHARS + 1],
	char values[][CORE_OPTION_VALUE_CHARS + 1])
{
	char *saved_values[MENU_ITEMS_PER_PAGE] = {0};
	unsigned int elapsed = plat_get_ticks_ms() - selected_since;

	for (size_t row = 0; row < page_count; row++) {
		struct core_option_entry *entry = page_entries[row];
		int selected = (int)row == sel;
		core_option_field(names[row], sizeof(names[row]), entry->desc, selected, elapsed);
		menu[row].name = names[row];

		/* me_draw reads the selected label from this array. Replace just that
		 * pointer for the draw, then restore it before processing input. */
		if (entry->labels && entry->labels[entry->value]) {
			saved_values[row] = entry->labels[entry->value];
			core_option_field(values[row], sizeof(values[row]), saved_values[row],
				selected, elapsed);
			entry->labels[entry->value] = values[row];
		}
	}

	me_draw(menu, sel, NULL);

	for (size_t row = 0; row < page_count; row++) {
		struct core_option_entry *entry = page_entries[row];
		if (saved_values[row])
			entry->labels[entry->value] = saved_values[row];
	}
}

static int core_options_loop(menu_entry *menu,
	struct core_option_entry **page_entries, size_t page_count, int *menu_sel)
{
	int ret = 0, inp, sel = *menu_sel;
	int sel_max = me_count(menu) - 1;
	int previous_sel;
	unsigned int selected_since = plat_get_ticks_ms();
	char names[MENU_ITEMS_PER_PAGE][CORE_OPTION_LABEL_CHARS + 1];
	char values[MENU_ITEMS_PER_PAGE][CORE_OPTION_VALUE_CHARS + 1];

	if (sel_max < 0)
		return 0;
	if (sel > sel_max)
		sel = sel_max;
	while ((!menu[sel].enabled || !menu[sel].selectable) && sel < sel_max)
		sel++;
	previous_sel = sel;

	core_options_draw(menu, page_entries, page_count, sel, selected_since, names, values);
	while (in_menu_wait_any(NULL, 50) & (PBTN_MOK | PBTN_MBACK | PBTN_MENU))
		;

	for (;;) {
		if (sel != previous_sel) {
			previous_sel = sel;
			selected_since = plat_get_ticks_ms();
		}
		core_options_draw(menu, page_entries, page_count, sel, selected_since, names, values);
		inp = in_menu_wait(PBTN_UP | PBTN_DOWN | PBTN_LEFT | PBTN_RIGHT |
			PBTN_MOK | PBTN_MBACK | PBTN_MENU | PBTN_L | PBTN_R, NULL, 70);
		if (inp & (PBTN_MENU | PBTN_MBACK))
			break;
		if (inp & PBTN_UP) {
			do { sel--; if (sel < 0) sel = sel_max; }
			while (!menu[sel].enabled || !menu[sel].selectable);
		}
		if (inp & PBTN_DOWN) {
			do { sel++; if (sel > sel_max) sel = 0; }
			while (!menu[sel].enabled || !menu[sel].selectable);
		}
		if (inp & (PBTN_LEFT | PBTN_RIGHT | PBTN_L | PBTN_R)) {
			if (me_process(&menu[sel], (inp & (PBTN_RIGHT | PBTN_R)) != 0,
				inp & (PBTN_L | PBTN_R)))
				continue;
		}
		if (inp & (PBTN_MOK | PBTN_LEFT | PBTN_RIGHT | PBTN_L | PBTN_R)) {
			if (menu[sel].handler &&
				(menu[sel].beh != MB_NONE || (inp & PBTN_MOK))) {
				ret = menu[sel].handler(menu[sel].id, inp);
				if (ret)
					break;
				sel_max = me_count(menu) - 1;
			}
		}
	}

	*menu_sel = sel;
	return ret;
}

static int menu_loop_core_options_page(int offset, int keys) {
	static int sel = 0;
	menu_entry *e_menu_core_options;
	struct core_option_entry *page_entries[MENU_ITEMS_PER_PAGE] = {0};
	size_t i, menu_idx;
	struct core_option_entry *j2me_screen = options_get_entry("froggykvm_screen_size");
	int old_j2me_screen = j2me_screen ? j2me_screen->value : -1;

	/* core_option + 2 for possible "Next page" +  NULL */
	e_menu_core_options = (menu_entry *)calloc(core_options.visible_len + 2, sizeof(menu_entry));

	if (!e_menu_core_options) {
		PA_ERROR("Error allocating core options\n");
		return 0;
	}

	for (i = offset, menu_idx = 0; i < core_options.len && menu_idx < MENU_ITEMS_PER_PAGE; i++) {
		struct core_option_entry *entry = &core_options.entries[i];
		menu_entry *option;
		const char *key = entry->key;

		if (entry->blocked || !entry->visible)
			continue;

		option = &e_menu_core_options[menu_idx];

		option->name = entry->desc;
		option->beh = MB_OPT_ENUM;
		option->var = options_get_value_ptr(key);
		option->enabled = 1;
		option->need_to_save = 1;
		option->selectable = 1;
		option->data = options_get_options(key);
		option->help = entry->info;
		page_entries[menu_idx] = entry;
		menu_idx++;
	}

	if (i < core_options.len) {
		menu_entry *option;
		option = &e_menu_core_options[menu_idx];
		option->name = "Next page";
		option->beh = MB_OPT_CUSTOM;
		option->id = i;
		option->enabled = 1;
		option->selectable = 1;
		option->handler = menu_loop_core_options_page;
	}

	core_options_loop(e_menu_core_options, page_entries, menu_idx, &sel);

	options_update_changed();
	if (j2me_screen && j2me_screen->value != old_j2me_screen)
		save_config(1);

	free(e_menu_core_options);

	return 0;
}

static int menu_loop_core_options(int id, int keys)
{
	return menu_loop_core_options_page(0, keys);
}

static const char h_rm_config_game[]  = "Removes game-specific config file";

static const char h_restore_def[]     = "Switches back to default settings";

static const char h_show_fps[]        = "Shows frames and vsyncs per second";
static const char h_show_cpu[]        = "Shows CPU usage";
static const char h_ff_enabled[]      = "Allow fast-forward (SELECT+R1). Off by default.";
static const char h_rewind_enabled[]  = "Allow rewind (hold SELECT+B). Uses RAM and slows frames; off by default.";

static const char h_audio_buffer_size[]        =
	"The size of the audio buffer, in frames. Higher\n"
	"values reduce the risk of audio crackling at the\n"
	"cost of delayed sound.";



// static const char h_max_upscale[]       =
// 	"When stretching the screen, the maximum integer\n"
// 	"step to scale up to before hardware scaling. Higher\n"
// 	"values produce crisper scaled pixels but increase\n"
// 	"the risk of audio crackling and frameskip.";

// static const char h_scale_filter[]        =
// 	"When stretching, how missing pixels are filled.\n"
// 	"Nearest copies the last pixel. Sharp keeps pixels\n"
// 	"aligned where possible. Smooth adds a blur effect.";



static const char h_optimize_text[]        =
	"When non-integer scaling is required,\n"
	"prioritize a consistent stroke size for\n"
	"text. May affect non-text content.";


static const char *men_aspect_ratio[] = {
	"Integer", "Native", "4:3", "16:9", "3:2", "5:4", "8:7", "16:10", "Fill", NULL };
/* SF3000/SF3500 expose only the hardware-supported modes (used by
 * menu_loop_video_prep when the running device is not R36SX). */
static const char *men_aspect_ratio_sf[] = { "Integer", "Native", "Fill", NULL };

static const char h_aspect_ratio[] =
	"Integer: exact pixel multiples (sharpest).\n"
	"Native: the ratio the core reports. Then forced ratios,\n"
	"and Fill: stretch to the whole screen.";
static const char *men_scale_filter[] = { "Nearest", "Bilinear", "Sharp", NULL};
static const char h_scale_filter[] =
	"Nearest/Sharp are software-scaled and can lower FPS.\n"
	"Bilinear is hardware (fast, soft). Sharp ~ nearest, faster.";

/* Custom name+cycle for screen size — hides "Integer" when filter=BILINEAR
 * (HW path: SW-upscale to 854×480 panel buffer is too memory-heavy and lags
 * at 60fps). User can still cycle Aspect/Full freely in that mode. */



#ifdef PLATFORM_SF3000
extern int sf3000_snd_gain_pct;            /* SW output gain percent (plat_sdl.c) */
extern void sf3000_apply_snd_gain(void);
static const char h_sf3000_volume[] =
	"Lowers this app's audio output so the system's low\n"
	"volume steps are actually quiet (the stock volume\n"
	"curve is loud). 100 = full. Saved to sndgain.txt.";
static const char h_sf3000_mono[] =
	"Mix both stereo channels into the handheld's speaker.\n"
	"Disable only when using stereo-capable external audio.";
static const char h_sf3000_sharpness[] =
	"Hardware edge-sharpen for scaled video (0=off, 10=max).\n"
	"Crisps up the display's bilinear scaling. Per-game.";
#endif

static menu_entry e_menu_video_options[] =
{
	mee_onoff_h      ("Show FPS",                 0, show_fps, 1, h_show_fps),
	mee_onoff_h      ("Show CPU %",               0, show_cpu, 1, h_show_cpu),
	mee_onoff_h      ("Fast forward (SELECT+R1)",  0, ff_enabled, 1, h_ff_enabled),
	mee_onoff_h      ("Rewind (hold SELECT+B)",   0, rewind_enabled, 1, h_rewind_enabled),
	mee_enum_h       ("Aspect ratio",  MA_VID_ASPECT, aspect_ratio_mode, men_aspect_ratio, h_aspect_ratio),
	mee_enum_h       ("Filter",        MA_VID_FILTER, scale_filter, men_scale_filter, h_scale_filter),
	// mee_range_h      ("Max upscale",              0, max_upscale, 1, 8, h_max_upscale),
	mee_handler_id_h (             "", MA_VID_BLANK, NULL, NULL),
	mee_onoff_h      ("Optimize text",            0, optimize_text, 1, h_optimize_text),
	mee_range_h      ("Audio buffer",             0, audio_buffer_size, 1, 15, h_audio_buffer_size),
#ifdef PLATFORM_SF3000
	mee_onoff_h      ("Mono output",               0, audio_mono, 1, h_sf3000_mono),
	mee_range_h      ("Sharpness",     MA_VID_SHARPNESS, sf3000_sharpness, 0, 10, h_sf3000_sharpness),
	mee_range_h      ("Volume %",                 0, sf3000_snd_gain_pct, 0, 100, h_sf3000_volume),
#endif
	mee_end,
};

	// only show effects on native scale
static void menu_loop_video_prep(void) {
	#ifdef PLATFORM_SF3000
	/* Keep the live pmem reload from restoring the previous value while the
	 * Volume slider is being edited. */
	sf3000_apply_snd_gain();
	/* SF3000/SF3500 expose only the hardware-supported Native and Fill modes.
	 * R36SX keeps the complete aspect list on its separate display path. */
	if (!sf3000_is_r36sx()) {
		if (aspect_ratio_mode == 8) aspect_ratio_mode = 2; /* Fill -> compact index */
		else if (aspect_ratio_mode > 2) aspect_ratio_mode = 1; /* unsupported forced ratio -> Native */
		e_menu_video_options[4].data = men_aspect_ratio_sf;
	} else {
		e_menu_video_options[4].data = men_aspect_ratio;
	}
	#endif
	/* Filter is owned by FrogUI settings. The old scale-size/effects entries are
	 * intentionally removed from this menu; do not call me_enable() for their
	 * IDs, since me_id2offset() falls back to entry 0 and disables Show FPS. */
	me_enable(e_menu_video_options, MA_VID_FILTER, true);
	/* Sharpness is a HW post-process for the hardware scale (Bilinear + Sharp);
	 * true Nearest is already sharp (and SW), so hide it there. */
	me_enable(e_menu_video_options, MA_VID_SHARPNESS,
	          scale_filter != SCALE_FILTER_NEAREST);
	/* Screen-size/effects entries are no longer present in this compact menu. */
}

static int menu_loop_video_options(int id, int keys)
{
	static int sel = 0;
	menu_loop_video_prep();

	me_loop_d(e_menu_video_options, &sel, menu_loop_video_prep, NULL);
	#ifdef PLATFORM_SF3000
	if (!sf3000_is_r36sx() && aspect_ratio_mode == 2) aspect_ratio_mode = 8;
	#endif
	scale_update_scaler();

#ifdef PLATFORM_SF3000
	sf3000_apply_snd_gain();   /* recompute + persist the volume gain on exit */
#endif
	/* Per-game settings are written only by the explicit Save Game Config
	 * action.  Opening/changing video settings must not create a game config. */
	return 0;
}

static int key_config_loop_wrap(int id, int keys)
{
	const struct core_override *override = get_overrides();
	me_bind_action *actions = CORE_OVERRIDE(override, actions, me_ctrl_actions);
	size_t action_size = CORE_OVERRIDE(override, action_size, array_size(me_ctrl_actions));
	me_bind_action *emu_actions = emuctrl_actions;
	size_t emu_action_size = array_size(emuctrl_actions);

	switch (id) {
	case MA_CTRL_PLAYER1:
		key_config_loop(actions, action_size - 1, 0);
		break;
	case MA_CTRL_EMU:
		key_config_loop(emu_actions, emu_action_size - 1, -1);
		break;
	default:
		break;
	}
	return 0;
}

const char *config_label(int id, int *offs) {
	return config_override
		? tr_or("pico.config.loaded_game", "Loaded: game config")
		: tr_or("pico.config.loaded_global", "Loaded: global config");
}

static menu_entry e_menu_config_options[] =
{
	mee_cust_nosave  ("Save global config",       MA_OPT_SAVECFG,      mh_savecfg, mgn_saveloadcfg),
	mee_cust_nosave  ("Save game config",         MA_OPT_SAVECFG_GAME, mh_savecfg, mgn_saveloadcfg),
	mee_handler_id_h ("Delete game config",       MA_OPT_RMCFG_GAME,   mh_rmcfg,   h_rm_config_game),
	mee_handler_h    ("Restore defaults",         mh_restore_defaults, h_restore_def),
	mee_label        (""),
	mee_label_mk     (0,                          config_label),
	mee_end,
};

static int menu_loop_config_options(int id, int keys)
{
	static int sel = 0;
	me_enable(e_menu_config_options, MA_OPT_RMCFG_GAME, config_override == 1);

	me_loop(e_menu_config_options, &sel);

	return 0;
}

static menu_entry e_menu_options[] =
{
	mee_handler   ("Audio and video",    menu_loop_video_options),
	mee_handler_id("Emulator options",   MA_OPT_CORE_OPTS,    menu_loop_core_options),
	mee_handler_id("Player controls",    MA_CTRL_PLAYER1,     key_config_loop_wrap),
	mee_handler_id("Emulator controls",  MA_CTRL_EMU,         key_config_loop_wrap),
	mee_handler   ("Save config",        menu_loop_config_options),
	mee_end,
};

static int menu_loop_options(int id, int keys)
{
	static int sel = 0;
	me_loop(e_menu_options, &sel);

	return 0;
}

static int main_menu_handler(int id, int keys)
{
	switch (id)
	{
	case MA_MAIN_RESUME_GAME:
		return 1;
	case MA_MAIN_SAVE_STATE:
		return menu_loop_savestate(0);
	case MA_MAIN_LOAD_STATE:
		return menu_loop_savestate(1);
	case MA_MAIN_RESET_GAME:
		current_core.retro_reset();
		return 1;
	case MA_MAIN_EXIT:
		should_quit = 1;
		return 1;
	default:
		lprintf("%s: something unknown selected\n", __FUNCTION__);
		break;
	}

	return 0;
}

static menu_entry e_menu_main[] =
{
	mee_handler_id("Resume game",        MA_MAIN_RESUME_GAME, main_menu_handler),
	mee_handler_id("Save state",         MA_MAIN_SAVE_STATE,  main_menu_handler),
	mee_handler_id("Load state",         MA_MAIN_LOAD_STATE,  main_menu_handler),
	mee_handler_id("Disc control",       MA_MAIN_DISC_CTRL,   menu_loop_disc),
	mee_handler_id("Cheats",             MA_MAIN_CHEATS,      menu_loop_cheats),
	mee_handler   ("Options",            menu_loop_options),
	mee_handler_id("Reset game",         MA_MAIN_RESET_GAME,  main_menu_handler),
	mee_handler_id("Load new game",      MA_MAIN_CONTENT_SEL, menu_loop_select_content),
	mee_handler_id("Exit",               MA_MAIN_EXIT,        main_menu_handler),
	mee_end,
};

/* Keep untranslated/third-party core labels untouched. These are PicoArch's
 * own stable shell labels and are refreshed after the shared pack is loaded. */
static void menu_localize(void)
{
	/* Miyoo / OnionOS wording for the main menu. */
	e_menu_main[0].name = tr_or("pico.menu.continue", "Continue");
	e_menu_main[1].name = tr_or("pico.menu.save", "Save");
	e_menu_main[2].name = tr_or("pico.menu.load", "Load");
	e_menu_main[3].name = tr_or("pico.menu.disc_control", "Disc control");
	e_menu_main[4].name = tr_or("pico.menu.cheats", "Cheats");
	e_menu_main[5].name = tr_or("pico.menu.options", "Options");
	e_menu_main[6].name = tr_or("pico.menu.reset", "Reset");
	e_menu_main[7].name = tr_or("pico.menu.load_new_game", "Load new game");
	e_menu_main[8].name = tr_or("pico.menu.exit_game", "Exit Game");
	e_menu_options[0].name = tr_or("pico.options.audio_video", "Audio and video");
	e_menu_options[1].name = tr_or("pico.options.emulator", "Emulator options");
	e_menu_options[2].name = tr_or("pico.options.player_controls", "Player controls");
	e_menu_options[3].name = tr_or("pico.options.emulator_controls", "Emulator controls");
	e_menu_options[4].name = tr_or("pico.options.save_config", "Save config");
	e_menu_video_options[0].name = tr_or("pico.video.show_fps", "Show FPS");
	e_menu_video_options[1].name = tr_or("pico.video.show_cpu", "Show CPU %");
	e_menu_video_options[2].name = tr_or("pico.video.fast_forward", "Fast forward (SELECT+R1)");
	e_menu_video_options[3].name = tr_or("pico.video.rewind", "Rewind (hold SELECT+B)");
	e_menu_video_options[4].name = tr_or("pico.video.aspect_ratio", "Aspect ratio");
	e_menu_video_options[5].name = tr_or("pico.video.filter", "Filter");
	e_menu_video_options[7].name = tr_or("pico.video.optimize_text", "Optimize text");
	e_menu_video_options[8].name = tr_or("pico.video.audio_buffer", "Audio buffer");
#ifdef PLATFORM_SF3000
	e_menu_video_options[9].name = tr_or("pico.video.mono_output", "Mono output");
	e_menu_video_options[10].name = tr_or("pico.video.sharpness", "Sharpness");
	e_menu_video_options[11].name = tr_or("pico.video.volume", "Volume %");
#endif
	e_menu_config_options[0].name = tr_or("pico.config.save_global", "Save global config");
	e_menu_config_options[1].name = tr_or("pico.config.save_game", "Save game config");
	e_menu_config_options[2].name = tr_or("pico.config.delete_game", "Delete game config");
	e_menu_config_options[3].name = tr_or("pico.config.restore_defaults", "Restore defaults");
	me_ctrl_actions[0].name = (char *)tr_or("pico.control.up", "UP       ");
	me_ctrl_actions[1].name = (char *)tr_or("pico.control.down", "DOWN     ");
	me_ctrl_actions[2].name = (char *)tr_or("pico.control.left", "LEFT     ");
	me_ctrl_actions[3].name = (char *)tr_or("pico.control.right", "RIGHT    ");
	me_ctrl_actions[4].name = (char *)tr_or("pico.control.a", "A BUTTON ");
	me_ctrl_actions[5].name = (char *)tr_or("pico.control.b", "B BUTTON ");
	me_ctrl_actions[6].name = (char *)tr_or("pico.control.x", "X BUTTON ");
	me_ctrl_actions[7].name = (char *)tr_or("pico.control.y", "Y BUTTON ");
	me_ctrl_actions[8].name = (char *)tr_or("pico.control.start", "START    ");
	me_ctrl_actions[9].name = (char *)tr_or("pico.control.select", "SELECT   ");
	me_ctrl_actions[10].name = (char *)tr_or("pico.control.l", "L BUTTON ");
	me_ctrl_actions[11].name = (char *)tr_or("pico.control.r", "R BUTTON ");
	me_ctrl_actions[12].name = (char *)tr_or("pico.control.l2", "L2 BUTTON ");
	me_ctrl_actions[13].name = (char *)tr_or("pico.control.r2", "R2 BUTTON ");
	emuctrl_actions[0].name = (char *)tr_or("pico.emu.save_state", "Save State       ");
	emuctrl_actions[1].name = (char *)tr_or("pico.emu.load_state", "Load State       ");
	emuctrl_actions[2].name = (char *)tr_or("pico.emu.toggle_hud", "Toggle FPS/CPU%  ");
	emuctrl_actions[3].name = (char *)tr_or("pico.emu.fast_forward", "Toggle FF        ");
}

static void draw_savestate_bg(int slot)
{
	char filename[MAX_PATH];
	int w, h, bpp;
	size_t bufsize = SCREEN_PITCH * SCREEN_HEIGHT;
	void *buf = calloc(bufsize, sizeof(char));

	if (!buf) {
		PA_WARN("Couldn't allocate savestate background");
		goto finish;
	}
	state_file_name(filename, MAX_PATH, slot);

	if (plat_load_screen(filename, buf, bufsize, &w, &h, &bpp))
		goto finish;

	if (bpp == sizeof(uint16_t)) {
		menu_darken_bg(g_menubg_ptr, buf, w * h, 1);
		drew_alt_bg = 1;
	}

finish:
	if (buf)
		free(buf);
}

void menu_begin(void)
{
	if (!drew_alt_bg)
		draw_src_bg();
}

void menu_end(void)
{
	drew_alt_bg = 0;
}

/* ------------------------- MinUI-style main pause menu ------------------------- */


static void minui_game_title(char *out, size_t n) {
	const char *p = (content && content->path[0]) ? content->path : "";
	const char *base = strrchr(p, '/');
	base = base ? base + 1 : p;
	snprintf(out, n, "%s", base);
	char *dot = strrchr(out, '.');
	if (dot && dot != out) *dot = '\0';
	if (!out[0]) snprintf(out, n, "picoarch");
}

static void minui_fill(int x, int y, int w, int h, uint16_t c) {
	uint16_t *fb = (uint16_t *)g_menuscreen_ptr;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > g_menuscreen_w) w = g_menuscreen_w - x;
	if (y + h > g_menuscreen_h) h = g_menuscreen_h - y;
	for (int j = 0; j < h; j++) {
		uint16_t *r = fb + (size_t)(y + j) * g_menuscreen_pp + x;
		for (int i = 0; i < w; i++) r[i] = c;
	}
}

static void minui_border(int x, int y, int w, int h, int t, uint16_t c) {
	minui_fill(x, y, w, t, c);
	minui_fill(x, y + h - t, w, t, c);
	minui_fill(x, y, t, h, c);
	minui_fill(x + w - t, y, t, h, c);
}

/* Nearest-scale an RGB565 source into a box of g_menuscreen. */
static void minui_thumb(const uint16_t *src, int sw, int sh, int spp,
                        int dx, int dy, int dw, int dh) {
	if (!src || sw <= 0 || sh <= 0) { minui_fill(dx, dy, dw, dh, 0x0000); return; }
	uint16_t *fb = (uint16_t *)g_menuscreen_ptr;
	for (int y = 0; y < dh; y++) {
		int sy = y * sh / dh;
		const uint16_t *srow = src + (size_t)sy * spp;
		uint16_t *drow = fb + (size_t)(dy + y) * g_menuscreen_pp + dx;
		for (int x = 0; x < dw; x++) drow[x] = srow[x * sw / dw];
	}
}

/* ---- Main menu, laid out like the Miyoo / OnionOS in-game menu -------------
 * The paused game fills the screen; a dark panel on the left holds the rows,
 * the selected one on a full-width bar in the theme's selection colour, and a
 * hint bar runs along the bottom. Save and Load pick their slot in place with
 * Left/Right (as on SpruceOS); while one of them is selected, the background
 * shows that slot's screenshot, or "Empty". */
#define MAIN_SLOT_ROW(id) ((id) == MA_MAIN_SAVE_STATE || (id) == MA_MAIN_LOAD_STATE)

/* The slot's screenshot, cached until the slot or the files change. */
static uint16_t *slot_shot;
static int slot_shot_w, slot_shot_h, slot_shot_for = -2;

static void slot_shot_invalidate(void) { slot_shot_for = -2; }

static int slot_shot_load(int slot) {
	if (slot_shot_for == slot)
		return slot_shot != NULL && slot_shot_w > 0;
	slot_shot_for = slot;
	slot_shot_w = slot_shot_h = 0;
	if (!(state_slot_flags & (1 << slot)))
		return 0;
	size_t cap = (size_t)g_menuscreen_w * g_menuscreen_h;
	if (!slot_shot)
		slot_shot = malloc(cap * 2);
	if (!slot_shot)
		return 0;
	char fn[MAX_PATH];
	int bpp = 0;
	state_file_name(fn, sizeof(fn), slot);
	if (plat_load_screen(fn, slot_shot, cap * 2, &slot_shot_w, &slot_shot_h, &bpp) != 0 || bpp != 2)
		slot_shot_w = 0;
	return slot_shot_w > 0;
}

/* A small filled triangle pointing left (dir < 0) or right. */
static void minui_arrow(int cx, int cy, int size, int dir, uint16_t c) {
	/* Column i from the tip: the tip is on the side it points to. */
	for (int i = 0; i < size; i++) {
		int half = i * 2 / 3;
		int x = dir < 0 ? cx - size / 2 + i : cx + size / 2 - i;
		minui_fill(x, cy - half, 1, half * 2 + 1, c);
	}
}

static void minui_text_mid(int x, int cy, const char *t, uint16_t color) {
	int baseline, cap;
	menu_font_cap_metrics(&baseline, &cap);
	menu_font_draw_text((uint16_t *)g_menuscreen_ptr, g_menuscreen_w, g_menuscreen_h,
	                    x, cy - (baseline - cap / 2), t, color);
}

/* One footer hint: the button letter on a rounded square, then its label. */
static int minui_hint(int x, int cy, int size, const char *btn, const char *label,
                      uint16_t accent) {
	menu_round_fill(x, cy - size / 2, size, size, size / 4, accent);
	int bw = menu_font_measure(btn);
	minui_text_mid(x + (size - bw) / 2, cy, btn, 0xFFFF);
	x += size + size / 4;
	minui_text_mid(x, cy, label, 0xFFFF);
	return x + menu_font_measure(label) + size;
}

/* Status bar over the Options and controls pages: the game's name on the
 * left, room for the battery (drawn by menu_draw_end at the top right).
 * Returns its height. */
static int minui_status_bar(const char *title, uint16_t bg, uint16_t fg) {
	int W = g_menuscreen_w, H = g_menuscreen_h;
	int h = H * 40 / 480;
	if (h < 22) h = 22;   /* the battery is 11 px tall at y = 6 */
	minui_fill(0, 0, W, h, bg);
	float px = h * 0.55f;
	menu_font_set_px(px < 14 ? 14 : px);
	char label[128];
	snprintf(label, sizeof label, "%s", title);
	int room = W - W / 32 - 48;
	while (label[0] && menu_font_measure(label) > room) {
		size_t len = strlen(label);
		if (len <= 4) { label[0] = '\0'; break; }
		label[len - 4] = '\0';
		strcat(label, "...");
	}
	minui_text_mid(W / 32, h / 2, label, fg);
	return h;
}

/* A list row's value text ("" when it has none); *can_change is set for
 * values Left/Right step through. */
static void me_value_text(const menu_entry *ent, char *out, size_t size, int *can_change) {
	int offs = 0;
	out[0] = '\0';
	*can_change = 0;
	switch (ent->beh) {
	case MB_OPT_ONOFF:
		snprintf(out, size, "%s", me_read_onoff(ent) ? tr_or("value.on", "On") : tr_or("value.off", "Off"));
		*can_change = 1;
		break;
	case MB_OPT_RANGE:
		snprintf(out, size, "%d", *(int *)ent->var);
		*can_change = 1;
		break;
	case MB_OPT_CUSTOM:
	case MB_OPT_CUSTONOFF:
	case MB_OPT_CUSTRANGE: {
		const char *name = ent->generate_name ? ent->generate_name(ent->id, &offs) : NULL;
		if (name)
			snprintf(out, size, "%s", name);
		*can_change = ent->beh != MB_OPT_CUSTOM || ent->handler != NULL;
		break;
	}
	case MB_OPT_ENUM: {
		const char **names = (const char **)ent->data;
		int v = *(unsigned char *)ent->var;
		for (int i = 0; names && names[i]; i++)
			if (i == v) {
				snprintf(out, size, "%s", names[i]);
				break;
			}
		*can_change = 1;
		break;
	}
	default:
		break;
	}
}

static void me_draw_miyoo(const menu_entry *entries, int sel, void (*draw_more)(void)) {
	extern int menu_sel_color, menu_sel_text_color;
	const uint16_t ACCENT = (menu_sel_color >= 0) ? (uint16_t)menu_sel_color : 0xC104;
	const uint16_t SEL_TEXT = (uint16_t)menu_sel_text_color;
	const uint16_t PANEL = 0x18E3, FOOTER = 0x10A2, DIM = 0xA534;
	int W = g_menuscreen_w, H = g_menuscreen_h;
	int foot_h = H * 56 / 480, top, area_h, rh, pad = W / 32;

	plat_video_menu_begin();
	minui_fill(0, 0, W, H, PANEL);
	char game[128];
	minui_game_title(game, sizeof game);
	top = minui_status_bar(game, FOOTER, DIM);
	area_h = H - foot_h - top;
	rh = area_h / 6;

	/* Visible rows, and the window that keeps the selection on screen. */
	int vis[256], n = 0, sel_vis = 0;
	for (int i = 0; entries[i].name && n < 256; i++) {
		if (!entries[i].enabled) continue;
		if (i == sel) sel_vis = n;
		vis[n++] = i;
	}
	int per_page = area_h / rh;
	int first = sel_vis - per_page / 2;
	if (first > n - per_page) first = n - per_page;
	if (first < 0) first = 0;

	float px = rh * 0.42f;
	if (px < 18) px = 18;
	if (px > 28) px = 28;
	menu_font_set_px(px);

	for (int r = 0; r < per_page && first + r < n; r++) {
		const menu_entry *ent = &entries[vis[first + r]];
		int on = vis[first + r] == sel, y = top + r * rh;
		uint16_t tc = on ? SEL_TEXT : 0xFFFF;
		if (on)
			minui_fill(0, y, W, rh, ACCENT);
		int offs = 0;
		const char *name = ent->name;
		if ((!name || !name[0]) && ent->generate_name)
			name = ent->generate_name(ent->id, &offs);
		char value[96];
		int can_change;
		me_value_text(ent, value, sizeof value, &can_change);
		int right = W - pad, vw = menu_font_measure(value), a = rh / 4;
		if (value[0]) {
			if (can_change) {
				minui_arrow(right - a / 2, y + rh / 2, a, 1, tc);
				right -= a * 2;
			}
			minui_text_mid(right - vw, y + rh / 2, value, on ? tc : DIM);
			if (can_change)
				minui_arrow(right - vw - a, y + rh / 2, a, -1, tc);
		}
		/* Keep the name clear of the value; cut it with "..." if needed. */
		char label[128];
		snprintf(label, sizeof label, "%s", name ? name : "");
		int room = (value[0] ? right - vw - a * 3 : W - pad) - pad;
		while (label[0] && menu_font_measure(label) > room) {
			size_t len = strlen(label);
			if (len <= 4) { label[0] = '\0'; break; }
			label[len - 4] = '\0';
			strcat(label, "...");
		}
		minui_text_mid(pad, y + rh / 2, label, tc);
	}

	/* Hint bar; a message (or the row's help) takes it over for a while. */
	minui_fill(0, H - foot_h, W, foot_h, FOOTER);
	int cy = H - foot_h / 2, bsz = foot_h * 6 / 10;
	const char *note = menu_error_msg[0] ? menu_error_msg : NULL;
	if (note && plat_get_ticks_ms() - menu_error_time > 2048)
		menu_error_msg[0] = 0, note = NULL;
	if (note) {
		minui_text_mid(pad, cy, note, 0xFFFF);
	} else {
		int hx = minui_hint(pad, cy, bsz, "A", tr_or("pico.legend.ok", "OK"), ACCENT);
		hx = minui_hint(hx, cy, bsz, "B", tr_or("pico.legend.back", "BACK"), ACCENT);
		int a = bsz / 3;
		minui_arrow(hx + a / 2, cy, a, -1, 0xFFFF);
		minui_arrow(hx + a * 2, cy, a, 1, 0xFFFF);
		minui_text_mid(hx + a * 3 + a / 2, cy, tr_or("pico.legend.change", "CHANGE"), 0xFFFF);
		if (n > per_page) {
			char pos[16];
			snprintf(pos, sizeof pos, "%d/%d", sel_vis + 1, n);
			minui_text_mid(W - pad - menu_font_measure(pos), cy, pos, DIM);
		}
	}

	menu_font_set_px((float)MENU_TTF_PX);
	if (draw_more)
		draw_more();
	menu_draw_end();
}

/* Player / Emulator controls in the same style: a title row, each action
 * with its binding on the right, and the bind/clear keys in the hint bar. */
static void keys_draw_miyoo(const struct me_bind_action *opts_, int opt_cnt, int player_idx,
                            int sel, int dev_id, int is_bind) {
	const me_bind_action *opts = (const me_bind_action *)opts_;
	extern int menu_sel_color, menu_sel_text_color;
	const uint16_t ACCENT = (menu_sel_color >= 0) ? (uint16_t)menu_sel_color : 0xC104;
	const uint16_t SEL_TEXT = (uint16_t)menu_sel_text_color;
	const uint16_t PANEL = 0x18E3, FOOTER = 0x10A2, DIM = 0xA534;
	int W = g_menuscreen_w, H = g_menuscreen_h;
	int foot_h = H * 56 / 480, pad = W / 32;

	plat_video_menu_begin();
	minui_fill(0, 0, W, H, PANEL);
	char game[128];
	minui_game_title(game, sizeof game);
	int top = minui_status_bar(game, FOOTER, DIM);
	int area_h = H - foot_h - top, rh = area_h / 7;
	int per_page = area_h / rh - 1;   /* the first row is the title */
	int first = sel - per_page / 2;
	if (first > opt_cnt - per_page) first = opt_cnt - per_page;
	if (first < 0) first = 0;
	float px = rh * 0.42f;
	if (px < 18) px = 18;
	if (px > 26) px = 26;
	menu_font_set_px(px);

	char title[64];
	if (player_idx >= 0)
		snprintf(title, sizeof title, tr_or("pico.keys.player", "Player %d controls"), player_idx + 1);
	else
		snprintf(title, sizeof title, "%s", tr_or("pico.keys.emulator", "Emulator controls"));
	minui_text_mid(pad, top + rh / 2, title, ACCENT);
	if (opt_cnt > per_page) {
		char pos[24];
		snprintf(pos, sizeof pos, "%d/%d", sel + 1, opt_cnt);
		minui_text_mid(W - pad - menu_font_measure(pos), top + rh / 2, pos, DIM);
	}
	for (int r = 0; r < per_page && first + r < opt_cnt; r++) {
		int i = first + r, y = top + (r + 1) * rh, on = i == sel;
		uint16_t tc = on ? SEL_TEXT : 0xFFFF;
		if (on)
			minui_fill(0, y, W, rh, ACCENT);
		const char *bind = action_binds(player_idx, opts[i].mask, dev_id);
		int bw = menu_font_measure(bind);
		minui_text_mid(pad, y + rh / 2, localized_action_name(opts[i].name), tc);
		minui_text_mid(W - pad - bw, y + rh / 2, bind, on ? tc : DIM);
	}

	minui_fill(0, H - foot_h, W, foot_h, FOOTER);
	int cy = H - foot_h / 2, bsz = foot_h * 6 / 10;
	if (is_bind) {
		minui_text_mid(pad, cy, tr_or("pico.keys.press", "Press a button to bind or unbind"), 0xFFFF);
	} else {
		char ok[16], clr[16];
		snprintf(ok, sizeof ok, "%s", in_get_key_name(-1, -PBTN_MOK));
		snprintf(clr, sizeof clr, "%s", in_get_key_name(-1, -PBTN_MA2));
		int hx = minui_hint(pad, cy, bsz, ok[0] ? ok : "A", tr_or("pico.legend.bind", "BIND"), ACCENT);
		hx = minui_hint(hx, cy, bsz, clr[0] ? clr : "X", tr_or("pico.legend.clear", "CLEAR"), ACCENT);
		minui_hint(hx, cy, bsz, "B", tr_or("pico.legend.back", "BACK"), ACCENT);
	}
	menu_font_set_px((float)MENU_TTF_PX);
	menu_draw_end();
}

static void minui_draw_main(menu_entry *menu, int sel) {
	extern int menu_sel_color, menu_sel_text_color;
	const uint16_t ACCENT = (menu_sel_color >= 0) ? (uint16_t)menu_sel_color : 0xC104;
	const uint16_t SEL_TEXT = (uint16_t)menu_sel_text_color;
	const uint16_t PANEL = 0x18E3, FOOTER = 0x10A2, DIM = 0x8C51;
	int W = g_menuscreen_w, H = g_menuscreen_h;
	int foot_h = H * 56 / 480, area_h = H - foot_h, panel_w = W / 2;
	int sel_id = menu[sel].id;
	int slot_row = MAIN_SLOT_ROW(sel_id) && (menu[sel].enabled);

	plat_video_menu_begin();
	minui_fill(0, 0, W, H, 0x0000);

	/* Background: the paused game, or the selected slot's screenshot, at
	 * 4:3 like the game on screen. */
	int bh = area_h, bw = bh * 4 / 3;
	if (bw > W) { bw = W; bh = bw * 3 / 4; }
	int bx = (W - bw) / 2, by = (area_h - bh) / 2;
	if (slot_row) {
		if (slot_shot_load(state_slot))
			minui_thumb(slot_shot, slot_shot_w, slot_shot_h, slot_shot_w, bx, by, bw, bh);
	} else {
		minui_thumb((const uint16_t *)g_menubg_src_ptr, g_menubg_src_w, g_menubg_src_h,
		            g_menubg_src_pp, bx, by, bw, bh);
	}

	/* Left panel and rows. */
	int rows = 0;
	for (menu_entry *e = menu; e->name; e++)
		if (e->enabled) rows++;
	int rh = area_h / (rows > 6 ? rows : 6);
	float px = rh * 0.42f;
	if (px < 18) px = 18;
	if (px > 30) px = 30;
	menu_font_set_px(px);
	minui_fill(0, 0, panel_w, area_h, PANEL);
	int pad = W / 32, y = 0;
	for (menu_entry *e = menu; e->name; e++) {
		if (!e->enabled) continue;
		int on = (e - menu) == sel;
		uint16_t tc = on ? SEL_TEXT : 0xFFFF;
		if (on)
			minui_fill(0, y, panel_w, rh, ACCENT);
		minui_text_mid(pad, y + rh / 2, e->name, tc);
		if (MAIN_SLOT_ROW(e->id)) {
			/* "<  1  >" at the right edge of the panel. */
			char num[8];
			snprintf(num, sizeof num, "%d", state_slot + 1);
			int nw = menu_font_measure(num), a = rh / 4;
			int right = panel_w - pad, cx = right - a - nw / 2 - a;
			minui_arrow(right - a / 2, y + rh / 2, a, 1, tc);
			minui_text_mid(cx - nw / 2, y + rh / 2, num, tc);
			minui_arrow(cx - nw / 2 - a - a / 2, y + rh / 2, a, -1, tc);
		}
		y += rh;
	}

	if (slot_row && !(state_slot_flags & (1 << state_slot))) {
		const char *empty = tr_or("pico.menu.slot_empty", "Empty");
		minui_text_mid(panel_w + (W - panel_w - menu_font_measure(empty)) / 2, area_h / 2, empty, DIM);
	}

	/* Hint bar. */
	minui_fill(0, area_h, W, foot_h, FOOTER);
	const char *ok = sel_id == MA_MAIN_SAVE_STATE ? tr_or("pico.legend.save", "SAVE")
	               : sel_id == MA_MAIN_LOAD_STATE ? tr_or("pico.legend.load", "LOAD")
	               : tr_or("pico.legend.open", "OPEN");
	int hx = pad, cy = area_h + foot_h / 2, bsz = foot_h * 6 / 10;
	hx = minui_hint(hx, cy, bsz, "A", ok, ACCENT);
	hx = minui_hint(hx, cy, bsz, "B", tr_or("pico.legend.back", "BACK"), ACCENT);
	if (slot_row) {
		/* Left/Right picks the slot. */
		int a = bsz / 3;
		minui_arrow(hx + a / 2, cy, a, -1, 0xFFFF);
		minui_arrow(hx + a * 2, cy, a, 1, 0xFFFF);
		minui_text_mid(hx + a * 3 + a / 2, cy, tr_or("pico.legend.slot", "SLOT"), 0xFFFF);
	}

	menu_font_set_px((float)MENU_TTF_PX);   /* the other menus' size */
	menu_draw_end();
}

static int minui_main_loop(menu_entry *menu, int *menu_sel) {
	extern void pa_toast_later(const char *text);
	int ret = 0, inp, sel = *menu_sel, smax = me_count(menu) - 1;
	if (smax < 0) return 0;
	while ((!menu[sel].enabled || !menu[sel].selectable) && sel < smax) sel++;
	if (state_slot < 0 || state_slot >= STATE_SLOT_COUNT) state_slot = 0;
	state_check_slots();
	slot_shot_invalidate();

	minui_draw_main(menu, sel);
	while (in_menu_wait_any(NULL, 50) & (PBTN_MOK | PBTN_MBACK | PBTN_MENU))
		;

	for (;;) {
		minui_draw_main(menu, sel);
		inp = in_menu_wait(PBTN_UP | PBTN_DOWN | PBTN_LEFT | PBTN_RIGHT | PBTN_MOK |
		                   PBTN_MBACK | PBTN_MENU, NULL, 70);
		if (inp & (PBTN_MENU | PBTN_MBACK))
			break;
		if (inp & PBTN_UP) {
			do { sel--; if (sel < 0) sel = smax; }
			while (!menu[sel].enabled || !menu[sel].selectable);
		}
		if (inp & PBTN_DOWN) {
			do { sel++; if (sel > smax) sel = 0; }
			while (!menu[sel].enabled || !menu[sel].selectable);
		}
		if ((inp & (PBTN_LEFT | PBTN_RIGHT)) && MAIN_SLOT_ROW(menu[sel].id)) {
			state_slot += (inp & PBTN_RIGHT) ? 1 : STATE_SLOT_COUNT - 1;
			state_slot %= STATE_SLOT_COUNT;
		}
		if (inp & PBTN_MOK) {
			int id = menu[sel].id;
			if (MAIN_SLOT_ROW(id)) {
				/* Save or load the shown slot, then back to the game. */
				char msg[64];
				int loading = id == MA_MAIN_LOAD_STATE;
				if (loading && !(state_slot_flags & (1 << state_slot)))
					continue;   /* nothing to load */
				if (emu_save_load_game(loading, 0)) {
					snprintf(msg, sizeof msg, "%s", loading ? tr_or("pico.toast.load_failed", "Load failed")
					                                        : tr_or("pico.toast.save_failed", "Save failed"));
					pa_toast_later(msg);
					state_check_slots();
					slot_shot_invalidate();
					continue;
				}
				snprintf(msg, sizeof msg, loading ? tr_or("pico.toast.loaded", "Loaded slot %d")
				                                  : tr_or("pico.toast.saved", "Saved to slot %d"),
				         state_slot + 1);
				pa_toast_later(msg);
				ret = 1;
				break;
			}
			if (menu[sel].handler) {
				ret = menu[sel].handler(menu[sel].id, inp);
				if (ret) break;
				smax = me_count(menu) - 1;   /* enables may have changed */
			}
		}
	}
	*menu_sel = sel;
	return ret;
}

void menu_loop(void)
{
	static int sel = 0;
	bool needs_disc_ctrl = disc_get_count() > 1;
	// const struct core_override *override = get_overrides();

	plat_video_menu_enter(1);

	me_enable(e_menu_options, MA_OPT_CORE_OPTS, core_options.visible_len > 0);

	me_enable(e_menu_main, MA_MAIN_SAVE_STATE, state_allowed());
	me_enable(e_menu_main, MA_MAIN_LOAD_STATE, state_allowed());
	me_enable(e_menu_main, MA_MAIN_CHEATS, cheats != NULL);
	
	me_enable(e_menu_main, MA_MAIN_DISC_CTRL, needs_disc_ctrl);

	me_enable(e_menu_main, MA_MAIN_CONTENT_SEL, false);

	// if (override)
	// 	me_enable(e_menu_main, MA_MAIN_CONTENT_SEL, !override->block_load_content);

#ifdef MMENU
	if (state_allowed()) {
		me_enable(e_menu_main, MA_MAIN_SAVE_STATE, mmenu == NULL);
		me_enable(e_menu_main, MA_MAIN_LOAD_STATE, mmenu == NULL);
	}
#endif
	minui_main_loop(e_menu_main, &sel);

	/* wait until menu, ok, back is released */
	while (in_menu_wait_any(NULL, 50) & (PBTN_MENU|PBTN_MOK|PBTN_MBACK))
		;

	/* Force the hud to clear */
	plat_video_set_msg(NULL, 0, 0);
	plat_video_menu_leave();
}

int menu_init(void)
{
	menu_init_base();
	i18n_init_from_settings();
	menu_localize();

	/* Load FrogUI's TTF for the menu (no-op + bitmap fallback if unavailable),
	 * then the OnionOS theme's font when FrogUI passed one in skin.txt. */
	menu_font_init((float)MENU_TTF_PX);
	dbg_log("DBG menu: ttf=%d skin font='%s' onion_theme off=%d\n", menu_font_ready(),
	        menu_skin_font, frogui_setting_is("onion_theme", "off"));
	if (menu_font_ready() && !frogui_setting_is("onion_theme", "off")) {
		/* The theme's font from skin.txt, else ShallotsOS's own Exo 2. */
		const char *faces[] = { menu_skin_font,
		                        "/mnt/sdcard/frogui/onion/fonts/Exo-2-Bold-Italic.ttf" };
		int ok = 0;
		for (int i = 0; i < 2 && !ok; i++) {
			if (!faces[i][0]) continue;
			ok = menu_font_use_face(faces[i]);
			dbg_log("DBG menu: font %s %s\n", faces[i], ok ? "loaded" : "failed");
		}
	}

	return 0;
}

int menu_buffers_init(void)
{
	if (g_menubg_src_ptr && g_menubg_ptr) return 0;
	g_menubg_src_ptr = calloc(g_menubg_src_pp * g_menubg_src_h, sizeof(uint16_t));
	g_menubg_ptr = calloc(g_menuscreen_h * g_menuscreen_pp, sizeof(uint16_t));
	if (g_menubg_src_ptr == NULL || g_menubg_ptr == NULL) {
		fprintf(stderr, "OOM\n");
		menu_buffers_free();
		return -1;
	}
	return 0;
}

void menu_buffers_free(void)
{
	if (g_menubg_src_ptr) {
		free(g_menubg_src_ptr);
		g_menubg_src_ptr = NULL;
	}

	if (g_menubg_ptr) {
		free(g_menubg_ptr);
		g_menubg_ptr = NULL;
	}
}

void menu_finish(void)
{
	menu_buffers_free();
}

static void debug_menu_loop(void)
{
}

void menu_update_msg(const char *msg)
{
	strncpy(menu_error_msg, msg, sizeof(menu_error_msg));
	menu_error_msg[sizeof(menu_error_msg) - 1] = 0;

	menu_error_time = plat_get_ticks_ms();
	PA_INFO("%s\n", menu_error_msg);
}
