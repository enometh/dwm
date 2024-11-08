/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#define X_ISVISIBLE(C)		(ISVISIBLE(C) &&  (c->tags != TAGMASK))
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)

//xtile
#define GETINC(X)               ((X) < 0 ? X + 1000 : X - 1000)
#define INC(X)                  ((X) < 0 ? X - 1000 : X + 1000)
#define ISINC(X)                ((X) <= -1000 || (X) >= 1000)
#define MOD(N,M)                ((N)%(M) < 0 ? (N)%(M) + (M) : (N)%(M))
#define PREVSEL                 3000
#define TRUNC(X,A,B)            (MAX((A), MIN((X), (B))))

#define SYSTEM_TRAY_REQUEST_DOCK    0
#define _NET_SYSTEM_TRAY_ORIENTATION_HORZ 0
/* XEMBED messages */
#define XEMBED_EMBEDDED_NOTIFY      0
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_FOCUS_IN             4
#define XEMBED_MODALITY_ON         10

#define XEMBED_MAPPED              (1 << 0)
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_WINDOW_DEACTIVATE    2

#define VERSION_MAJOR               0
#define VERSION_MINOR               0
#define XEMBED_EMBEDDED_VERSION (VERSION_MAJOR << 16) | VERSION_MINOR

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDock,
       NetWMWindowOpacity,
       NetWMWindowTypeDesktop,
       NetSystemTray, NetSystemTrayOP, NetSystemTrayOrientation,
       NetWMWindowTypeDialog, NetClientList,
       NetWMPid,
       NetDesktopNames, NetNumberOfDesktops,
       NetCurrentDesktop, NetWMDesktop,
       NetWMSkipTaskbar,
       NetLast }; /* EWMH atoms */
enum { Manager, Xembed, XembedInfo, XLast }; /* Xembed atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { DWMTags, DWMLast };                              /* DWM atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

enum { DirHor, DirVer, DirRotHor, DirRotVer, DirLast }; /* tiling dirs */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int x, y, fx, fy, n, dir;
	float fact;
} Area;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
	int bw, oldbw;
	unsigned int tags;
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen, isterminal, noswallow, isdesktop;
	pid_t pid;
	Client *next;
	Client *snext;
	Client *swallowing;
	Monitor *mon;
	Window win;
	double opacity;
	int raiseme;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

