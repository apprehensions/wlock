/* color format is 0xRRGGBBAA */
static Clr colorname[4] = {
	[INIT]      = { 0x00000000, 0x00000000, 0x00000000 }, /* after initialization */
	[INPUT]     = { 0x00000000, 0x55555555, 0x77777777 }, /* during input */
	[INPUT_ALT] = { 0x00000000, 0x50505050, 0x70707070 }, /* during input, second color */
	[FAILED]    = { 0xcccccccc, 0x33333333, 0x33333333 }, /* wrong password */
};

/* treat a cleared input like a wrong password (color) */
static const int failonclear = 1;
