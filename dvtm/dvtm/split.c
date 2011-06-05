
static void
split(void) {
	unsigned int nx, ny, nw, nh;
	Client *c;

	nframes = 2;

	// Dimensions for left side
	nx = wax;
	ny = way;
	nw = mwfact * waw;
	nh = wah;

	for(c = frames[0].clients; c; c = c->next) {
		resize(c, nx, ny, nw, nh);
	}

	wattrset(stdscr, NORMAL_ATTR);
	madtty_color_set(stdscr, NORMAL_FG, NORMAL_BG);

	// Middle line
	mvvline(ny, nw, ACS_VLINE, wah);

	// Top default line - window border is over top

	mvhline(ny, nx, ACS_HLINE, waw);
	mvaddch(ny, nw, ACS_TTEE);

	nx += nw + 1;
	nw = waw - nw - 1;

	for(c = frames[1].clients; c; c = c->next) {
		resize(c, nx, ny, nw, nh);
	}
}

static void
split3(void) {
	unsigned int nx, ny, nw, nh;
	Client *c;

	nframes = 3;

	// Dimensions for left side
	nx = wax;
	ny = way;
	nw = mwfact * waw;
	nh = wah;

	for(c = frames[0].clients; c; c = c->next) {
		resize(c, nx, ny, nw, nh);
	}

	// Middle line
	mvvline(ny, nw, ACS_VLINE, wah);

	// Top default line - window border is over top
	mvhline(ny, nx, ACS_HLINE, waw);
	mvaddch(ny, nw, ACS_TTEE);

	nx += nw + 1;
	nw = waw - nw - 1;
	nh = .7 * wah;

	for(c = frames[1].clients; c; c = c->next) {
		resize(c, nx, ny, nw, nh);
	}

	ny += nh;
	nh = wah - nh;

	// default border line for bottom frame
	mvhline(ny, nx, ACS_HLINE, nw);
	mvaddch(ny, nx - 1, ACS_LTEE);

	for(c = frames[2].clients; c; c = c->next) {
		resize(c, nx, ny, nw, nh);
	}
}

int
to_next(void) {
	int result = cur_frame + 1;
	if(result >= nframes) {
		result = 0;
	}
	return result;
}

int
to_prev(void) {
	int result = cur_frame - 1;
	if(result < 0) {
		result = nframes - 1;
	}
	return result;
}

static void
split_focus(const char *args[]) {
	int dest_frame; 
	if(!args[0] || !strcmp(args[0], "+")) {
		dest_frame = to_next();
	} else {
		dest_frame = to_prev();
	}
	focus_frame(dest_frame);
}

static void
move_frame(const char *args[]) {

	Client *cur = frames[cur_frame].sel;
	Client *next = NULL;
	Client *fcur = NULL;

	if (!cur)
		return;

	detach(cur, cur_frame);
	if(cur->next) 
		focus(next);
	else if(frames[cur_frame].clients)
		focus(frames[cur_frame].clients);

	focus_frame(to_next());
	attach(cur);
	focus(cur);
	arrange();
}