typedef struct Pertag Pertag;
struct Monitor {
	char ltsymbol[16];
	int nmaster;
	int n_non_master_columns;
	int num;
	int by;               /* bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	int showbar;
	int topbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
	const Layout *lt[2];
	Pertag *pertag;
};

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	int isfloating;
	int monitor;
	double opacity;
	int isterminal;
	int noswallow;
} Rule;

typedef struct Systray   Systray;
struct Systray {
	Window win;
	Client *icons;
};

enum placement_style { centered, under_mouse, };
extern enum placement_style placement_style; 

/* function declarations */
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static Bool atompropop(Window w, Atom prop, Atom value, int op);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static Client *findbefore(Client *c);
static void window_opacity_set(Client *c, double opacity);
static void focus(Client *c);
static void focusclienttaskbar(const Arg *arg);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Atom getatomprop(Client *c, Atom prop);
static Client *getclientundermouse(void);
static int getcardprop(Client *c, Atom prop);
static pid_t getparentprocess(pid_t p);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static unsigned int getsystraywidth();
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void icccm2_setup(int replace_wm);
static void incnmaster(const Arg *arg);
static void incnstackcols(const Arg *arg);
static int isdescprocess(pid_t p, pid_t c);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void pop(Client *c);
static void propertynotify(XEvent *e);
static void pushstack(const Arg *arg);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void removesystrayicon(Client *i);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizebarwin(Monitor *m);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void resizerequest(XEvent *e);
static void restack(Monitor *m);
static void run(void);
static void scan(void);
static void selectionclear(XEvent *e);
static Bool sendevent(Window w, Atom proto, int m, long d0, long d1, long d2, long d3, long d4);
static void sendmon(Client *c, Monitor *m);
static void set_net_current_desktop();
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
//static void setmfact(const Arg *arg);
static void settagsprop(Window w, unsigned int tags);
static void setup(void);
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
#ifndef HAVE_USE_SIGACTION_SIGCHLD
static void sigchld(int unused);
#endif
static void spawn(const Arg *arg);
static int stackpos(const Arg *arg, int exludetaggedall);
static Client *swallowingclient(Window w);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static Client *termforwin(const Client *c);
static void tile(Monitor *m);
static void togglebar(const Arg *arg);
static void togglelosefocus(const Arg *arg);
static void togglefloating(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatesystray(void);
static void updatesystrayicongeom(Client *i, int w, int h);
static void updatesystrayiconstate(Client *i, XPropertyEvent *ev);
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void unswallow_now(const Arg *arg);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static Client *wintosystrayicon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void xinitvisual();
static void WARP(const Client *c);
static void zoom(const Arg *arg);

//xtile
static void setdirs(const Arg *arg);
static void setfacts(const Arg *arg);

/* variables */
static Client *prevzoom = NULL;
static const char broken[] = "broken";
static char stext[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar height */
static int lrpad;            /* sum of left and right padding for text */
static int ncc;		     /* number of client clicked */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	[ResizeRequest] = resizerequest,
	[SelectionClear] = selectionclear,
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast], dwmatom[DWMLast], xatom[XLast];
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;
static Systray *systray = NULL;
static unsigned long systrayorientation = _NET_SYSTEM_TRAY_ORIENTATION_HORZ;

static int useargb = 0;
static Visual *visual;
static int depth;
static Colormap cmap;

static int replace_wm = 0;

/* configuration, allows nested code to access above variables */
#include "config.h"

struct Pertag {
	unsigned int curtag, prevtag; /* current and previous tag */
	int nmasters[LENGTH(tags) + 1]; /* number of windows in master area */
	int n_non_master_columns[LENGTH(tags) + 1];
	Area areas[LENGTH(tags) + 1][3]; /* tiling areas */
	unsigned int sellts[LENGTH(tags) + 1]; /* selected layouts */
	const Layout *ltidxs[LENGTH(tags) + 1][2]; /* matrix of tags and layouts indexes  */
	Bool showbars[LENGTH(tags) + 1]; /* display bar for the current tag */
	Client *prevzooms[LENGTH(tags) + 1]; /* store zoom information */
};

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
void
applyrules(Client *c)
{
	const char *class, *instance;
	unsigned int i;
	const Rule *r;
	Monitor *m;
	XClassHint ch = { NULL, NULL };

	/* rule matching */
	c->isfloating = 0;
	c->tags = 0;
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if ((!r->title || strstr(c->name, r->title))
		&& (!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance)))
		{
			c->isterminal = r->isterminal;
			c->isfloating = r->isfloating;
			c->opacity = r->opacity;
			c->tags |= r->tags;
			for (m = mons; m && m->num != r->monitor; m = m->next);
			if (m)
				c->mon = m;
		}
	}
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > sw)
			*x = sw - WIDTH(c);
		if (*y > sh)
			*y = sh - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else {
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= m->wx)
			*x = m->wx;
		if (*y + *h + 2 * c->bw <= m->wy)
			*y = m->wy;
	}

	/* Don't touch 1x1 windows. vbox configures a 1x1 window when
	   mapping a window. if we resize it to bh x bh then when vbox
	   maps an actual window, it reconfigures that window to a
	   minimum size minw x minw */
	if (*h < bh && *h != 1 && *w != 1)
		*h = bh;
	if (*w < bh && *h != 1 && *w != 1)
		*w = bh;
	if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
		if (!c->hintsvalid)
			updatesizehints(c);
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if (c->mina > 0 && c->maxa > 0) {
			if (c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if (c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if (c->incw)
			*w -= *w % c->incw;
		if (c->inch)
			*h -= *h % c->inch;
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if (c->maxw)
			*w = MIN(*w, c->maxw);
		if (c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m)
{
	if (m)
		showhide(m->stack);
	else for (m = mons; m; m = m->next)
		showhide(m->stack);
	if (m) {
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		arrangemon(m);
}

void
arrangemon(Monitor *m)
{
	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
}

void
attach(Client *c)
{
	c->next = c->mon->clients;
	c->mon->clients = c;
}

 void
attachbottom(Client *c)
{
	Client **tc;
	c->next = NULL;
	for (tc = &c->mon->clients; *tc; tc = &(*tc)->next);
	*tc = c;
}

void
attachstack(Client *c)
{
	{ //madhu 131023 - TODO FIGURE OUT HOW TO PRINT A STACKBACKTRACE HERE
		Client **tc;
		int count = 0, maxcount = 1000;
		for (tc=&c->mon->stack; *tc
			    && *tc != c
			    && ((count == 0)?1:(*tc != c->mon->stack))
			    && (count <= maxcount);
		    tc=&(*tc)->snext,count++);
		if (count > maxcount || (*tc && *tc == c->mon->stack)) {
			fprintf(stderr, "FIXME: attachstack infinite loop\n");
			*tc = NULL;
			return;
		}
		if (*tc == c) {
			fprintf(stderr, "FIXME: attachstack corruption dup\n");
			return;
		}
	}
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

/* mode = 0: has, mode=1, add, mode=2 delete.
get mode=3 - val is the index of the property to retrieve
*/
Bool
atompropop(Window w, Atom prop, Atom value, int mode)
{

	Atom realtype;
	unsigned long n, extra;
	int format, i = 0;
	Atom *p;

	if (XGetWindowProperty(dpy, w, prop, 0L, 64L, False, XA_ATOM,
			       &realtype, &format, &n, &extra,
			       (unsigned char **)&p)
	    != Success)
		return False;

	if  (p == NULL)
		if (mode == 0 || mode == 2) {
			return False;
		}

	if (n == 0) {
		if (mode == 0 || mode == 2 || mode == 3) {
			XFree(p);
			return False;
		}
	}

	if (mode == 3) {
		Atom ret = (value < n) ? p[value] : False;
		XFree(p);
		return ret;
	}

	int found = 0;
	for (i = 0; i < n; i++)
		if (value == p[i]) {
			found++;
			if (mode == 0) {
				XFree(p);
				return True;
			}
		}
	if (mode == 0) {
		XFree(p);
		return found ? True : False;
	}

	if (mode == 2 && !found) {
		XFree(p);
		return True;
	}

	if (mode == 1 && found) {
		XFree(p);
		return True;
	}

	int newsize = (mode == 1) ? 1 + n : n - found;

	Atom *ret;
	if (!(ret = (Atom *)calloc(newsize, sizeof(Atom))))
		if (! (mode == 2 && newsize == 0))
			die("fatal: could not malloc() %u bytes\n",
			    sizeof(Atom) * newsize);
	int j = 0;
	for (i = 0; i < n; i++) {
		if (mode == 1 || (mode == 2 && p[i] != value))
			ret[j++] = p[i];
	}

	if (mode == 1)
		ret[j++] = value;
	if (j != newsize) die("assert");


	Bool retval = XChangeProperty(dpy, w, prop, XA_ATOM, 32,
				      PropModeReplace,
				      (unsigned char *)ret, j);
	if (mode == 1 && newsize == 1)
		if (! (ret[0] == value))
			die("wtf");
	if (ret) free(ret);
	XFree(p);
	return retval;
}

void
swallow(Client *p, Client *c)
{
	if (c->noswallow || c->isterminal)
		return;

	detach(c);
	detachstack(c);

	setclientstate(c, WithdrawnState);
	XUnmapWindow(dpy, p->win);

	p->swallowing = c;
	c->mon = p->mon;

	Window w = p->win;
	p->win = c->win;
	c->win = w;
	updatetitle(p);
	arrange(p->mon);
	configure(p);
	updateclientlist();
}

void
unswallow(Client *c)
{
	c->win = c->swallowing->win;

	free(c->swallowing);	// ;madhu 231223 free? rly?
	c->swallowing = NULL;

	updatetitle(c);
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
	configure(c);
	setclientstate(c, NormalState);
}

void
unswallow_now(const Arg *arg)
{
	Client *c = selmon->sel;
	if (!c) return;
	if (!c->swallowing) {
		Client *term = termforwin(c);
		if (term)
			swallow(term, c);
		return;
	}
	Window w = c->win;
	Client *d = c->swallowing;
	c->win = c->swallowing->win;
	c->swallowing = NULL;
	d->win = w;
	if (!(d->mon == c->mon)) die("assert");
	updatetitle(d);
	updatetitle(c);
	attach(d);
	attachstack(d);
	setclientstate(c, NormalState);
	setclientstate(d, NormalState);
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	XMapWindow(dpy, d->win);
}

void
buttonpress(XEvent *e)
{
	unsigned int i, x, click, n = 0;
	float wpc; //width per client
	Arg arg = {0};
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;

	click = ClkRootWin;
	/* focus monitor if necessary */
	if ((m = wintomon(ev->window)) && m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	if (ev->window == selmon->barwin) {
		i = x = 0;
		do
			x += TEXTW(tags[i]);
		while (ev->x >= x && ++i < LENGTH(tags));
		if (i < LENGTH(tags)) {
			click = ClkTagBar;
			arg.ui = 1 << i;
		} else if (ev->x < x + TEXTW(selmon->ltsymbol))
			click = ClkLtSymbol;
		else if (ev->x > selmon->ww - (int)TEXTW(stext))
			click = ClkStatusText;
		else {
			click = ClkWinTitle;
			for (c = selmon->clients; c; c = c->next)
				if (ISVISIBLE(c))
					n++;
			if (n == 0)
				ncc = 0;
			else {
				int blw = TEXTW(m->ltsymbol);
				wpc = (selmon->wx + selmon->ww - TEXTW(stext) - x - blw) / n;
				ncc = (ev->x - x - blw) / wpc;
			}
		}
	}
	else if ((c = wintoclient(ev->window))) {
		focus(c);
		click = ClkClientWin;
	}
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

void
cleanup(void)
{
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	Monitor *m;
	size_t i;

	view(&a);
	selmon->lt[selmon->sellt] = &foo;
	for (m = mons; m; m = m->next)
		while (m->stack)
			unmanage(m->stack, 0);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (mons)
		cleanupmon(mons);
	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors); i++)
		free(scheme[i]);
	free(scheme);
	// make sure we release SubstructureRedirect so that another
	// wm can start successfully.
	XSelectInput(dpy, DefaultRootWindow(dpy), NoEventMask);
	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	if (showsystray) {
		XUnmapWindow(dpy, systray->win);
		XDestroyWindow(dpy, systray->win);
		free(systray);
	}
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
cleanupmon(Monitor *mon)
{
	Monitor *m;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	XUnmapWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->barwin);
	free(mon);
}

void
clientmessage(XEvent *e)
{
	XWindowAttributes wa;
	XSetWindowAttributes swa;
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if (showsystray && cme->window == systray->win && cme->message_type == netatom[NetSystemTrayOP]) {
		/* add systray icons */
		if (cme->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
			if (!(c = (Client *)calloc(1, sizeof(Client))))
				die("fatal: could not malloc() %u bytes\n", sizeof(Client));
			c->win = cme->data.l[2];
			c->mon = selmon;
			c->next = systray->icons;
			systray->icons = c;
			XGetWindowAttributes(dpy, c->win, &wa);
			c->x = c->oldx = c->y = c->oldy = 0;
			c->w = c->oldw = wa.width;
			c->h = c->oldh = wa.height;
			c->oldbw = wa.border_width;
			c->bw = 0;
			c->isfloating = True;
			/* reuse tags field as mapped status */
			c->tags = 1;
			updatesizehints(c);
			updatesystrayicongeom(c, wa.width, wa.height);
			XAddToSaveSet(dpy, c->win);
			XSelectInput(dpy, c->win, StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);
			XReparentWindow(dpy, c->win, systray->win, 0, 0);
			/* use parents background pixmap */
			swa.background_pixmap = ParentRelative;
			swa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
			XChangeWindowAttributes(dpy, c->win, CWBackPixmap|CWBackPixel, &swa);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_EMBEDDED_NOTIFY, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			/* FIXME not sure if I have to send these events, too */
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_FOCUS_IN, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_MODALITY_ON, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			resizebarwin(selmon);
			updatesystray();
			setclientstate(c, NormalState);
		}
		return;
	}

	if (cme->message_type == netatom[NetCurrentDesktop]) {
		int tag = (cme->data.l[0] == (unsigned long)-1) ? TAGMASK : cme->data.l[0];
//		fprintf(stderr, "NET_CURRENT_DESKTOP client %s: %lu = tag %d\n",
//			c->name, cme->data.l[0], tag);
		Arg a = { .ui = 1 << tag  };
		view(&a);
	}

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
		else if (cme->data.l[1] == netatom[NetWMSkipTaskbar]
			 || cme->data.l[2] == netatom[NetWMSkipTaskbar]) {
			Window w = cme->window;
			switch(cme->data.l[0]) {
			case 0: //_NET_WM_STATE_REMOVE
				atompropop(w, netatom[NetWMState], netatom[NetWMSkipTaskbar], 2); break;
			case 1: //_NET_WM_STATE_ADD
				atompropop(w, netatom[NetWMState], netatom[NetWMSkipTaskbar], 1); break;
			case 2: //NET_WM_STATE_TOGGLE

				atompropop(w, netatom[NetWMState], netatom[NetWMSkipTaskbar], atompropop(w, netatom[NetWMState], netatom[NetWMSkipTaskbar], 0) ? 2 : 1);
			}
		}
	} else if (cme->message_type == netatom[NetActiveWindow]) {
//		if (c != selmon->sel && !c->isurgent)
//			seturgent(c, 1);
//		int frompagerp = (cme->data.l[0] == 2); /* source indicator */
		{
			int i;
			for(i=0; i < LENGTH(tags) && !((1 << i) & c->tags); i++);
			if(i < LENGTH(tags)) {
				const Arg a = {.ui = 1 << i};
				view(&a);
				focus(c);
				c->raiseme = 1;
				restack(selmon);
				//WARP(c);
			}
		}
	}
	else if (cme->message_type == netatom[NetWMDesktop]) {
		/* The EWMH spec states that if the cardinal returned
		 * is 0xFFFFFFFF (-1) then the window should appear on
		 * all desktops */
		int tagno = (cme->data.l[0] == (unsigned long)-1)
			? TAGMASK : cme->data.l[0];
//		fprintf(stderr, "NET WM DESKTOP client %s: %lu = tag %d\n",
//			c->name, cme->data.l[0], tagno) ;
		if (selmon->sel == c) {
				Arg a = { .ui = 1<< tagno };
				tag(&a);
		} else {
//			fprintf(stderr, "not selected\n");
		}
	}
}

void
configure(Client *c)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e)
{
	Monitor *m;
	Client *c;
	XConfigureEvent *ev = &e->xconfigure;
	int dirty;

	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, bh);
			updatebars();
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					if (c->isfullscreen)
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
				XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
				resizebarwin(m);
			}
			focus(NULL);
			arrange(NULL);
		}
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth)
			c->oldbw = c->bw = ev->border_width;
		else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
			m = c->mon;
			if (ev->value_mask & CWX) {
				c->oldx = //c->x;
				c->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) {
				c->oldy = //c->y;
				c->y = m->my + ev->y;
			}
			if (ev->value_mask & CWWidth) {
				c->oldw = //c->w;
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				c->oldh = //c->h;
				c->h = ev->height;
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
			if ((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if ((ev->value_mask & CWStackMode) && ! (ev->value_mask & CWSibling)) {
				XWindowChanges changes;
				changes.sibling = ev->above;
				changes.stack_mode = ev->detail;
				XConfigureWindow(dpy,c->win,CWStackMode,
						 &changes);
			}
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if (ISVISIBLE(c))
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		} else
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

Monitor *
createmon(void)
{
	Monitor *m;
	int i, j;

	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->nmaster = nmaster;
	m->n_non_master_columns = n_non_master_columns;
	m->showbar = showbar;
	m->topbar = topbar;
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
	if (!(m->pertag = (Pertag *)calloc(1, sizeof(Pertag))))
		die("fatal: could not malloc() %u bytes\n", sizeof(Pertag));
	m->pertag->curtag = m->pertag->prevtag = 1;
	for (i=0; i <= LENGTH(tags); i++) {
		/* init nmaster */
		m->pertag->nmasters[i] = m->nmaster;
		m->pertag->n_non_master_columns[i] = m->n_non_master_columns;

		/* init tiling dirs and facts */
		for (j = 0; j < 3; j++) {
			m->pertag->areas[i][j].dir = MIN(dirs[j], ((int[]){ 3, 1, 1 }[j]));
			m->pertag->areas[i][j].fact = TRUNC(facts[j], 0.1, 10);
		}

		/* init layouts */
		m->pertag->ltidxs[i][0] = m->lt[0];
		m->pertag->ltidxs[i][1] = m->lt[1];
		m->pertag->sellts[i] = m->sellt;

		/* init showbar */
		m->pertag->showbars[i] = m->showbar;

		/* swap focus and zoomswap*/
		m->pertag->prevzooms[i] = NULL;
	}
	return m;
}

void
destroynotify(XEvent *e)
{
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
	else if ((c = wintosystrayicon(ev->window))) {
		removesystrayicon(c);
		resizebarwin(selmon);
		updatesystray();
	}
	else if ((c = swallowingclient(ev->window)))
		unmanage(c->swallowing, 1);
}

void
detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
detachstack(Client *c)
{
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = (t == c) ? NULL : t;
		{ //madhu 131023 - TODO FIGURE OUT HOW TO PRINT A STACKBACKTRACE HERE
			if (t == c)
				fprintf(stderr,"FIXME: cornercase: detachstack(%p)=%lu %s fails\n",
					(void *)c, c->win,c->name);
		}
	}
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next))
			m = mons;
	} else if (selmon == mons)
		for (m = mons; m->next; m = m->next);
	else
		for (m = mons; m->next != selmon; m = m->next);
	return m;
}

