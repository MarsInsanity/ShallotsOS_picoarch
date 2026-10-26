#include "overrides.h"

/* HCSNES (github.com/MarsInsanity/HCSNES), Snes9x 2005-based. Default to its
 * Timed frameskip: draw unless the frame starts late, like the stock
 * firmware's frontend does. Uses snes9x2005's controls (same core options). */
static const struct core_override_option hcsnes_core_option_overrides[] = {
	{
		.key = "snes9x_2005_frameskip",
		.default_value = "timed"
	},
	{
		.key = "snes9x_2005_frameskip_interval",
		.default_value = "2"
	},
	{ NULL }
};

#define hcsnes_overrides(name) {                            \
	.core_name = name,                                  \
	.fast_forward = &snes9x2005_fast_forward,           \
	.actions = snes9x2005_ctrl_actions,                 \
	.action_size = array_size(snes9x2005_ctrl_actions), \
	.options = hcsnes_core_option_overrides             \
}
