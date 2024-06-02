#define _BSD_SOURCE
#include <crypt.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <shadow.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "ext-session-lock-v1-protocol.h"
#include "single-pixel-buffer-v1-protocol.h"
#include "viewporter-protocol.h"

#define LENGTH(X) (sizeof(X) / sizeof((X)[0]))

typedef struct {
	unsigned int r, g, b;
} Clr;

static struct {
	struct wl_keyboard *keyboard;
	struct xkb_context *context;
	struct xkb_keymap *keymap;
	struct xkb_state *state;

	int repeat_timer;
	int repeat_delay;
	int repeat_period;
	enum wl_keyboard_key_state repeat_key_state;
	xkb_keysym_t repeat_sym;
} *keyboard;

static struct {
	char input[256];
	int len;
} pw;

typedef struct {
	uint32_t wl_name;
	struct wl_output *wl_output;
	struct wl_surface *surface;
	struct ext_session_lock_surface_v1 *lock_surface;
	struct wp_viewport *viewport;

	const char *name;
	int32_t scale;
	int32_t width, height;
	bool created;

	struct wl_list link;
} Output;

static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_compositor *compositor;
static struct wl_seat *seat;
static struct wp_viewporter *viewporter;
static struct ext_session_lock_v1 *lock;
static struct ext_session_lock_manager_v1 *lock_manager;
static struct wp_single_pixel_buffer_manager_v1 *buf_manager;
static struct wl_list output_list;

static char *hash;

static bool locked, running;

enum state { INIT, INPUT, FAILED } state = INIT;

static Clr colorname[3] = {
	[INIT]   = { 0x00000000, 0x00000000, 0x00000000 }, /* after initialization */
	[INPUT]  = { 0x00000000, 0x55555555, 0x77777777 }, /* during input */
	[FAILED] = { 0xcccccccc, 0x33333333, 0x33333333 }, /* wrong password */
};

static void
noop()
{
	// :3c
}

static Clr
parse_clr(const char *color)
{
	int len;
	unsigned int res;

	len = strlen(color);
	if (len != 6)
		errx(EXIT_FAILURE, "invalid color given: %s", color);

	res = strtoul(color, NULL, 16);

	Clr c = {
		((res >> 16) & 0xff) * (0xffffffff / 0xff),
		((res >> 8) & 0xff) * (0xffffffff / 0xff),
		((res >> 0) & 0xff) * (0xffffffff / 0xff),
	};

	return c;
}