void
drawbar(Monitor *m)
{
	int x, w, tw = 0, stx;
	int boxs = drw->fonts->h / 9;
	int boxw = drw->fonts->h / 6 + 2;
	unsigned int i, occ = 0, urg = 0, n = 0;
	Client *c;

	if (!m->showbar)
		return;

	/* draw status first so it can be overdrawn by tags later */
	if (m == selmon) { /* status is only drawn on selected monitor */
		drw_setscheme(drw, scheme[SchemeNorm]);
		tw = TEXTW(stext) - lrpad + 2; /* 2px right padding */
		if (showsystray && m == selmon)
			tw += getsystraywidth();
		drw_text(drw, m->ww - tw, 0, tw, bh, 0, stext, 0);
	}

	resizebarwin(m);
	for (c = m->clients; c; c = c->next) {
		if (ISVISIBLE(c))
			n++;
		if (c->tags != TAGMASK)
			occ |= c->tags;
		if (c->isurgent)
			urg |= c->tags;
	}
	x = 0;
	for (i = 0; i < LENGTH(tags); i++) {
		w = TEXTW(tags[i]);
		drw_setscheme(drw, scheme[m->tagset[m->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
		drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
		if (occ & 1 << i)
			drw_rect(drw, x + boxs, boxs, boxw, boxw,
				m == selmon && selmon->sel && selmon->sel->tags & 1 << i,
				urg & 1 << i);
		x += w;
	}
	w = TEXTW(m->ltsymbol);
	drw_setscheme(drw, scheme[SchemeNorm]);
	x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);

	if ((w = (n > 0) ? (m->ww - tw - x)/n : (m->ww - tw - x)) > bh) {
		stx = m->ww - tw;
		if (m->sel  || (lose_focus && n != 0)) {
			for (c = m->clients, i = 1; c; c = c->next) {
				if (ISVISIBLE(c)) {
					drw_setscheme(drw, scheme[c == selmon->sel ? SchemeSel : SchemeNorm]);
					drw_text(drw, x, 0, w, bh, lrpad / 2, c->name, 0);
					if (c->isfloating)
						drw_rect(drw, x + boxs, boxs, boxw, boxw, c->isfixed, 0);
					x += w;
					w = ++i < n ? w : stx - x;
				}
			}
		} else {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_rect(drw, x, 0, w, bh, 1, 1);
		}
	}
	drw_map(drw, m->barwin, 0, 0, m->ww, bh);
}

void
drawbars(void)
{
	Monitor *m;

	for (m = mons; m; m = m->next)
		drawbar(m);
	updatesystray();
}

Time	Last_Event_Time = CurrentTime;

void
enternotify(XEvent *e)
{
	Client *c;
	Monitor *m;
	XCrossingEvent *ev = &e->xcrossing;

	Last_Event_Time = ev->time;
	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
		return;
	c = wintoclient(ev->window);
	m = c ? c->mon : wintomon(ev->window);
	if (m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
	} if (lose_focus && !c) {
	} else if (!c || c == selmon->sel)
		return;
	focus(c);
}

void
expose(XEvent *e)
{
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	if (ev->count == 0 && (m = wintomon(ev->window)))
		drawbar(m);
}

void
window_opacity_set(Client *c, double opacity)
{
	if (opacity >= 0 && opacity <= 1) {
		unsigned long real_opacity[] = { opacity * 0xffffffff };
		XChangeProperty(dpy, c->win, netatom[NetWMWindowOpacity], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)real_opacity, 1);
	}
	else
		XDeleteProperty(dpy, c->win, netatom[NetWMWindowOpacity]);
}

Client *
findbefore(Client *c)
{
	Client *tmp;
	if (c == selmon->clients)
		return NULL;
	for (tmp = selmon->clients; tmp && tmp->next != c; tmp = tmp->next);
	return tmp;
}

void
focus(Client *c)
{
	if (lose_focus && !c) goto skip_choose;

	if (!c || !ISVISIBLE(c))
		// first restrict search to clients which were not
		// visible in the previous view.
		for (c = selmon->stack; c &&
			     ((c->tags & c->mon->tagset[1 - c->mon->seltags])
			      ||  !ISVISIBLE(c));
		     c = c->snext);

	if (!c || !ISVISIBLE(c))
		// search failed. broaden search to all clients in the
		// current view
		for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);

skip_choose:
	if (selmon->sel && selmon->sel != c) {
		unfocus(selmon->sel, 0);
		float o = selmon->sel->opacity;
		if ((o <= 0.0) || (o > 1.0) || (o != 1.0) /*&& (o >= shade)*/)
			window_opacity_set(selmon->sel, shade);
	}
	if (c) {
		if (c->mon != selmon)
			selmon = c->mon;
		if (c->isurgent)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
		setfocus(c);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
	if(c) window_opacity_set(c, c->opacity);
}

void
focusclienttaskbar(const Arg *arg)
{
    Client *c;

    for (c = selmon->clients; c; c = c->next)
		if (ISVISIBLE(c) && --ncc < 0) {
		focus(c);
		c->raiseme = 1;
		restack(selmon);
			break;
		}
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
}

void
focusmon(const Arg *arg)
{
	Monitor *m;

	if (!mons->next)
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(NULL);
}

void
focusstack(const Arg *arg)
{
	int i = stackpos(arg, ISINC(arg->i));
	Client *c, *p;

	if (i < 0)
		return;
#define Y_ISVISIBLE(C) ((ISINC(arg->i) ? X_ISVISIBLE(C) : ISVISIBLE(C)))

	for (p = NULL, c = selmon->clients; c && (i || !Y_ISVISIBLE(c));
	    i -= Y_ISVISIBLE(c) ? 1 : 0, p = c, c = c->next);
	focus(c ? c : p);
	restack(selmon);
#undef Y_ISVISIBLE
}

Atom
getatomprop(Client *c, Atom prop)
{
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;
	/* FIXME getatomprop should return the number of items and a pointer to
	 * the stored data instead of this workaround */
	Atom req = XA_ATOM;
	if (prop == xatom[XembedInfo])
		req = xatom[XembedInfo];

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, req,
		&da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		if (da == xatom[XembedInfo] && dl == 2)
			atom = ((Atom *)p)[1];
		XFree(p);
	}
	return atom;
}

static int
getcardprop(Client *c, Atom prop)
{
	int format;
	int result = -1;
	int *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, 1L, False, XA_CARDINAL,
			       &real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if (n != 0)
		result = *p;
	XFree(p);
	return result;
}

Client *
getclientundermouse(void)
{
	int ret, di;
	unsigned int dui;
	Window child, dummy;

	ret = XQueryPointer(dpy, root, &dummy, &child, &di, &di, &di, &di, &dui);
	if (!ret)
		return NULL;

	return wintoclient(child);
}

int
getrootptr(int *x, int *y)
{
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if (n != 0)
		result = *p;
	XFree(p);
	return result;
}

