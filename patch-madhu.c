static void
cycle_layouts(Arg *arg) {
	static int next_layout = 0;
	static const int nlayouts = sizeof(layouts)/sizeof(Layout);
	if ((++next_layout) == nlayouts)
		next_layout = 0;
	//;madhu 230122 arg->v = &layouts[next_layout] results in a segfault, why
	Arg arg2 = { .v = &layouts[next_layout] };
	setlayout(&arg2);
}

static void
identify_wintitle(const Arg *arg)
{
	Client * c;
	if (selmon && (c = selmon->sel) && c->name[0]) {
		int x = TEXTW("XXXXXXX");
		char buf[600];
		snprintf(buf, sizeof(buf),
			 "%s, pid %d, %d", c->name, c->pid,
			 getcardprop(c, netatom[NetWMPid]));
		int w = TEXTW(buf);
		drw_setscheme(drw, scheme[SchemeSel]);
		drw_text(drw, x, 0, w, bh, lrpad / 2, buf, 0);
		drw_map(drw, selmon->barwin, x, 0, w, bh);
	}
}

static void
myfocus(const Arg *arg) {
	Client *c;
	if (!(c=selmon->sel)) {
		fprintf(stderr,"myfocus(NULL): failed\n");
	} else if (raise_on_click) {
		c->raiseme = 1;
		restack(selmon);
	}
	XAllowEvents(dpy,ReplayPointer,CurrentTime);
	XAllowEvents(dpy, AsyncKeyboard, CurrentTime);
}

static void
startwm(const Arg *arg) {
	static char *shell = NULL;
	if(!shell && !(shell = getenv("SHELL")))
		shell = "/bin/sh";
	if(!arg)
		return;

	{ // unconditionally switch to default layout before restarting
		Arg arg2 = { .v = &layouts[0] };
		Arg arg3 = { .ui = ~0 };
		setlayout(&arg2);
		view(&arg3);
	}

	{ // unconditionally switch fullscreen windows. FIXME - all monitors
		Client *c;
		for (c = selmon->clients; c; c = c->next)
			if (c->isfullscreen)
				setfullscreen(c,False);
	}

	fprintf(stderr, "startwm: execlp '%s -c %s'", shell, (char *) arg->v);
	execlp(shell, shell, "-c", (char *) arg->v, (char *)NULL);
}

static void
toggle_fixed(const Arg * arg) {
	if(!selmon->sel)
		return;
	fprintf(stderr,"toggle_fixed: %s->isfixed=%d\n",selmon->sel->name,selmon->sel->isfixed);
	if (selmon->sel->isfixed == True)
		selmon->sel->isfixed = False;
	else
		selmon->sel->isfixed = True;
}

static void
toggle_resizehints () {
	resizehints = (resizehints == False) ? True : False;
}

