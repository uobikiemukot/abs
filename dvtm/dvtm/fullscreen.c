static void
fullscreen(void) {
	Client *c;

	nframes = 1;

	for(c = frames[0].clients; c; c = c->next)
		resize(c, wax, way, waw, wah);
}