unsigned int
getsystraywidth()
{
	unsigned int w = 0;
	Client *i;
	if (showsystray)
		for (i = systray->icons; i; w += i->w + systrayspacing, i = i->next) ;
	return w ? w + systrayspacing : 1;
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING) {
		strncpy(text, (char *)name.value, size - 1);
	} else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
		strncpy(text, *list, size - 1);
		XFreeStringList(list);
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}

void
grabbuttons(Client *c, int focused)
{
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if (focused) {
			for (i = 0; i < LENGTH(buttons); i++)
				if (buttons[i].click == ClkClientWin)
					for (j = 0; j < LENGTH(modifiers); j++)
						XGrabButton(dpy, buttons[i].button,
							    buttons[i].mask | modifiers[j],
							    c->win, False, BUTTONMASK,
							    GrabModeSync, GrabModeAsync, None, None);
		} else
 			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				    BUTTONMASK, GrabModeSync, GrabModeAsync, None, None);
 	}
}

void
grabkeys(void)
{
	updatenumlockmask();
	{
		unsigned int i, j, k;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		int start, end, skip;
		KeySym *syms;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		XDisplayKeycodes(dpy, &start, &end);
		syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
		if (!syms)
			return;
		for (k = start; k <= end; k++)
			for (i = 0; i < LENGTH(keys); i++)
				/* skip modifier codes, we do that ourselves */
				if (keys[i].keysym == syms[(k - start) * skip])
					for (j = 0; j < LENGTH(modifiers); j++)
						XGrabKey(dpy, k,
							 keys[i].mod | modifiers[j],
							 root, True,
							 GrabModeAsync, GrabModeAsync);
		XFree(syms);
	}
}

void
incnmaster(const Arg *arg)
{
	selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag] = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

void
incnstackcols(const Arg *arg)
{
	int new = selmon->n_non_master_columns + arg->i;
	selmon->n_non_master_columns = selmon->pertag->n_non_master_columns[selmon->pertag->curtag] = MAX(new, 1);
	arrange(selmon);
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent *e)
{
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void
killclient(const Arg *arg)
{
	if (!selmon->sel)
		return;
	if (!sendevent(selmon->sel->win, wmatom[WMDelete], NoEventMask, wmatom[WMDelete], CurrentTime, 0 , 0, 0)) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, selmon->sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL, *term = NULL;
	Window trans = None;
	XWindowChanges wc;

	c = ecalloc(1, sizeof(Client));
	c->win = w;
	/* geometry */
	c->mon = selmon;	// XXX FIXME ;madhu 160725

	// centered placement of new windows
	if (wa->map_state != IsViewable) {
		XSizeHints size;
		long tmp;
		if (!XGetWMNormalHints(dpy, w, &size, &tmp))
			size.flags = 0;
		if (!(size.flags & (USPosition | PPosition))) {
			int px, py;
			if ((placement_style == under_mouse) &&
			    getrootptr(&px, &py)) {
				wa->x = px;
				wa->y = py;
			} else { //centered
			  wa->x = c->mon->wx + (sw - wa->width) / 2;
			  wa->y = c->mon->wy + (sh - wa->height) / 2;
			}
		}
	}

	c->x = c->oldx = (wa->x % sw) + c->mon->wx;
	c->y = c->oldy = wa->y + ((c->mon->topbar == True && wa->y !=0 ) ? 0 : c->mon->wy);
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;

	updatetitle(c);
	c->opacity=-1;
	c->pid = getcardprop(c, netatom[NetWMPid]);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->mon = t->mon;
		c->tags = t->tags;
	} else {
		c->mon = selmon;
		applyrules(c);
		term = termforwin(c);
	}

	if (c->x + WIDTH(c) > c->mon->wx + c->mon->ww)
		c->x = c->mon->wx + c->mon->ww - WIDTH(c);
	if (c->y + HEIGHT(c) > c->mon->wy + c->mon->wh)
		c->y = c->mon->wy + c->mon->wh - HEIGHT(c);
	c->x = MAX(c->x, c->mon->wx);
	c->y = MAX(c->y, c->mon->wy);
	c->bw = borderpx;

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	configure(c); /* propagates border_width, if size doesn't change */
	updatewindowtype(c);
	updatesizehints(c);
	updatewmhints(c);
	c->oldx = c->x;
	c->oldy = c->y;
	c->oldw = c->w;
	c->oldh = c->h;
	c->oldbw = c->bw;
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);
	updatetitle(c);
	XTextProperty prop;
	applyrules(c);
	if (XGetTextProperty(dpy, c->win, &prop, dwmatom[DWMTags])) {
		c->tags = *(unsigned int *)prop.value;
		XFree(prop.value);
	} else {
		if (XGetTransientForHint(dpy, w, &trans))
			t = wintoclient(trans);
		if (t)
			c->tags = t->tags;
	}
	if (!c->tags)
		c->tags = selmon->tagset[selmon->seltags];
	settagsprop(c->win, c->tags);
	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	if (c->isfloating)
		if (!c->isdesktop) XRaiseWindow(dpy, c->win);
	if (!attach_bottom_p)
		attach(c);
	else
		attachbottom(c);
	attachstack(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	setclientstate(c, NormalState);
	if (c->mon == selmon)
		unfocus(selmon->sel, 0);
	c->mon->sel = c;
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	if (term)
		swallow(term, c);
	focus(NULL);
	updateclientlist();
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;
	Client *i;
	if ((i = wintosystrayicon(ev->window))) {
		sendevent(i->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0, systray->win, XEMBED_EMBEDDED_VERSION);
		resizebarwin(selmon);
		updatesystray();
	}

	if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
		return;
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}

void
monocle(Monitor *m)
{
	unsigned int n = 0;
	Client *c;

	for (c = m->clients; c; c = c->next)
		if (ISVISIBLE(c))
			n++;
	if (n > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
}

void
motionnotify(XEvent *e)
{
	static Monitor *mon = NULL;
	Monitor *m;
	XMotionEvent *ev = &e->xmotion;

	if (ev->window != root)
		return;
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	mon = m;
}

void
movemouse(const Arg *arg)
{
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	int orig_opacity = c->opacity;
	window_opacity_set(c, .42);
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;
	if (!getrootptr(&x, &y))
		return;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);
			if (abs(selmon->wx - nx) < snap)
				nx = selmon->wx;
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
				nx = selmon->wx + selmon->ww - WIDTH(c);
			if (abs(selmon->wy - ny) < snap)
				ny = selmon->wy;
			else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
				ny = selmon->wy + selmon->wh - HEIGHT(c);
			if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
			&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
				togglefloating(NULL);
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	window_opacity_set(c, orig_opacity);
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

Client *
nexttiled(Client *c)
{
	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

void
pop(Client *c)
{
	detach(c);
	attach(c);
	focus(c);
	arrange(c->mon);
}

void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if ((c = wintosystrayicon(ev->window))) {
		if (ev->atom == XA_WM_NORMAL_HINTS) {
			updatesizehints(c);
			updatesystrayicongeom(c, c->w, c->h);
		}
		else
			updatesystrayiconstate(c, ev);
		resizebarwin(selmon);
		updatesystray();
	}
	if ((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if (ev->state == PropertyDelete)
		return; /* ignore */
	else if ((c = wintoclient(ev->window))) {
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
				(c->isfloating = (wintoclient(trans)) != NULL))
				arrange(c->mon);
			break;
		case XA_WM_NORMAL_HINTS:
			c->hintsvalid = 0;
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			drawbars();
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			if (c == c->mon->sel)
				drawbar(c->mon);
		}
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
	}
}

void
pushstack(const Arg *arg)
{
	int i = stackpos(arg, 0);
	Client *sel = selmon->sel, *c, *p;

	if (i < 0)
		return;
	else if (i == 0) {
		detach(sel);
		attach(sel);
	}
	else {
		for (p = NULL, c = selmon->clients; c; p = c, c = c->next)
			if (!(i -= (ISVISIBLE(c) && c != sel)))
				break;
		c = c ? c : p;
		detach(sel);
		sel->next = c->next;
		c->next = sel;
	}
	arrange(selmon);
}

void
quit(const Arg *arg) {
	running = 0;
}


Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

void
removesystrayicon(Client *i)
{
	Client **ii;

	if (!showsystray || !i)
		return;
	for (ii = &systray->icons; *ii && *ii != i; ii = &(*ii)->next);
	if (ii)
		*ii = i->next;
	free(i);
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void
resizebarwin(Monitor *m)
{
	unsigned int w = m->ww;
	if (showsystray && m == selmon)
		w -= getsystraywidth();
	XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, w, bh);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;

	c->x = wc.x = x;
	c->y = wc.y = y;
	c->w = wc.width = w;
	c->h = wc.height = h;
	wc.border_width = c->bw;
	if ((!selmon->lt[selmon->sellt]->arrange || c->isfloating) && !c->isfullscreen) {
		c->oldx = x;
		c->oldy = y;
		c->oldw = w;
		c->oldh = h;
	}
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

void
resizemouse(const Arg *arg)
{
	int ocx, ocy, nw, nh;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;

	int orig_opacity = c->opacity;
	window_opacity_set(c, .42);
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / 60))
				continue;
			lasttime = ev.xmotion.time;

			nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
			nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
			if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
			&& c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh)
			{
				if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
				&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL);
			}
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, c->x, c->y, nw, nh, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	XUngrabPointer(dpy, CurrentTime);
	window_opacity_set(c, orig_opacity);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

void
resizerequest(XEvent *e)
{
	XResizeRequestEvent *ev = &e->xresizerequest;
	Client *i;

	if ((i = wintosystrayicon(ev->window))) {
		updatesystrayicongeom(i, ev->width, ev->height);
		resizebarwin(selmon);
		updatesystray();
	}
}

