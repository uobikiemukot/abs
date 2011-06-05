/*
 * The initial "port" of dwm to curses was done by
 * (c) 2007-2009 Marc Andre Tanner <mat at brain-dump dot org>
 *
 * It is highly inspired by the original X11 dwm and
 * reuses some code of it which is mostly
 *
 * (c) 2006-2007 Anselm R. Garbe <garbeam at gmail dot com>
 *
 * See LICENSE for details.
 */

#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <ncurses.h>
#include <stdio.h>
#include <signal.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#ifdef __CYGWIN__
# include <termios.h>
#endif
#include "madtty.h"

typedef struct {
	const char *symbol;
	void (*arrange)(void);
} Layout;

typedef struct Client Client;
struct Client {
	WINDOW *window;
	madtty_t *term;
	const char *cmd;
	char title[256];
	uint8_t order;
	pid_t pid;
	int pty;
#ifdef CONFIG_CMDFIFO
	unsigned short int id;
#endif
	short int x;
	short int y;
	short int w;
	short int h;
	bool minimized;
	bool died;
	Client *next;
	Client *prev;
};

typedef struct Frame Frame;
struct Frame {
	Client *clients;
	Client *sel;
};

#define ALT(k)      ((k) + (161 - 'a'))
#ifndef CTRL
  #define CTRL(k)   ((k) & 0x1F)
#endif
#define CTRL_ALT(k) ((k) + (129 - 'a'))

#define MAX_ARGS 2

typedef struct {
	void (*cmd)(const char *args[]);
	/* needed to avoid an error about initialization
	 * of nested flexible array members */
	const char *args[MAX_ARGS + 1];
} Action;

typedef struct {
	unsigned int mod;
	unsigned int code;
	Action action;
} Key;

#ifdef CONFIG_MOUSE
typedef struct {
	mmask_t mask;
	Action action;
} Button;
#endif

#ifdef CONFIG_CMDFIFO
typedef struct {
	const char *name;
	Action action;
} Cmd;
#endif

#ifdef CONFIG_STATUSBAR
enum { BarTop, BarBot, BarOff };
#endif

#define countof(arr) (sizeof(arr) / sizeof((arr)[0]))
#define sstrlen(str) (sizeof(str) - 1)
#define max(x, y) ((x) > (y) ? (x) : (y))
#define min(x, y) ((x) < (y) ? (x) : (y))
#define clamp(m, low, high) (max(min(m, high), low))

#ifdef NDEBUG
 #define debug(format, args...)
#else
 #define debug eprint
#endif

/* commands for use by keybindings */
static void quit(const char *args[]);
static void create(const char *args[]);
static void create_in_dir(const char *args[]);
static void startup(const char *args[]);
static void escapekey(const char *args[]);
static void killclient(const char *args[]);
static void focusn(const char *args[]);
static void focusnext(const char *args[]);
static void focusnextnm(const char *args[]);
static void focusprev(const char *args[]);
static void focusprevnm(const char *args[]);
static void togglebell(const char *key[]);
static void toggleminimize(const char *args[]);
static void setmwfact(const char *args[]);
static void setmwcol(const char *args[]);
static void setlayout(const char *args[]);
static void scrollback(const char *args[]);
static void redraw(const char *args[]);
static void zoom(const char *args[]);
static void lock(const char *key[]);
static void togglerunall(const char *args[]);

#ifdef CONFIG_STATUSBAR
enum { ALIGN_LEFT, ALIGN_RIGHT };
static void togglebar(const char *args[]);
#endif

#ifdef CONFIG_MOUSE
static void mouse_focus(const char *args[]);
static void mouse_fullscreen(const char *args[]);
static void mouse_minimize(const char *args[]);
static void mouse_zoom(const char *args[]);
static void mouse_toggle();
#endif

static void clear_workspace();
static void draw_all(bool border);
static void draw_border(Client *c, int frame);
static void draw_content(Client *c, int frame);
static void resize(Client *c, int x, int y, int w, int h);
static void resize_screen();
static void eprint(const char *errstr, ...);
static bool isarrange(void (*func)());
static void arrange();
static void focus(Client *c);
static void focus_frame(int frame);
static void keypress(int code);
static void create_with_env(const char *args[], const char *key, const char *val);