/*
xprop -root _NET_CLIENT_LIST
*/
static void
windowlist(const Arg *arg) {
	Client *c;
	int out[2], nts[2], cpid, i, j;
	char *argv[] = {"dmenu", "-l", "50", "-i", NULL };
	FILE *fout;
#ifdef HAVE_USE_SIGACTION_SIGCHLD
	struct sigaction sa;
#endif

	if (pipe(out) == -1) { perror("windowlist: pipe(out)"); return; }
	if (pipe(nts) == -1) { perror("windowlist: pipe(nts)"); return; }
	if ((cpid = fork()) == -1) { perror("windowlist: fork()"); return;}

	if (cpid == 0) { // child reads from NTS and writes to OUT
#ifdef HAVE_USE_SIGACTION_SIGCHLD
// ;madhu 240201 restoring SIGCHLD handler in child has no effect
// on the parent.
		setsid();
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);
#endif
		close(nts[1]);
		if (dup2(nts[0], STDIN_FILENO) == -1)
		{fprintf(stderr, "child:dup2(nts[0]=%d,stdin=%d)\n",
			 nts[0], STDIN_FILENO);
			perror("windowlist:");
			return;}
		close(nts[0]);
		if (dup2(out[1], STDOUT_FILENO) == -1)
		{fprintf(stderr, "child:dup2(out[1]=%d,stdout=%d)\n",
			 out[1], STDOUT_FILENO);
			perror("windowlist:"); return;}
		close(out[1]);
		if (execvp("dmenu", argv))
		{fprintf(stderr, "execvp dmenu failed\n");
			perror("windowlist:"); return;}
	} /* Child */

	// parent writes to NTS and reads from OUT
	close(out[1]);

	if ((fout = fdopen(nts[1], "w")) == NULL)  {
		perror("windowlist: fdopen");
		return;
	}

	for (c = selmon->clients, i = 0; c; c= c->next, i++) { //FIXME - all monitors
		fprintf(fout, "%10d | %60.60s | 0x%7.7lx\n",  i,  c->name, c->win);
		fflush(fout);
	}
	close(nts[1]);
	wait(NULL);

	char cmd[1024];	size_t nread;
	if ((nread = read(out[0], cmd, sizeof(cmd)-1)) == -1) {
		fprintf(stderr, "windowlist: read from dmenu failed\n");
		return;
	}

	cmd[nread] = '\0';
	close(out[0]);

	if (nread==0 || (nread == 1 && cmd[0] == '\n'))	{
		fprintf(stderr,"windowlist: ignoring cmd<<%s>>\n", cmd); /* KLUDGE */
		return;
	}

	if (strncmp(&cmd[73], " | 0x", 5)) {
		fprintf(stderr, "windowlst: Expected |0x, got %s", &cmd[73]);
		return;
	}

	j = strtol(cmd,NULL,10);

	for (c = selmon->clients, i = 0; ((c != NULL) && (i != j)); c = c->next, i++);

	if(!ISVISIBLE(c)) {
		for(int i = 0; i < LENGTH(tags); i++)
			if (c->tags & (1 << i)) {
				Arg arg; arg.ui = 1 << i; view(&arg);
				break;
			}
//		c->mon->seltags ^= 1;
//		c->mon->tagset[c->mon->seltags] = c->tags;
	}
	pop(c);
	return;
}


static void toggleopacity(const Arg *arg) {
	if(!selmon->sel)
		return;
	fprintf(stderr,"%s->opacity=%g",selmon->sel->name,selmon->sel->opacity);
	if (arg->f == 0) {	// toggle-opacity
		if (selmon->sel->opacity <= -1)
			selmon->sel->opacity = shade;
		else if ((-1 < selmon->sel->opacity) &&
		    (selmon->sel->opacity < 0))
			selmon->sel->opacity += 1;
		else if ((0  <= selmon->sel->opacity) &&
			 (selmon->sel->opacity < 1))
			selmon->sel->opacity -= 1;
		else
			selmon->sel->opacity = -1;
	} else {
		selmon->sel->opacity += arg->f;
		if (selmon->sel->opacity < 0)
			selmon->sel->opacity = 1;
		else if (selmon->sel->opacity > 1)
			selmon->sel->opacity = 0.1;
	}
	fprintf(stderr,"==>%g\n",selmon->sel->opacity);
	window_opacity_set(selmon->sel, selmon->sel->opacity);
}

static void toggle_systray () {
	showsystray = (showsystray == False) ? True : False;
	updatestatus();
}

static void
focusurgent(const Arg *arg)
{
	Client *c;
	int i;
	for(c=selmon->clients; c && !c->isurgent; c=c->next);
	if(c) {
		for(i=0; i < LENGTH(tags) && !((1 << i) & c->tags); i++);
		if(i < LENGTH(tags)) {
			const Arg a = {.ui = 1 << i};
			view(&a);
			WARP(c);
			focus(c);
		}
	}
}

static void
mywarp(const Arg *arg)
{
	Client *c;
	if (!(c=selmon->sel)) {
		fprintf(stderr,"mywarp(NULL): failed\n");
	} else {
	  Arg a = {.i = 0};
	  focusstack(&a);
	  WARP(c);
	}
}

static void
toggle_placement_style(const Arg *arg)
{
  if (placement_style == centered)
    placement_style = under_mouse;
  else if (placement_style == under_mouse)
    placement_style = centered;
}


static void
toggle_raise_on_click(const Arg *arg)
{
  raise_on_click = !raise_on_click;
}

static void
toggle_attach_bottom(const Arg *arg)
{
  attach_bottom_p = !attach_bottom_p;
}
static void
toggle_zoom_swap(const Arg *arg)
{
  zoom_swap_p = !zoom_swap_p;
}