void
restack(Monitor *m)
{
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if (!m->sel)
		return;
	if (m->sel->isfloating || !m->lt[m->sellt]->arrange || m->sel->raiseme) {
		if (!m->sel->isdesktop) XRaiseWindow(dpy, m->sel->win);
		if (m->sel->raiseme) m->sel->raiseme = 0;
	}
	if (m->lt[m->sellt]->arrange) {
		wc.stack_mode = Below;
		wc.sibling = m->barwin;
		for (c = m->stack; c; c = c->snext)
			if (!c->isfloating && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
	}
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void)
{
	XEvent ev;
	/* main event loop */
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}

void
scan(void)
{
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
				manage(wins[i], &wa);
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
}

void
sendmon(Client *c, Monitor *m)
{
	if (c->mon == m)
		return;
	unfocus(c, 1);
	detach(c);
	detachstack(c);
	c->mon = m;
	c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	if (!attach_bottom_p)
		attach(c);
	else
		attachbottom(c);
	attachstack(c);
	focus(NULL);
	arrange(NULL);
}

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

void
setdirs(const Arg *arg)
{
	int *dirs = (int *)arg->v, i, n;
	Area *areas = selmon->pertag->areas[selmon->pertag->curtag];

	for (i = 0; i < 3; i++) {
		n = (int[]){ 4, 2, 2 }[i];
		areas[i].dir = ISINC(dirs[i]) ?
			MOD((int)areas[i].dir + GETINC(dirs[i]), n) : TRUNC(dirs[i], 0, n - 1);
	}
	arrange(selmon);
}

void
setfacts(const Arg *arg)
{
	float *facts = (float *)arg->v;
	Area *areas = selmon->pertag->areas[selmon->pertag->curtag];
	int i;

	for (i = 0; i < 3; i++)
		areas[i].fact = TRUNC(ISINC(facts[i]) ?
			areas[i].fact + GETINC(facts[i]) : facts[i], 0.1, 10);
	arrange(selmon);
}

int
sendevent(Window w, Atom proto, int mask, long d0, long d1, long d2, long d3, long d4)
{
	int n;
	Atom *protocols, mt;
	int exists = 0;
	XEvent ev;

	if (proto == wmatom[WMTakeFocus] || proto == wmatom[WMDelete]) {
		mt = wmatom[WMProtocols];
		if (XGetWMProtocols(dpy, w, &protocols, &n)) {
			while (!exists && n--)
				exists = protocols[n] == proto;
			XFree(protocols);
		}
	}
	else {
		exists = 1;
		mt = proto;
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = w;
		ev.xclient.message_type = mt;
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = d0;
		ev.xclient.data.l[1] = d1;
		ev.xclient.data.l[2] = d2;
		ev.xclient.data.l[3] = d3;
		ev.xclient.data.l[4] = d4;
		XSendEvent(dpy, w, False, mask, &ev);
	}
	return exists;
}

void
set_net_current_desktop()
{
	unsigned int tagset = selmon->tagset[selmon->seltags];
	int i, j = 0, ntags = 0;
	for (i = 0; i < LENGTH(tags); i++)
		if (tagset & (1 << i)) {
			j = i;
			if (++ntags > 1)
				break;
		}
	XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32, PropModeReplace, (unsigned char *) &j, 1);
}

void
setfocus(Client *c)
{
	if (!c->neverfocus) {
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *) &(c->win), 1);
	}
//	sendevent(c, wmatom[WMTakeFocus]);
	sendevent(c->win, wmatom[WMTakeFocus], NoEventMask,
		  wmatom[WMTakeFocus],
		  (ISVISIBLE(c) ?
		   Last_Event_Time : CurrentTime), 0, 0, 0);
}

void
setfullscreen(Client *c, int fullscreen)
{
	if (fullscreen && !c->isfullscreen) {
//		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
//			PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		atompropop(c->win, netatom[NetWMState],
			   netatom[NetWMFullscreen], 1); // add
		c->isfullscreen = 1;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		if (!c->isdesktop) XRaiseWindow(dpy, c->win);
	} else if (!fullscreen && c->isfullscreen){
//		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
//			PropModeReplace, (unsigned char*)0, 0);
		atompropop(c->win, netatom[NetWMState],
			   netatom[NetWMFullscreen], 2); //delete
		c->isfullscreen = 0;
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		arrange(c->mon);
	}
}

void
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt]) {
		selmon->pertag->sellts[selmon->pertag->curtag] ^= 1;
		selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
	}
	if (arg && arg->v)
		selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt] = (Layout *)arg->v;
	selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);
	if (selmon->sel || (lose_focus && selmon->clients)) // lose_focus: loose
		arrange(selmon);
	else
		drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutely * /
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if (f < 0.05 || f > 0.95)
		return;
	selmon->mfact = selmon->pertag->mfacts[selmon->pertag->curtag] = f;
	arrange(selmon);
}
*/

void
settagsprop(Window w, unsigned int _tags)
{
	unsigned int v[1] = { _tags };
	XTextProperty p;
	p.value = (unsigned char *)v;
	p.encoding = XA_CARDINAL;
	p.format = 32;
	p.nitems = LENGTH(v);
	XSetTextProperty(dpy, w, &p, dwmatom[DWMTags]);
	int i, j = 0, ntags = 0;
	for (i = 0; i < LENGTH(tags); i++)
		if (_tags & (1 << i)) {
			j = i;
			if (++ntags > 1) break;
		}
	unsigned long x = -1; // all desktops
	if (ntags == 1) x = j;
	XChangeProperty(dpy, w, netatom[NetWMDesktop], XA_CARDINAL, 32,
			PropModeReplace,
			(unsigned char *) &x, 1);
	// add _NET_WM_STATE_SKIP_TASKBAR to _NET_WM_STATE if the
	// window is displayed on all tags. Remove it if not.
	atompropop(w, netatom[NetWMState], netatom[NetWMSkipTaskbar],
		   (_tags == TAGMASK) ? 1 : 2);
}

void
setup(void)
{
	int i;
	XSetWindowAttributes wa;
	Atom utf8string;
#ifdef HAVE_USE_SIGACTION_SIGCHLD
	struct sigaction sa;

	/* do not transform children into zombies when they terminate */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	/* clean up any zombies (inherited from .xinitrc etc) immediately */
	while (waitpid(-1, NULL, WNOHANG) > 0);
#else
	sigchld(0);
#endif

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	xinitvisual();
	drw = drw_create(dpy, screen, root, sw, sh, visual, depth, cmap);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;
	bh = drw->fonts->h + 2;
	updategeom();
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetSystemTray] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_S0", False);
	netatom[NetSystemTrayOP] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
	netatom[NetSystemTrayOrientation] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMWindowOpacity] = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDock] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetWMWindowTypeDesktop] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netatom[NetWMPid] = XInternAtom(dpy, "_NET_WM_PID", False);
	netatom[NetDesktopNames] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
	netatom[NetNumberOfDesktops] = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	netatom[NetWMDesktop] = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
	netatom[NetWMSkipTaskbar] = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
	dwmatom[DWMTags] = XInternAtom(dpy, "DWM_TAGS", False);
	xatom[Manager] = XInternAtom(dpy, "MANAGER", False);
	xatom[Xembed] = XInternAtom(dpy, "_XEMBED", False);
	xatom[XembedInfo] = XInternAtom(dpy, "_XEMBED_INFO", False);
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CurResize] = drw_cur_create(drw, XC_sizing);
	cursor[CurMove] = drw_cur_create(drw, XC_fleur);
	/* init appearance */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], 3, alpha_scheme);
	/* init system tray */
	updatesystray();
	/* init bars */
	updatebars();
	updatestatus();
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);

	icccm2_setup(replace_wm);
	checkotherwm();		/* bogus (if there is an iccm2 wm!) */

	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "dwm", 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);

	/* initialize desktop names */
	{
		unsigned long i = LENGTH(tags);
		XChangeProperty(dpy, root, netatom[NetNumberOfDesktops],
				XA_CARDINAL, 32, PropModeReplace,
				(unsigned char *) &i, 1);
		XDeleteProperty(dpy, root, netatom[NetDesktopNames]);
		char buf[26 * LENGTH(tags)], *p;
		int tlen = 0;
		for(i = 0, p = buf; i < LENGTH(tags); i++) {
			const char *str; int len;
			str = "Desktop "; len = strlen(str);
			strcpy(p, str); p += len; tlen += len;
			str = tags[i]; len = strlen(str);
			strcpy(p, str); p += len; tlen += len;
			if (*p != 0 || !(tlen < sizeof(buf) - 2)) {
				die("ASSERT ERROR\n");
			}
			p++; tlen++;
		}
		XChangeProperty(dpy, root, netatom[NetDesktopNames],
				utf8string, 8, PropModeReplace,
				(unsigned char *) buf, tlen);
	}
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;

	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
#ifdef XINERAMA
	wa.event_mask |= PointerMotionMask;
#endif
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	focus(NULL);
}

void
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

void
showhide(Client *c)
{
	if (!c)
		return;
	if (ISVISIBLE(c)) {
		/* show clients top down */
		XMoveWindow(dpy, c->win, c->x, c->y);
		if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) && !c->isfullscreen) {
			if (c->isfloating)
				resize(c, c->x, c->y, c->w, c->h, 0);
			else
				resize(c, c->oldx, c->oldy, c->oldw, c->oldh, 0);
		}
		showhide(c->snext);
	} else {
		/* hide clients bottom up */
		showhide(c->snext);
		XMoveWindow(dpy, c->win, c->x + 2 * sw, c->y);
	}
}