static void
output_frame(Output *output)
{
	Clr c = colorname[state];
	struct wl_buffer *buffer;
	struct wl_region *opaque = wl_compositor_create_region(compositor);

	/* alpha has no effect on this surface */
	buffer = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(
		buf_manager, c.r, c.g, c.b, 0xffffffff);

	wl_surface_set_buffer_scale(output->surface, output->scale);

	wl_surface_attach(output->surface, buffer, 0, 0);
	wl_surface_damage_buffer(output->surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_region_add(opaque, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_set_opaque_region(output->surface, opaque);
	wp_viewport_set_destination(output->viewport, output->width, output->height);
	wl_surface_commit(output->surface);

	wl_buffer_destroy(buffer);
}

static void
outputs_frame(void)
{
	Output *output;

	wl_list_for_each(output, &output_list, link)
		output_frame(output);
}


static void
lock_surface_configure(void *data,
		struct ext_session_lock_surface_v1 *lock_surface,
		uint32_t serial, uint32_t width, uint32_t height)
{
	Output *output = data;
	output->width = width;
	output->height = height;
	ext_session_lock_surface_v1_ack_configure(lock_surface, serial);
	output_frame(output);
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
	.configure = lock_surface_configure,
};

static void
output_create(Output *output)
{
	output->surface = wl_compositor_create_surface(compositor);
	if (!output->surface)
		errx(EXIT_FAILURE, "no compositor surface given");

	output->lock_surface = ext_session_lock_v1_get_lock_surface(lock, output->surface, output->wl_output);
	if (!output->lock_surface)
		errx(EXIT_FAILURE, "no lock surface surface given");
	ext_session_lock_surface_v1_add_listener(output->lock_surface, &lock_surface_listener, output);

	output->viewport = wp_viewporter_get_viewport(viewporter, output->surface);

	output->created = true;
}

static void
output_geometry(void *data, struct wl_output *wl_output,
		int32_t x, int32_t y, int32_t width_mm, int32_t height_mm,
		int32_t subpixel, const char *make, const char *model,
		int32_t transform)
{
	Output *output = data;
	if (running)
		output_frame(output);
}

static void
output_done(void *data, struct wl_output *wl_output)
{
	Output *output = data;
	if (!output->created && running)
		output_create(output);
}

static void
output_scale(void *data, struct wl_output *wl_output, int32_t scale)
{
	Output *output = data;
	output->scale = scale;
	if (running)
		output_frame(output);
}

static void
output_name(void *data, struct wl_output *wl_output, const char *name)
{
	Output *output = data;
	output->name = strdup(name);
}

static void
output_destroy(Output *output)
{
	wl_list_remove(&output->link);
	if (output->lock_surface != NULL)
		ext_session_lock_surface_v1_destroy(output->lock_surface);
	if (output->surface != NULL)
		wl_surface_destroy(output->surface);
	if (output->viewport != NULL)
		wp_viewport_destroy(output->viewport);
	wl_output_release(output->wl_output);
	free(output);
}

static const struct wl_output_listener output_listener = {
	.geometry = output_geometry,
	.mode = noop,
	.done = output_done,
	.scale = output_scale,
	.name = output_name,
	.description = noop,
};

static void
keyboard_keypress(enum wl_keyboard_key_state key_state,
		xkb_keysym_t sym)
{
	int n;
	char buf[8], *inputhash;

	if (key_state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	switch (sym) {
	case XKB_KEY_KP_Enter:
	case XKB_KEY_Return:
		pw.input[pw.len] = '\0';
		errno = 0;
		if (!(inputhash = crypt(pw.input, hash)))
			warn("crypt:");
		else
			running = !!strcmp(inputhash, hash);
		if (running)
			state = FAILED;
		explicit_bzero(&pw.input, sizeof(pw.input));
		pw.len = 0;
		break;
	case XKB_KEY_BackSpace:
		if (pw.len)
			pw.input[--pw.len] = '\0';
		break;
	case XKB_KEY_Escape:
		explicit_bzero(&pw.input, sizeof(pw.input));
		pw.len = 0;
		break;
	default:
		state = INPUT;
		if (!xkb_keysym_to_utf8(sym, buf, 8))
			break;
		n = strnlen(buf, 8);
		if (pw.len + n < 256) {
			memcpy(pw.input + pw.len, buf, n);
			pw.len += n;
		}
		break;
	}

	outputs_frame();
}

static void
keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size)
{
	char *map_shm;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
		errx(EXIT_FAILURE, "unknown keymap %d", format);

	map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (map_shm == MAP_FAILED)
		errx(EXIT_FAILURE, "keymap shm");

	keyboard->keymap = xkb_keymap_new_from_string(keyboard->context,
		map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	munmap(map_shm, size);
	close(fd);

	keyboard->state = xkb_state_new(keyboard->keymap);
}

static void
keyboard_repeat(void)
{
	struct itimerspec spec = { 0 };

	keyboard_keypress(keyboard->repeat_key_state, keyboard->repeat_sym);
	spec.it_value.tv_sec = keyboard->repeat_period / 1000;
	spec.it_value.tv_nsec = (keyboard->repeat_period % 1000) * 1000000l;
	timerfd_settime(keyboard->repeat_timer, 0, &spec, NULL);
}

static void
keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t _key_state)
{
	struct itimerspec spec = { 0 };
	enum wl_keyboard_key_state key_state = _key_state;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(keyboard->state, key + 8);

	keyboard_keypress(key_state, sym);

	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED && keyboard->repeat_period >= 0) {
		keyboard->repeat_key_state = key_state;
		keyboard->repeat_sym = sym;

		spec.it_value.tv_sec = keyboard->repeat_delay / 1000;
		spec.it_value.tv_nsec = (keyboard->repeat_delay % 1000) * 1000000l;
	}

	timerfd_settime(keyboard->repeat_timer, 0, &spec, NULL);
}

static void
keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed,
		uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
	xkb_state_update_mask(keyboard->state, mods_depressed, mods_latched,
			mods_locked, 0, 0, group);
}

static void
keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
               int32_t rate, int32_t delay)
{
	keyboard->repeat_delay = delay;
	keyboard->repeat_period = rate >= 0 ? 1000 / rate : -1;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = noop,
	.leave = noop,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

static void
seat_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps)
{
	if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD))
		return;

	keyboard = calloc(1, sizeof(*keyboard));
	keyboard->keyboard = wl_seat_get_keyboard(wl_seat);
	if (!(keyboard->context = xkb_context_new(XKB_CONTEXT_NO_FLAGS)))
		errx(EXIT_FAILURE, "xkb_context_new failed");
	if ((keyboard->repeat_timer = timerfd_create(CLOCK_MONOTONIC, 0)) < 0)
		err(EXIT_FAILURE, NULL);
	wl_keyboard_add_listener(keyboard->keyboard, &keyboard_listener, NULL);
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = noop,
};