static unsigned int waw, wah, wax, way;
extern double mwfact;

static Frame frames[3] = {{NULL, NULL}, {NULL, NULL}, {NULL, NULL}};
static int cur_frame = 0;
static int nframes = 2;

static void detach(Client *c, int frame);
static void attachafter(Client *c, Client *a);
static void attach(Client *c);

#include "config.h"

double mwfact = MWFACT;
static Layout *layout = layouts;
static const char *shell;
static bool need_screen_resize;
static int width, height, scroll_buf_size = SCROLL_BUF_SIZE;
static bool running = true;
static bool runinall = false;

#ifdef CONFIG_MOUSE
# include "mouse.c"
#endif

#ifdef CONFIG_CMDFIFO
# include "cmdfifo.c"
#endif

#ifdef CONFIG_STATUSBAR
# include "statusbar.c"
#endif

static void
eprint(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
}

static void
error(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static void
attach(Client *c) {
	uint8_t order;
	if (frames[cur_frame].clients)
		frames[cur_frame].clients->prev = c;
	c->next = frames[cur_frame].clients;
	c->prev = NULL;
	frames[cur_frame].clients = c;
	for (order = 1; c; c = c->next, order++)
		c->order = order;
}

static void
attachafter(Client *c, Client *a) { /* attach c after a */
	uint8_t o;
	if (c == a)
		return;
	if (!a)
		for (a = frames[cur_frame].clients; a && a->next; a = a->next);

	if (a) {
		if (a->next)
			a->next->prev = c;
		c->next = a->next;
		c->prev = a;
		a->next = c;
		for (o = a->order; c; c = c->next)
			c->order = ++o;
	} else {
		attach(c);
	}
}

static void
detach(Client *c, int frame) {
	Client *d;
	if (c->prev)
		c->prev->next = c->next;
	if (c->next) {
		c->next->prev = c->prev;
		for (d = c->next; d; d = d->next)
			--d->order;
	}
	if (c == frames[frame].clients)
		frames[frame].clients = c->next;
	if (c == frames[frame].sel)
		frames[frame].sel = NULL;
	c->next = c->prev = NULL;
}

static void
arrange() {
	clear_workspace();
	layout->arrange();
	wnoutrefresh(stdscr);
	draw_all(true);
}

static bool
isarrange(void (*func)()) {
	return func == layout->arrange;
}

static void
focus_frame(int end_frame) {
	if (!isarrange(fullscreen) && !isarrange(split) && !isarrange(split3)) {
		return;
	}

	Client *cur_focus = frames[cur_frame].sel;
	int start_frame = cur_frame;
	cur_frame = end_frame;

	if (cur_focus) {
		draw_border(cur_focus, start_frame);
		wrefresh(cur_focus->window);
	}

	Client *dest_focus = frames[cur_frame].sel;
	if (dest_focus) {
		draw_border(dest_focus, cur_frame);
		wrefresh(dest_focus->window);
	}
}

static void
focus(Client *c) {
	if (frames[cur_frame].sel == c)
		return;
	frames[cur_frame].sel = c;
	if (isarrange(fullscreen) || isarrange(split) || isarrange(split3))
		redrawwin(c->window);
	draw_border(c, cur_frame);
	wrefresh(c->window);
}

static void
focusn(const char *args[]) {
	Client *c;

	for (c = frames[cur_frame].clients; c; c = c->next) {
		if (c->order == atoi(args[cur_frame])) {
			focus(c);
			if (c->minimized)
				toggleminimize(NULL);
			return;
		}
	}
}

static void
focusnext(const char *args[]) {
	Client *c = frames[cur_frame].sel;

	if (!c)
		return;

	c = c->next;
	if (!c)
		c = frames[cur_frame].clients;
	if (c)
		focus(c);
}

static void
focusnextnm(const char *args[]) {
	Client *c = frames[cur_frame].sel;

	if (!c)
		return;

	do {
		c = c->next;
		if (!c)
			c = frames[cur_frame].clients;
	} while (c->minimized && c != frames[cur_frame].sel);
	focus(c);
}

static void
focusprev(const char *args[]) {
	Client *c;

	if (!frames[cur_frame].sel)
		return;
	c = frames[cur_frame].sel->prev;
	if (!c)
		for (c = frames[cur_frame].clients; c && c->next; c = c->next);
	if (c)
		focus(c);
}

static void
focusprevnm(const char *args[]) {
	Client *c;

	if (!frames[cur_frame].sel)
		return;
	c = frames[cur_frame].sel;
	do {
		c = c->prev;
		if (!c)
			for (c = frames[cur_frame].clients; c && c->next; c = c->next);
	} while (c->minimized && c != frames[cur_frame].sel);
	focus(c);
}

static void
zoom(const char *args[]) {
	Client *c = frames[cur_frame].sel;

	if (!c)
		return;
	if (c == frames[cur_frame].clients)
		if (!(c = c->next))
			return;
	detach(c, cur_frame);
	attach(c);
	focus(c);
	if (c->minimized)
		toggleminimize(NULL);
	arrange();
}

static void
togglebell(const char *args[]) {
	madtty_togglebell(frames[cur_frame].sel->term);
}

static void
toggleminimize(const char *args[]) {
	Client *c, *m;
	unsigned int n;
	if (!frames[cur_frame].sel)
		return;
	/* the last window can't be minimized */
	if (!frames[cur_frame].sel->minimized) {
		for (n = 0, c = frames[cur_frame].clients; c; c = c->next)
			if (!c->minimized)
				n++;
		if (n == 1)
			return;
	}
	frames[cur_frame].sel->minimized = !frames[cur_frame].sel->minimized;
	m = frames[cur_frame].sel;
	/* check whether the master client was minimized */
	if (frames[cur_frame].sel == frames[cur_frame].clients && frames[cur_frame].sel->minimized) {
		c = frames[cur_frame].sel->next;
		detach(c, cur_frame);
		attach(c);
		focus(c);
		detach(m, cur_frame);
		for (; c && c->next && !c->next->minimized; c = c->next);
		attachafter(m, c);
	} else if (m->minimized) {
		/* non master window got minimized move it above all other
		 * minimized ones */
		focusnextnm(NULL);
		detach(m, cur_frame);
		for (c = frames[cur_frame].clients; c && c->next && !c->next->minimized; c = c->next);
		attachafter(m, c);
	} else { /* window is no longer minimized, move it to the master area */
		madtty_dirty(m->term);
		detach(m, cur_frame);
		attach(m);
	}
	arrange();
}

static void
transition()
{
	Client *c;
	int cur_nframes = nframes;
	layout->arrange();
	int dest = nframes - 1;
	focus_frame(dest);
	for(; cur_nframes > nframes; cur_nframes--) {
		int idx = cur_nframes - 1;
		while(frames[idx].clients) {
			c = frames[idx].clients;
			detach(c, idx);
			attachafter(c, NULL);
		}
	}
}

static void
setlayout(const char *args[]) {
	unsigned int i;

	if (!args || !args[0]) {
		if (++layout == &layouts[countof(layouts)])
			layout = &layouts[0];
	} else {
		for (i = 0; i < countof(layouts); i++)
			if (!strcmp(args[0], layouts[i].symbol))
				break;
		if (i == countof(layouts))
			return;
		layout = &layouts[i];
	}
	transition();
	arrange();
}

static void
setmwcol(const char *args[]) {
	int cols;

	if (isarrange(fullscreen) || isarrange(grid))
		return;
	/* arg handling, manipulate mwfact */
	if (args[0] == NULL)
		mwfact = MWFACT;
	else if (1 == sscanf(args[0], "%d", &cols)) {
		if (args[0][0] == '+' || args[0][0] == '-') {
			cols += mwfact * waw;
		}

		mwfact = ((double)cols) / waw;
		mwfact = clamp(mwfact, 0.1, 0.9);
	}
	arrange();
}

static void
setmwfact(const char *args[]) {
	double delta;

	if (isarrange(fullscreen) || isarrange(grid))
		return;
	/* arg handling, manipulate mwfact */
	if (args[0] == NULL)
		mwfact = MWFACT;
	else if (1 == sscanf(args[0], "%lf", &delta)) {
		if (args[0][0] == '+' || args[0][0] == '-')
			mwfact += delta;
		else
			mwfact = delta;

		mwfact = clamp(mwfact, 0.1, 0.9);
	}
	arrange();
}

static void
scrollback(const char *args[]) {
	Client *c = frames[cur_frame].sel;
	if (!c)
		return;

	if (!args[0] || atoi(args[0]) < 0)
		madtty_scroll(c->term, -c->h/2);
	else
		madtty_scroll(c->term,  c->h/2);

	draw_content(c, cur_frame);
	wrefresh(c->window);
}

static void
redraw(const char *args[]) {
	wrefresh(curscr);
	resize_screen();
	draw_all(true);
}

static void
draw_border(Client *c, int frame) {
	int x, y, cutoff;
	if (cur_frame == frame) {
		wattrset(c->window, ACTIVE_ATTR);
		madtty_color_set(c->window, ACTIVE_FG, ACTIVE_BG);
	} else {
		wattrset(c->window, NORMAL_ATTR);
		madtty_color_set(c->window, NORMAL_FG, NORMAL_BG);
	}
	getyx(c->window, y, x);
	curs_set(0);
	mvwhline(c->window, 0, 0, ACS_HLINE, c->w);
	cutoff = c->w - (4 + sstrlen(TITLE) - 5  + sstrlen(SEPARATOR));
	int i = 0;
	for(Client *cur = frames[frame].clients; cur; cur = cur->next) {
		cutoff = clamp(cutoff, 0, min(8, c->w - 4 - i));
		if(frame == cur_frame) {
			if(c == cur) {
				wattrset(c->window, SELECTED_ATTR);
				madtty_color_set(c->window, SELECTED_FG, SELECTED_BG);
			} else {
				wattrset(c->window, ACTIVE_ATTR);
				madtty_color_set(c->window, ACTIVE_FG, ACTIVE_BG);
			}
		} else {
			wattrset(c->window, NORMAL_ATTR);
			madtty_color_set(c->window, NORMAL_FG, NORMAL_BG);
		}
		mvwprintw(c->window, 0, 1 + i, TITLE,
			  cur->order,
			  *cur->title ? SEPARATOR : "",
			  cutoff,
			  *cur->title ? cur->title : ""
			  );
		i += 12;
	}
	wmove(c->window, y, x);
	if (!c->minimized)
		curs_set(madtty_cursor(c->term));
}

static void
draw_content(Client *c, int frame) {
	if (!c->minimized || isarrange(fullscreen) || isarrange(split) || isarrange(split3)) {
		madtty_draw(c->term, c->window, 1, 0);
		if (frame != cur_frame || c != frames[cur_frame].sel)
			curs_set(0);
	}
}

static void
clear_workspace() {
	unsigned int y;
	for (y = 0; y < wah; y++)
		mvhline(way + y, 0, ' ', waw);
	wnoutrefresh(stdscr);
}

static void
draw_all(bool border) {
	int f;
	Client *c;
	curs_set(0);
	for (f = 0; f < nframes; f++) {
		c = frames[f].sel;
		if (!c)
			continue;
		redrawwin(c->window);
		if (f == cur_frame && c == frames[f].sel) {
			continue;
		}
		draw_content(c, f);
		if (border && c == frames[f].sel)
			draw_border(c, f);
		wnoutrefresh(c->window);
	}
	/* as a last step the selected window is redrawn,
	 * this has the effect that the cursor position is
	 * accurate
	 */
	refresh();
	c = frames[cur_frame].sel;
	if (c) {
		draw_content(c, cur_frame);
		if (border)
			draw_border(c, cur_frame);
		wrefresh(c->window);
	}
}

static void
escapekey(const char *args[]) {
	int key;
	if ((key = getch()) >= 0) {
		debug("escaping key `%c'\n", key);
		keypress(CTRL(key));
	}
}

/*
 * Lock the screen until the correct password is entered.
 * The password can either be specified in config.h which is
 * not recommended because `strings dvtm` will contain it. If
 * no password is specified in the configuration file it is read
 * from the keyboard before the screen is locked.
 *
 * NOTE: this function doesn't handle the input from clients. All
 *       foreground operations are temporarily suspended since the
 *       function doesn't return.
 */
static void
lock(const char *args[]) {
	size_t len = 0, i = 0;
	char buf[16], *pass = buf, c;

	erase();
	curs_set(0);

	if (args && args[0]) {
		len = strlen(args[0]);
		pass = (char *)args[0];
	} else {
		mvprintw(LINES / 2, COLS / 2 - 7, "Enter password");
		while (len < sizeof buf && (c = getch()) != '\n')
			if (c != ERR)
				buf[len++] = c;
	}

	mvprintw(LINES / 2, COLS / 2 - 7, "Screen locked!");

	while (i != len) {
		for(i = 0; i < len; i++) {
			if (getch() != pass[i])
				break;
		}
	}

	arrange();
}

static void
togglerunall(const char *args[]) {
	runinall = !runinall;
}

static void
killclient(const char *args[]) {
	if (!frames[cur_frame].sel)
		return;
	debug("killing client with pid: %d\n", frames[cur_frame].sel->pid);
	kill(-frames[cur_frame].sel->pid, SIGKILL);
}

void peek()
{
	Client *cur = frames[cur_frame].sel;

	debug("sel is [%X]\n", cur);
	if(cur) {
		//cur
	}
}

static int
title_escape_seq_handler(madtty_t *term, char *es) {
	Client *c;
	unsigned int l;
	if (es[0] != ']' || (es[1] && (es[1] < '0' || es[1] > '9')) || (es[2] && es[2] != ';'))
		return MADTTY_HANDLER_NOWAY;
	if ((l = strlen(es)) < 3 || es[l - 1] != '\07')
		return MADTTY_HANDLER_NOTYET;
	es[l - 1] = '\0';
	c = (Client *)madtty_get_data(term);
	strncpy(c->title, es + 3, sizeof(c->title));
	draw_border(c, cur_frame);
	debug("window title: %s\n", c->title);
	return MADTTY_HANDLER_OK;
}

static void
create(const char *args[]) {
	create_with_env(args, NULL, NULL);
}

static void
create_with_env(const char *args[], const char *key, const char *val) {
	Client *c = calloc(sizeof(Client), 1);
	if (!c)
		return;
	const char *cmd = (args && args[0]) ? args[0] : shell;
	const char *pargs[] = { "/bin/sh", "-c", cmd, NULL };
#ifdef CONFIG_CMDFIFO
	c->id = ++client_id;
	char buf[8];
	snprintf(buf, sizeof buf, "%d", c->id);
#endif

#ifdef CONFIG_CMDFIFO
#define ENV_LEN 7
#else 
#define ENV_LEN 5
#endif

	const char *env[ENV_LEN] = {
		"DVTM", VERSION,
#ifdef CONFIG_CMDFIFO
		"DVTM_WINDOW_ID", buf,
#endif
		NULL, NULL, NULL
	};

	if(key != NULL) {
		int idx = ENV_LEN - 3;
		env[idx++] = key;
		env[idx++] = val;
	}

	c->window = newwin(wah, waw, way, wax);
	c->term = madtty_create(height - 1, width, scroll_buf_size);
	c->cmd = cmd;
	if (args && args[1])
		strncpy(c->title, args[1], sizeof(c->title));
	c->pid = madtty_forkpty(c->term, "/bin/sh", pargs, env, &c->pty);
	madtty_set_data(c->term, c);
	madtty_set_handler(c->term, title_escape_seq_handler);
	c->w = width;
	c->h = height;
	c->x = wax;
	c->y = way;
	c->order = 0;
	c->minimized = false;
	debug("client with pid %d forked\n", c->pid);
	attachafter(c, NULL);
	focus(c);
	arrange();
}

static void
create_in_dir(const char *args[]) {
	create_with_env(NULL, "DVTM_TO_DIR", args[0]);
}

static void
destroy(Client *c, int frame) {
	int f, qctr = 0;
	if (frames[cur_frame].sel == c)
		focusprevnm(NULL);
	detach(c, frame);
	if (frames[frame].sel == c) {
		if (frames[frame].clients) {
			focus(frames[frame].clients);
			toggleminimize(NULL);
		} else {
			frames[cur_frame].sel = NULL;
		}
	}
	werase(c->window);
	wrefresh(c->window);
	madtty_destroy(c->term);
	delwin(c->window);
	for(f = 0; f < nframes; f++) {
		if (!frames[f].clients && countof(actions)) {
			if (!strcmp(c->cmd, shell)) {
				qctr++;
			} else {
				create(NULL);
				break;
			}
		}
	}
	free(c);
	if(qctr >= nframes) {
		quit(NULL);
	}
	arrange();
}

static void
move_client(Client *c, int x, int y) {
	if (c->x == x && c->y == y)
		return;
	debug("moving, x: %d y: %d\n", x, y);
	if (mvwin(c->window, y, x) == ERR)
		eprint("error moving, x: %d y: %d\n", x, y);
	else {
		c->x = x;
		c->y = y;
	}
}

static void
resize_client(Client *c, int w, int h) {
	if (c->w == w && c->h == h)
		return;
	debug("resizing, w: %d h: %d\n", w, h);
	if (wresize(c->window, h, w) == ERR)
		eprint("error resizing, w: %d h: %d\n", w, h);
	else {
		c->w = w;
		c->h = h;
	}
	madtty_resize(c->term, h - 1, w);
}

static void
resize(Client *c, int x, int y, int w, int h) {
	resize_client(c, w, h);
	move_client(c, x, y);
}

static bool
is_modifier(unsigned int mod) {
	unsigned int i;
	for (i = 0; i < countof(keys); i++) {
		if (keys[i].mod == mod)
			return true;
	}
	return false;
}

static Key*
keybinding(unsigned int mod, unsigned int code) {
	unsigned int i;
	for (i = 0; i < countof(keys); i++) {
		if (keys[i].mod == mod && keys[i].code == code)
			return &keys[i];
	}
	return NULL;
}

static Client*
get_client_by_pid(pid_t pid) {
	Client *c;
	int f;
	for (f = 0; f < nframes; f++) {
		for (c = frames[f].clients; c; c = c->next) {
			if (c->pid == pid)
				return c;
		}
	}
	return NULL;
}

static void
sigchld_handler(int sig) {
	int errsv = errno;
	int status;
	pid_t pid;
	Client *c;

	signal(SIGCHLD, sigchld_handler);

	while ((pid = waitpid(-1, &status, WNOHANG)) != 0) {
		if (pid == -1) {
			if (errno == ECHILD) {
				/* no more child processes */
				break;
			}
			eprint("waitpid: %s\n", strerror(errno));
			break;
		}
		debug("child with pid %d died\n", pid);
		if ((c = get_client_by_pid(pid)))
			c->died = true;
	}

	errno = errsv;
}

static void
sigwinch_handler(int sig) {
	signal(SIGWINCH, sigwinch_handler);
	need_screen_resize = true;
}

static void
sigterm_handler(int sig) {
	running = false;
}

static void
resize_screen() {
	struct winsize ws;

	if (ioctl(0, TIOCGWINSZ, &ws) == -1)
		return;

	width = ws.ws_col;
	height = ws.ws_row;

	debug("resize_screen(), w: %d h: %d\n", width, height);

#if defined(__OpenBSD__) || defined(__NetBSD__)
	resizeterm(height, width);
#else
	resize_term(height, width);
#endif
	wresize(stdscr, height, width);
	wrefresh(curscr);
	refresh();

	waw = width;
	wah = height;
#ifdef CONFIG_STATUSBAR
	updatebarpos();
	drawbar();
#endif
	arrange();
}

static void
startup(const char *args[]) {
	for (int i = 0; i < countof(actions); i++)
		actions[i].cmd(actions[i].args);
}

static void
setup() {
	if (!(shell = getenv("SHELL")))
		shell = "/bin/sh";
	setlocale(LC_CTYPE, "");
	initscr();
	start_color();
	noecho();
	keypad(stdscr, TRUE);
#ifdef CONFIG_MOUSE
	mouse_setup();
#endif
	raw();
	madtty_init_colors();
	madtty_init_vt100_graphics();
	getmaxyx(stdscr, height, width);
	resize_screen();
	signal(SIGWINCH, sigwinch_handler);
	signal(SIGCHLD, sigchld_handler);
	signal(SIGTERM, sigterm_handler);
}

static void
cleanup() {
	clear_workspace();
	endwin();
#ifdef CONFIG_STATUSBAR
	if (statusfd > 0)
		close(statusfd);
#endif
#ifdef CONFIG_CMDFIFO
	if (cmdfd > 0)
		close(cmdfd);
	if (cmdpath)
		unlink(cmdpath);
#endif
}

static void
quit(const char *args[]) {
	cleanup();
	exit(EXIT_SUCCESS);
}

static void
usage() {
	cleanup();
	eprint("usage: dvtm [-v] [-m mod] [-d escdelay] [-h n] "
#ifdef CONFIG_STATUSBAR
		"[-s status-fifo] "
#endif
#ifdef CONFIG_CMDFIFO
		"[-c cmd-fifo] "
#endif
		"[cmd...]\n");
	exit(EXIT_FAILURE);
}

static int
open_or_create_fifo(const char *name) {
	struct stat info;
	int fd;
open:
	if ((fd = open(name, O_RDWR|O_NONBLOCK)) == -1) {
		if (errno == ENOENT && !mkfifo(name, S_IRUSR|S_IWUSR))
			goto open;
		error("%s\n", strerror(errno));
	}
	if (fstat(fd, &info) == -1)
		error("%s\n", strerror(errno));
	if (!S_ISFIFO(info.st_mode))
		error("%s is not a named pipe\n", name);
	return fd;
}

static bool
parse_args(int argc, char *argv[]) {
	int arg;
	bool init = false;

	if (!getenv("ESCDELAY"))
		ESCDELAY = 100;
	for (arg = 1; arg < argc; arg++) {
		if (argv[arg][0] != '-') {
			const char *args[] = { argv[arg], NULL };
			if (!init) {
				setup();
				init = true;
			}
			create(args);
			continue;
		}
		if (argv[arg][1] != 'v' && (arg + 1) >= argc)
			usage();
		switch (argv[arg][1]) {
			case 'v':
				puts("dvtm-"VERSION" (c) 2007-2009 Marc Andre Tanner");
				exit(EXIT_SUCCESS);
			case 'm': {
				char *mod = argv[++arg];
				if (mod[0] == '^' && mod[1])
					*mod = CTRL(mod[1]);
				for (int i = 0; i < countof(keys); i++)
					keys[i].mod = *mod;
				break;
			}
			case 'd':
				ESCDELAY = atoi(argv[++arg]);
				if (ESCDELAY < 50)
					ESCDELAY = 50;
				else if (ESCDELAY > 1000)
					ESCDELAY = 1000;
				break;
			case 'h':
				scroll_buf_size = atoi(argv[++arg]);
				break;
#ifdef CONFIG_STATUSBAR
			case 's':
				statusfd = open_or_create_fifo(argv[++arg]);
				updatebarpos();
				break;
#endif
#ifdef CONFIG_CMDFIFO
			case 'c':
				cmdfd = open_or_create_fifo(argv[++arg]);
				if (!(cmdpath = get_realpath(argv[arg])))
					error("%s\n", strerror(errno));
				setenv("DVTM_CMD_FIFO", cmdpath, 1);
				break;
#endif
			default:
				usage();
		}
	}
	return init;
}

void
keypress(int code) {
	Client *c;
	int len = 1;
	char buf[8] = { '\e' };

	if (code == '\e') {
		/* pass characters following escape to the underlying app */
		nodelay(stdscr, TRUE);
		while (len < sizeof(buf) - 1 && (buf[len] = getch()) != ERR)
			len++;
		buf[len] = '\0';
		nodelay(stdscr, FALSE);
	}

	for (c = runinall ? frames[cur_frame].clients : frames[cur_frame].sel; c; c = c->next) {
		if (!c->minimized || isarrange(fullscreen) || isarrange(split) || isarrange(split3)) {
			if (code == '\e')
				madtty_keypress_sequence(c->term, buf);
			else
				madtty_keypress(c->term, code);
		}
		if (!runinall)
			break;
	}
}

int
main(int argc, char *argv[]) {
	if (!parse_args(argc, argv)) {
		setup();
		startup(NULL);
	}

	while (running) {
		Client *c, *t;
		int r, nfds = 0;
		int f;
		fd_set rd;

		if (need_screen_resize) {
			resize_screen();
			need_screen_resize = false;
		}

		FD_ZERO(&rd);
		FD_SET(STDIN_FILENO, &rd);

#ifdef CONFIG_CMDFIFO
		if (cmdfd != -1) {
			FD_SET(cmdfd, &rd);
			nfds = cmdfd;
		}
#endif
#ifdef CONFIG_STATUSBAR
		if (statusfd != -1) {
			FD_SET(statusfd, &rd);
			nfds = max(nfds, statusfd);
		}
#endif
		for (f = 0; f < nframes; f++) {
			for (c = frames[f].clients; c; ) {
				if (c->died) {
					t = c->next;
					destroy(c, f);
					c = t;
					continue;
				}
				FD_SET(c->pty, &rd);
				nfds = max(nfds, c->pty);
				c = c->next;
			}
		}
		r = select(nfds + 1, &rd, NULL, NULL, NULL);

		if (r == -1 && errno == EINTR)
			continue;

		if (r < 0) {
			perror("select()");
			exit(EXIT_FAILURE);
		}

		if (FD_ISSET(STDIN_FILENO, &rd)) {
			int code = getch();
			Key *key;
			if (code >= 0) {
#ifdef CONFIG_MOUSE
				if (code == KEY_MOUSE) {
					handle_mouse();
				} else
#endif /* CONFIG_MOUSE */
				if (is_modifier(code)) {
					int mod = code;
					code = getch();
					if (code >= 0) {
						if (code == mod)
							keypress(code);
						else if ((key = keybinding(mod, code)))
							key->action.cmd(key->action.args);
					}
				} else if ((key = keybinding(0, code))) {
					key->action.cmd(key->action.args);
				} else {
					keypress(code);
				}
			}
			if (r == 1) /* no data available on pty's */
				continue;
		}

#ifdef CONFIG_CMDFIFO
		if (cmdfd != -1 && FD_ISSET(cmdfd, &rd))
			handle_cmdfifo();
#endif
#ifdef CONFIG_STATUSBAR
		if (statusfd != -1 && FD_ISSET(statusfd, &rd))
			handle_statusbar();
#endif

		for (f = 0; f < nframes; f++) {
			for (c = frames[f].clients; c; ) {
				if (FD_ISSET(c->pty, &rd)) {
					if (madtty_process(c->term) < 0 && errno == EIO) {
						/* client probably terminated */
						t = c->next;
						destroy(c, f);
						c = t;
						continue;
					}
					if (c != frames[f].sel) {
						draw_content(c, f);
						if (!isarrange(fullscreen) && !isarrange(split) && !isarrange(split3))
							wnoutrefresh(c->window);
					} else {
						if(f != cur_frame) {
							draw_content(c, f);
							wnoutrefresh(c->window);
						}
					}
				}
				c = c->next;
			}
		}
		if(frames[cur_frame].sel) {
			draw_content(frames[cur_frame].sel, cur_frame);
			wnoutrefresh(frames[cur_frame].sel->window);
		}
		doupdate();
	}

	cleanup();
	return 0;
}