#ifndef HAVE_USE_SIGACTION_SIGCHLD
void
sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("can't install SIGCHLD handler:");
	while (0 < waitpid(-1, NULL, WNOHANG));
}
#endif

void
spawn(const Arg *arg)
{
	struct sigaction sa;

	if (arg->v == dmenucmd)
		dmenumon[0] = '0' + selmon->num;
	if (fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();

		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);

		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("dwm: execvp '%s' failed:", ((char **)arg->v)[0]);
	}
}

int
stackpos(const Arg *arg, int excludep) {
	int n, i;
	Client *c, *l;

	/* The following commented out code implements the commit
	   67d76bdc68102d "Do not allow focus to drift from fullscreen
	   client via focusstack()" on top of "Carlos Pita's stacker
	   patch" but is commented out because we *do* want to focus
	   other windows in fullscreen mode */
	/*
	if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
		return -1;  /* ;madhu 210819 FIXME * /
	*/

	if (!selmon->clients)
		return -1;

#define Y_ISVISIBLE(C) ((excludep ? X_ISVISIBLE(C) : ISVISIBLE(C)))
	if (arg->i == PREVSEL) {
		if (!excludep) fprintf(stderr, "stackpos PREVSEL wtf\n");
		for (l = selmon->stack; l && (!ISVISIBLE(l) || (!excludep || l == selmon->sel)); l = l->snext);
		if (!l)
			return -1;
		for (i = 0, c = selmon->clients; c != l; i += ISVISIBLE(c) ? 1 : 0, c = c->next);
		return i;
	}
	else if (ISINC(arg->i)) {
		if (!lose_focus && !selmon->sel)
			return -1;
		for (i = 0, c = selmon->clients; c != selmon->sel; i += Y_ISVISIBLE(c) ? 1 : 0, c = c->next);
		for (n = i; c; n += Y_ISVISIBLE(c) ? 1 : 0, c = c->next);
		 // avoid cornercase division by 0
		return n == 0 ? 0 : MOD(i + GETINC(arg->i), n);
	}
	else if (arg->i < 0) {
		for (i = 0, c = selmon->clients; c; i += Y_ISVISIBLE(c) ? 1 : 0, c = c->next);
		return MAX(i + arg->i, 0);
	}
	else
		return arg->i;
#undef Y_ISVISIBLE
}

void
tag(const Arg *arg)
{
	if (selmon->sel && arg->ui & TAGMASK) {
		selmon->sel->tags = arg->ui & TAGMASK;
		settagsprop(selmon->sel->win, selmon->sel->tags);
		focus(NULL);
		arrange(selmon);
	}
}

void
tagmon(const Arg *arg)
{
	if (!selmon->sel || !mons->next)
		return;
	sendmon(selmon->sel, dirtomon(arg->i));
}

void
tile(Monitor *m)
{
	Client *c;
	Area *ga = m->pertag->areas[m->pertag->curtag], *ma = ga + 1, *sa = ga + 2, *a;
	unsigned int i, n, h, w, ms, ss;
	float f;

	/* print layout symbols */
	snprintf(m->ltsymbol, sizeof m->ltsymbol, "%c%c%c",
		(char[]){ '<', '^', '>', 'v' }[ga->dir],
		(char[]){ '-', '|' }[ma->dir],
		(char[]){ '-', '|' }[sa->dir]);
	/* calculate number of clients */
	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0)
		return;
	ma->n = MIN(n, m->nmaster), sa->n = n - ma->n;
	/* calculate area rectangles */
	f = ma->n == 0 ? 0 : (sa->n == 0 ? 1 : ga->fact / 2);
	if (ga->dir == DirHor || ga->dir == DirRotHor)
		ms = f * (m->ww - gappx), ss = m->ww - ms - gappx,
		ma->x = ga->dir == DirHor ? gappx : ss + gappx, ma->y = gappx, ma->fx = ma->x + ms, ma->fy = m->wh,
		sa->x = ga->dir == DirHor ? ms + gappx : gappx, sa->y = gappx, sa->fx = sa->x + ss, sa->fy = m->wh;
	else
		ms = f * (m->wh - gappx), ss = m->wh - ms - gappx,
		ma->x = gappx, ma->y = ga->dir == DirVer ? gappx : ss + gappx, ma->fx = m->ww, ma->fy = ma->y + ms,
		sa->x = gappx, sa->y = ga->dir == DirVer ? ms + gappx : gappx, sa->fx = m->ww, sa->fy = sa->y + ss;


	/* gaplessgrid.c (i.e. no gaps in the grid - not no gaps
	 * between tiles) arrange non-master clients in a gapless grid
	 * of at most n_non_master_columns which is always at least 1
	 */

	int nstacked = n - ma->n; /* number of clients in non-master */
	int cols = nstacked ? MIN(m->n_non_master_columns, nstacked) : 0;
	int rows = cols ? nstacked / cols : 0;
	if (rows && nstacked > rows * cols) rows++;

	int rn = 0, cn = 0;	/* current row/col */
	int cw, ch;		/* cell width/height */
	int ax, ay;

	/* tile clients */
	for (c = nexttiled(m->clients), i = 0; i < n; c = nexttiled(c->next), i++) {
		if (i == 0 || i == ma->n) {
			a = (i == 0) ? ma : sa;
			f = (a->n > 1) ? a->fact / (a->fact + a->n - 1) : 1;
			w = (a->dir == DirVer ? 1 : f) * (a->fx - a->x);
			h = (a->dir == DirHor ? 1 : f) * (a->fy - a->y);
			w -= gappx;  h -= gappx;
		} else if ((i + 1) == ma->n || (i + 1) == n) {
			w = (a->fx - a->x);
			h = (a->fy - a->y);
			w -= gappx;  h -= gappx;
		} else if ((i - 1) == 0 || (i - 1) == ma->n) {
			f = (a->n > 1) ? 1.0 / (a->n - 1) : 1;
			w = (a->dir == DirVer ? 1 : f) * (a->fx - a->x);
			h = (a->dir == DirHor ? 1 : f) * (a->fy - a->y);
			w -= gappx;  h -= gappx;
		}

		if (a == sa && cols > 1) {
			int j = i - ma->n; /* ordinal number in stack */

			// carlos complicated facts
			if (j == 0) {
				ax = a->x, ay = a->y;
				int nelem = (a->dir == DirVer ? rows : cols);
				f = (nelem > 1) ? a->fact / (a->fact + nelem - 1) : 1.0;
				cw = (a->dir == DirVer ? 1.0 /cols : f) * (a->fx - ax);
				ch = (a->dir == DirHor ? 1.0 /rows : f) * (a->fy - ay);
				ch -= gappx; cw -= gappx;
			} else if ((j + 1) == nstacked) {
				cw = (a->fx - ax);
				ch = (a->fy - ay);
				ch -= gappx; cw -= gappx;
			} else if (j == (a->dir == DirVer ? cols : rows)) {
				int nelem = (a->dir == DirVer ? rows : cols);
				f = (nelem > 1) ? 1.0 / (nelem - 1) : 1.0;
				cw = (a->dir == DirVer ? 1.0 /cols : f) * (a->fx - ax);
				ch = (a->dir == DirHor ? 1.0 /rows : f) * (a->fy - ay);
				ch -= gappx;  cw -= gappx;
			}

			/* // without complicated facts
			if (j == 0 || j == (a->dir == DirVer ? cols : rows)) {
				if (j == 0) ax = a->x, ay = a->y;
				ch = (a->fy - a->y) / rows;
				cw = (a->fx - a->x) / cols;
			} else if ((j + 1) == nstacked) {
				ch = a->fy - ay;
				cw = a->fx - ax;
			}
			*/

			resize(c, m->wx + ax, m->wy + ay,
			       cw - 2 * c->bw,
			       ch - 2 * c->bw,
			       False);
			if (a->dir == DirVer) {
				cn++;
				ax += cw /* WIDTH(c) */;
				ax += gappx;
				if (cn >= cols) {
					cn = 0;
					rn++;
					ax = a->x;
					ay += ch /*HEIGHT(c)*/;
					ay += gappx;
				}
			} else {
				rn++;
				ay += ch /*HEIGHT(c)*/;
				ay += gappx;
				if (rn >= rows) {
					rn = 0;
					cn++;
					ay = a->y;
					ax += cw /*WIDTH(c)*/;
					ax += gappx;
				}
			}
		} else {
			resize(c, m->wx + a->x, m->wy + a->y, w - 2 * c->bw, h - 2 * c->bw, False);
			a->x += a->dir == DirHor ? w /*WIDTH(c)*/: 0;
			a->y += a->dir == DirVer ? h /*HEIGHT(c)*/: 0;
			if (a->dir == DirVer) a->y += gappx; else a->x += gappx;
		}
	}
}

void
togglebar(const Arg *arg)
{
	selmon->showbar = selmon->pertag->showbars[selmon->pertag->curtag] = !selmon->showbar;
	updatebarpos(selmon);
	resizebarwin(selmon);
	if (showsystray) {
		XWindowChanges wc;
		if (!selmon->showbar)
			wc.y = -bh;
		else if (selmon->showbar) {
			wc.y = 0;
			if (!selmon->topbar)
				wc.y = selmon->mh - bh;
		}
		XConfigureWindow(dpy, systray->win, CWY, &wc);
	}
	arrange(selmon);
}

void
togglelosefocus(const Arg *arg)
{
	lose_focus = !lose_focus;
}

void
togglefloating(const Arg *arg)
{
	myfocus(arg);

	if (!selmon->sel)
		return;
//	if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
//		return;
	selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
	if (selmon->sel->isfloating)
		resize(selmon->sel, selmon->sel->oldx, selmon->sel->oldy,
		       selmon->sel->oldw, selmon->sel->oldh, 0);
	if (!selmon->sel->isfloating && !selmon->sel->isfullscreen) {
		selmon->sel->oldx = selmon->sel->x;
		selmon->sel->oldy = selmon->sel->y;
		selmon->sel->oldw = selmon->sel->w;
		selmon->sel->oldh = selmon->sel->h;
	}
	arrange(selmon);
}