static void
registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version)
{
	if (!strcmp(interface, wl_compositor_interface.name))
		compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	else if (!strcmp(interface, wp_single_pixel_buffer_manager_v1_interface.name))
		buf_manager = wl_registry_bind(registry, name, &wp_single_pixel_buffer_manager_v1_interface, 1);
	else if (!strcmp(interface, wp_viewporter_interface.name))
		viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
	else if (!strcmp(interface, ext_session_lock_manager_v1_interface.name))
		lock_manager = wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, 1);
	else if (strcmp(interface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(registry, name, &wl_seat_interface, 4);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	}
	else if (!strcmp(interface, wl_output_interface.name)) {
		Output *output = calloc(1, sizeof(Output));
		output->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 4);
		output->wl_name = name;
		wl_output_add_listener(output->wl_output, &output_listener, output);
		wl_list_insert(&output_list, &output->link);
	}
}

static void
registry_global_remove(void *data,
		struct wl_registry *registry, uint32_t name)
{
	Output *output, *tmp;

	wl_list_for_each_safe(output, tmp, &output_list, link) {
		if (output->wl_name == name) {
			output_destroy(output);
			break;
		}
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void
lock_locked(void *data, struct ext_session_lock_v1 *lock)
{
	locked = true;
}

static void
lock_finished(void *data, struct ext_session_lock_v1 *lock)
{
	abort();
}

static const struct ext_session_lock_v1_listener lock_listener = {
	.locked = lock_locked,
	.finished = lock_finished,
};

static void
usage(void)
{
	fprintf(stderr, "usage: wlock [-hv] [-c init_color] [-f fail_color] [-i input_color]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int opt;
	struct spwd *sp;
	struct passwd *p;

	while ((opt = getopt(argc, argv, "c:f:i:hv")) != -1) {
		switch (opt) {
		case 'c':
		case 'f':
		case 'i':
			colorname[opt == 'f' ? FAILED : opt == 'i' ? INPUT : INIT] = parse_clr(optarg);
			break;
		case 'v':
			puts("wlock " VERSION);
			return EXIT_SUCCESS;
		case 'h':
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	if (!(p = getpwuid(getuid())))
		err(EXIT_FAILURE, NULL);
	hash = p->pw_passwd;
	if (!strcmp(hash, "x")) {
		if (!(sp = getspnam(p->pw_name)))
			errx(EXIT_FAILURE, "getspnam failed, ensure suid & sgid lock");
		hash = sp->sp_pwdp;
	}

	if (setgid(getgid()) != 0)
		err(EXIT_FAILURE, NULL);
	if (setuid(getuid()) != 0)
		err(EXIT_FAILURE, NULL);
	if (setuid(0) > 0 || setgid(0) > 0)
		errx(EXIT_FAILURE, "failed to drop root (able to restore root)");

	wl_list_init(&output_list);

	display = wl_display_connect(NULL);
	if (!display)
		errx(EXIT_FAILURE, "wayland display connect failed");
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	if (wl_display_roundtrip(display) < 0)
		errx(EXIT_FAILURE, "roundtrip failed");

	if (!compositor || !lock_manager)
		errx(EXIT_FAILURE, "compositor missing wl_compositor or ext-session-lock-v1");

	lock = ext_session_lock_manager_v1_lock(lock_manager);
	ext_session_lock_v1_add_listener(lock, &lock_listener, NULL);

	if (wl_display_roundtrip(display) < 0)
		return EXIT_FAILURE;

	Output *output;
	wl_list_for_each(output, &output_list, link)
		output_create(output);

	while (!locked)
		if (wl_display_roundtrip(display) < 0)
			return EXIT_FAILURE;

	struct pollfd fds[] = {
		{ wl_display_get_fd(display), POLLIN, 0 },
		{ keyboard->repeat_timer,     POLLIN, 0 },
	};

	running = true;
	while (running) {
		if (wl_display_flush(display) < 0 && errno != EAGAIN)
			break;

		errno = 0;
		if (poll(fds, 2, -1) == -1 && errno != EINTR)
			err(EXIT_FAILURE, NULL);

		if (fds[0].revents & POLLIN)
			if (wl_display_dispatch(display) < 0)
				running = false;
		if (fds[0].revents & POLLHUP || fds[0].revents & POLLERR)
			errx(EXIT_FAILURE, "wayland socket disconnect");

		if (fds[1].revents & POLLIN)
			keyboard_repeat();
	}

	ext_session_lock_v1_unlock_and_destroy(lock);
	wl_display_roundtrip(display);

	return EXIT_SUCCESS;
}