void
toggletag(const Arg *arg)
{
	unsigned int newtags;

	if (!selmon->sel)
		return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		selmon->sel->tags = newtags;
		settagsprop(selmon->sel->win, selmon->sel->tags);
		focus(NULL);
		arrange(selmon);
	}
}

void
toggleview(const Arg *arg)
{
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);
	int i;

	if (newtagset) {
		if (newtagset == ~0) {
			selmon->pertag->prevtag = selmon->pertag->curtag;
			selmon->pertag->curtag = 0;
		}
		/* test if the user did not select the same tag */
		if (!(newtagset & 1 << (selmon->pertag->curtag - 1))) {
			selmon->pertag->prevtag = selmon->pertag->curtag;
			for (i=0; !(newtagset & 1 << i); i++) ;
			selmon->pertag->curtag = i + 1;
		}
		selmon->tagset[selmon->seltags] = newtagset;

		/* apply settings for this view */
		selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
		selmon->n_non_master_columns = selmon->pertag->n_non_master_columns[selmon->pertag->curtag];
		selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
		selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
		selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];
		if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
			togglebar(NULL);
		focus(NULL);
		set_net_current_desktop();
		arrange(selmon);
		Last_Event_Time = CurrentTime;
	}
}

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
unmanage(Client *c, int destroyed)
{
	Monitor *m = c->mon;
	XWindowChanges wc;

	if (c->swallowing) {
		unswallow(c);
		return;
	}

	Client *s = swallowingclient(c->win);
	if (s) {
		free(s->swallowing);
		s->swallowing = NULL;
		arrange(m);
        focus(getclientundermouse());
		return;
	}

	detach(c);
	detachstack(c);
	if (!destroyed) {
		XDeleteProperty(dpy, c->win, netatom[NetWMDesktop]);
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XSelectInput(dpy, c->win, NoEventMask);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);

	if (!s) {
		arrange(m);
		focus(getclientundermouse());
		updateclientlist();
	}
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) {
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);
	}
	else if ((c = wintosystrayicon(ev->window))) {
		removesystrayicon(c);
		resizebarwin(selmon);
		updatesystray();
	}
}

void
updatebars(void)
{
	unsigned int w;
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixel = 0,
		.border_pixel = 0,
		.colormap = cmap,
		.event_mask = ButtonPressMask|ExposureMask
	};
	XClassHint ch = {"dwm", "dwm"};
	for (m = mons; m; m = m->next) {
		if (m->barwin)
			continue;
		w = m->ww;
		if (showsystray && m == selmon)
			w -= getsystraywidth();
		m->barwin = XCreateWindow(dpy, root, m->wx, m->by, w, bh, 0, depth,
				InputOutput, visual,
				CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &wa);
		XChangeProperty(dpy, m->barwin, netatom[NetWMWindowType], XA_ATOM, 32,
				PropModeReplace, (unsigned char *) &netatom[NetWMWindowTypeDock], 1);
		XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
		XMapRaised(dpy, m->barwin);
		XSetClassHint(dpy, m->barwin, &ch);
	}
}

void
updatebarpos(Monitor *m)
{
	m->wy = m->my;
	m->wh = m->mh;
	if (m->showbar) {
		m->wh -= bh;
		m->by = m->topbar ? m->wy : m->wy + m->wh;
		m->wy = m->topbar ? m->wy + bh : m->wy;
	} else
		m->by = -bh;
}

void
updateclientlist(void)
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char *) &(c->win), 1);
}

int
updategeom(void)
{
	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;

		/* new monitors if nn > n */
		for (i = n; i < nn; i++) {
			for (m = mons; m && m->next; m = m->next);
			if (m)
				m->next = createmon();
			else
				mons = createmon();
		}
		for (i = 0, m = mons; i < nn && m; m = m->next, i++)
			if (i >= n
			|| unique[i].x_org != m->mx || unique[i].y_org != m->my
			|| unique[i].width != m->mw || unique[i].height != m->mh)
			{
				dirty = 1;
				m->num = i;
				m->mx = m->wx = unique[i].x_org;
				m->my = m->wy = unique[i].y_org;
				m->mw = m->ww = unique[i].width;
				m->mh = m->wh = unique[i].height;
				updatebarpos(m);
			}
		/* removed monitors if n > nn */
		for (i = nn; i < n; i++) {
			for (m = mons; m && m->next; m = m->next);
			while ((c = m->clients)) {
				dirty = 1;
				m->clients = c->next;
				detachstack(c);
				c->mon = mons;
				attach(c);
				attachstack(c);
			}
			if (m == selmon)
				selmon = mons;
			cleanupmon(m);
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup */
		if (!mons)
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

void
updatenumlockmask(void)
{
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else if (size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	} else
		c->basew = c->baseh = 0;
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	} else
		c->incw = c->inch = 0;
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	} else
		c->maxw = c->maxh = 0;
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else if (size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	} else
		c->minw = c->minh = 0;
	if (size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
	c->hintsvalid = 1;
}

void
updatestatus(void)
{
	if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
		strcpy(stext, "dwm-"VERSION);
	drawbar(selmon);
}

void
updatesystrayicongeom(Client *i, int w, int h)
{
	if (i) {
		i->h = bh;
		if (w == h)
			i->w = bh;
		else if (h == bh)
			i->w = w;
		else
			i->w = (int) ((float)bh * ((float)w / (float)h));
		applysizehints(i, &(i->x), &(i->y), &(i->w), &(i->h), False);
		/* force icons into the systray dimenons if they don't want to */
		if (i->h > bh) {
			if (i->w == i->h)
				i->w = bh;
			else
				i->w = (int) ((float)bh * ((float)i->w / (float)i->h));
			i->h = bh;
		}
	}
}

void
updatesystrayiconstate(Client *i, XPropertyEvent *ev) {
	long flags;
	int code = 0;

	if (!showsystray || !i || ev->atom != xatom[XembedInfo] ||
			!(flags = getatomprop(i, xatom[XembedInfo])))
		return;

	if (flags & XEMBED_MAPPED && !i->tags) {
		i->tags = 1;
		code = XEMBED_WINDOW_ACTIVATE;
		XMapRaised(dpy, i->win);
		setclientstate(i, NormalState);
	}
	else if (!(flags & XEMBED_MAPPED) && i->tags) {
		i->tags = 0;
		code = XEMBED_WINDOW_DEACTIVATE;
		XUnmapWindow(dpy, i->win);
		setclientstate(i, WithdrawnState);
	}
	else
		return;
	sendevent(i->win, xatom[Xembed], StructureNotifyMask, CurrentTime, code, 0,
			systray->win, XEMBED_EMBEDDED_VERSION);
}

void
updatesystray(void) {
	XSetWindowAttributes wa;
	Client *i;
	unsigned int x = selmon->mx + selmon->mw;
	unsigned int w = 1;

	if (!showsystray)
		return;
	if (!systray) {
		/* init systray */
		if (!(systray = (Systray *)calloc(1, sizeof(Systray))))
			die("fatal: could not malloc() %u bytes\n", sizeof(Systray));
		systray->win = XCreateSimpleWindow(dpy, root, x, selmon->by, w, bh, 0, 0, scheme[SchemeSel][ColBg].pixel);
		wa.event_mask        = ButtonPressMask | ExposureMask;
		wa.override_redirect = True;
		wa.background_pixmap = ParentRelative;
		wa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
		XSelectInput(dpy, systray->win, SubstructureNotifyMask);
		XChangeProperty(dpy, systray->win, netatom[NetSystemTrayOrientation], XA_CARDINAL, 32,
				PropModeReplace, (unsigned char *)&systrayorientation, 1);
		XChangeWindowAttributes(dpy, systray->win, CWEventMask|CWOverrideRedirect|CWBackPixel|CWBackPixmap, &wa);
		XMapRaised(dpy, systray->win);
		XSetSelectionOwner(dpy, netatom[NetSystemTray], systray->win, CurrentTime);
		if (XGetSelectionOwner(dpy, netatom[NetSystemTray]) == systray->win) {
			sendevent(root, xatom[Manager], StructureNotifyMask, CurrentTime, netatom[NetSystemTray], systray->win, 0, 0);
			XSync(dpy, False);
		}
		else {
			fprintf(stderr, "dwm: unable to obtain system tray.\n");
			free(systray);
			systray = NULL;
			return;
		}
	}
	for (w = 0, i = systray->icons; i; i = i->next) {
		XMapRaised(dpy, i->win);
		w += systrayspacing;
		XMoveResizeWindow(dpy, i->win, (i->x = w), 0, i->w, i->h);
		w += i->w;
		if (i->mon != selmon)
			i->mon = selmon;
	}
	w = w ? w + systrayspacing : 1;
	x -= w;
	XMoveResizeWindow(dpy, systray->win, x, selmon->by, w, bh);
	/* redraw background */
	drw_rect(drw, 0, 0,  w, bh, 1, 0);
	  //XSetForeground(dpy, dc.gc, scheme[SchemeNorm][ColBg].pixel);
	  //XFillRectangle(dpy, , dc.gc, 0, 0, w, bh);

	XSync(dpy, False);
}


void
updatetitle(Client *c)
{
	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
}


void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (wtype == netatom[NetWMWindowTypeDialog])
		c->isfloating = 1;
	else if (wtype == netatom[NetWMWindowTypeDesktop]) {
		c->isdesktop = c->isfloating = c->isfixed = 1;
		// put the "desktop window" on all "desktops"
		int x = -1;
		XChangeProperty(dpy, c->win, netatom[NetWMDesktop],
				XA_CARDINAL, 32, PropModeReplace,
				(unsigned char *) &x, 1);
	}
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
		if (wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = 0;
		XFree(wmh);
	}
}

void
view(const Arg *arg)
{
	int i;
	unsigned int tmptag;

	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK) {
		selmon->pertag->prevtag = selmon->pertag->curtag;
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
		if (arg->ui == ~0)
			selmon->pertag->curtag = 0;
		else {
			for (i=0; !(arg->ui & 1 << i); i++) ;
			selmon->pertag->curtag = i + 1;
		}
	} else {
		tmptag = selmon->pertag->prevtag;
		selmon->pertag->prevtag = selmon->pertag->curtag;
		selmon->pertag->curtag = tmptag;
	}
	selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
	selmon->n_non_master_columns = selmon->pertag->n_non_master_columns[selmon->pertag->curtag];
	selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
	selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
	selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];
	if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
		togglebar(NULL);
	focus(NULL);
	set_net_current_desktop();
	arrange(selmon);
	Last_Event_Time = CurrentTime;
}

pid_t
getparentprocess(pid_t p)
{
	unsigned int v = 0;

#ifdef __linux__
	FILE *f;
	char buf[256];
	snprintf(buf, sizeof(buf) - 1, "/proc/%u/stat", (unsigned)p);

	if (!(f = fopen(buf, "r")))
		return 0;

	fscanf(f, "%*u %*s %*c %u", &v);
	fclose(f);
#endif /* __linux__ */

	return (pid_t)v;
}

int
isdescprocess(pid_t p, pid_t c)
{
	while (p != c && c != 0)
		c = getparentprocess(c);

	return (int)c;
}

Client *
termforwin(const Client *w)
{
	Client *c;
	Monitor *m;

	if (!w->pid || w->isterminal)
		return NULL;

	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->isterminal && !c->swallowing && c->pid && isdescprocess(c->pid, w->pid))
				return c;
		}
	}

	return NULL;
}

Client *
swallowingclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->swallowing && c->swallowing->win == w)
				return c;
		}
	}

	return NULL;
}

static void
WARP(const Client *c) {
	Window dummy;
	int x, y, di;
	unsigned int dui;

	if (!c) {
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->wx + selmon->ww / 2, selmon->wy + selmon->wh/2);
		return;
	}

	XQueryPointer(dpy, root, &dummy, &dummy, &x, &y, &di, &di, &dui);

	if ((x > c->x && y > c->y && x < c->x + c->w && y < c->y + c->h) ||
	   (y > c->mon->by && y < c->mon->by + bh))
		return;

	// or (c->w / 2, c->h / 2)
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w, 0);
}

Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w)
				return c;
	return NULL;
}

Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = mons; m; m = m->next)
		if (w == m->barwin)
			return m;
	if ((c = wintoclient(w)))
		return c->mon;
	return selmon;
}

Client *
wintosystrayicon(Window w) {
	Client *i = NULL;

	if (!showsystray || !w)
		return i;
	for (i = systray->icons; i && i->win != w; i = i->next) ;
	return i;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	die("dwm: another window manager is already running");
	return -1;
}

void
xinitvisual()
{
	XVisualInfo *infos;
	XRenderPictFormat *fmt;
	int nitems;
	int i;

	XVisualInfo tpl = {
		.screen = screen,
		.depth = 32,
		.class = TrueColor
	};
	long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;

	infos = XGetVisualInfo(dpy, masks, &tpl, &nitems);
	visual = NULL;
	for(i = 0; i < nitems; i ++) {
		fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
		if (fmt && fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
			visual = infos[i].visual;
			depth = infos[i].depth;
			cmap = XCreateColormap(dpy, root, visual, AllocNone);
			useargb = 1;
			break;
		}
	}

	XFree(infos);

	if (! visual) {
		visual = DefaultVisual(dpy, screen);
		depth = DefaultDepth(dpy, screen);
		cmap = DefaultColormap(dpy, screen);
	}
}

void
zoom(const Arg *arg)
{
	Client *c = selmon->sel;

#if 0
	if (!selmon->lt[selmon->sellt]->arrange || !c || c->isfloating)
		return;
#endif
	if (!c) return;

	if (!zoom_swap_p) {
	if (c == nexttiled(selmon->clients) && !(c = nexttiled(c->next)))
		return;
	pop(c);
	} else {
		Client *at = NULL, *cold, *cprevious = NULL;
		if (c == nexttiled(selmon->clients)) {
			at = findbefore(prevzoom);
			if (at)
				cprevious = nexttiled(at->next);
			if (!cprevious || cprevious != prevzoom) {
				prevzoom = NULL;
				if (!c || !(c = nexttiled(c->next)))
					return;
			} else
				c = cprevious;
		}
		cold = nexttiled(selmon->clients);
		if (c != cold && !at)
			at = findbefore(c);
		detach(c);
		attach(c);
		/* swap windows instead of pushing the previous one down */
		if (c != cold && at) {
			prevzoom = cold;
			if (cold && at != cold) {
				detach(cold);
				cold->next = at->next;
				at->next = cold;
			}
		}
		focus(c);
		arrange(c->mon);
	}
}

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("dwm-"VERSION);
	else if (argc == 2 && !strcmp("-r", argv[1]))
		replace_wm = 1;
	else if (argc != 1)
		die("usage: dwm [-vr] ");
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("dwm: cannot open display");
	//checkotherwm(); runs too early. run it in setup
	setup();
#ifdef __OpenBSD__
	if (pledge("stdio rpath proc exec", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	scan();
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}

/* Returns the current X server time */
static Time
get_server_time(void)
{
	XEvent xev;
	XSetWindowAttributes attr;
	Window screen_support_win = wmcheckwin;

	/* add PropChange to NoFocusWin events */
	attr.event_mask = PropertyChangeMask;
	XChangeWindowAttributes (dpy, screen_support_win, CWEventMask, &attr);

	/* provoke an event */
	XChangeProperty(
		dpy, screen_support_win, XA_WM_CLASS, XA_STRING, 8, PropModeAppend,
		NULL, 0);

	XWindowEvent(dpy, screen_support_win, PropertyChangeMask, &xev);
	Last_Event_Time = xev.xproperty.time;

	/* restore NoFocusWin event mask */
	attr.event_mask = (KeyPressMask | KeyReleaseMask | FocusChangeMask);
	XChangeWindowAttributes(dpy, screen_support_win, CWEventMask, &attr);
	return xev.xproperty.time;
}

static void
icccm2_setup(int replace_wm)
{
	Window running_wm_win;
	XSetWindowAttributes attr;
	XEvent xev;
	XClientMessageEvent ev;
	char wm_sx[20];
	Atom _XA_WM_SX;
	Window screen_support_win = wmcheckwin;

	sprintf(wm_sx, "WM_S%u", screen);
	_XA_WM_SX = XInternAtom(dpy, wm_sx, False);

	/* Check for a running ICCCM 2.0 compliant WM */
	running_wm_win = XGetSelectionOwner(dpy, _XA_WM_SX);
	if (running_wm_win == screen_support_win)
		running_wm_win = None;
	if (running_wm_win != None)
	{
		if (!replace_wm)
		{
			fprintf(stderr, "icccm2_setup: another ICCCM 2.0 compliant WM is running. Try -r\n");
			exit(1);
		}
		/* We need to know when the old manager is gone. */
		attr.event_mask = StructureNotifyMask;
		XChangeWindowAttributes(
			dpy, running_wm_win, CWEventMask, &attr);
	}

	/* Have to get a timestamp manually by provoking a
	 * PropertyNotify. */
	Time managing_since = get_server_time();

	XSetSelectionOwner(dpy, _XA_WM_SX, screen_support_win, managing_since);
	if (XGetSelectionOwner(dpy, _XA_WM_SX) != screen_support_win)
	{
		fprintf(stderr,  "icccm2_setup failed to acquire selection ownership on screen %d", screen);
		exit(1);
	}

	/* Wait for the old wm to finish. */
	if (running_wm_win != None) {
		ulong wait = 0;
		ulong timeout = 1000000 * 15;  /* wait for 15s max */
		fprintf(stderr, "icccm2_setup waiting for WM to give up...");
		do {
			if (XCheckWindowEvent(dpy, running_wm_win, StructureNotifyMask, &xev))
				if (xev.type == DestroyNotify && xev.xany.window == running_wm_win) {
					fprintf(stderr, "done! after %lu microseconds", wait);
					break;
				}
			usleep(1000000 / 10);
			wait += 1000000 / 10;
		} while (wait < timeout);

		if (wait >= timeout) {
			fprintf(stderr, "The WM on screen %d is not exiting\n", screen);
			exit(0);
		}
	}

	/* Announce ourself as the new wm: */
	ev.type = ClientMessage;
	ev.window = DefaultRootWindow(dpy);
	ev.message_type = xatom[Manager];
	ev.format = 32;
	ev.data.l[0] = managing_since;
	ev.data.l[1] = _XA_WM_SX;
	XSendEvent(dpy, DefaultRootWindow(dpy), False, StructureNotifyMask,(XEvent*)&ev);
}

static void
selectionclear(XEvent *e)
{
	if ((e->xany).window == wmcheckwin) {
		fprintf(stderr, "icccm2_close: good luck, new wm\n");
		// cleanup() handles icccm2_close when it releases
		running = 0;
	} else
		fprintf(stderr, "ignoring selection clear event on window %lx selection %lx\n",
			(e->xany).window, ((XSelectionClearEvent *)e)->selection);
}