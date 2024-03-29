/* See LICENSE file for copyright and license details. */

//#include <XF86Keysym.h>

/* appearance */
static const unsigned int borderpx  = 1;        /* border pixel of windows */
/*static const*/ unsigned int gappx = 6;        /* gap pixel between windows */
static const unsigned int snap      = 32;       /* snap pixel */
static const unsigned int systrayspacing = 2;   /* systray spacing */
static       int showsystray        = 1;        /* False means no systray */
static const double shade           = 0.92;      /* opacity of unfocussed clients */
static const int showbar            = 1;        /* 0 means no bar */
static const int topbar             = 1;        /* 0 means bottom bar */
static const char dmenufont[]       = "monospace:size=10";
static const char *fonts[]          = { dmenufont };
static const char col_gray1[]       = "#222222";
static const char col_gray2[]       = "#444444";
static const char col_gray3[]       = "#bbbbbb";
static const char col_gray4[]       = "#eeeeee";
static const char col_cyan[]        = "#005577";
static const char *colors[][3]      = {
	/*               fg         bg         border   */
	[SchemeNorm] = { col_gray3, col_gray1, col_gray2 },
	[SchemeSel]  = { col_gray4, col_cyan,  col_cyan  },
};

/* alpha */
static const unsigned int alpha_scheme[3] = { 0xff, 0xdd, 0xff };

/* tagging */
static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

/* Query class:instance:title for regex matching info with following command:
 *
 * xprop | awk -F '"' '/^WM_CLASS/ { printf("%s:%s:",$4,$2) }; /^WM_NAME/ { printf("%s\n",$2) }'
 */

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class      instance    title       tags mask     isfloating   monitor   opacity, isterminal, noswallow, */
	{ "Gimp",     NULL,       NULL,       0,            1,           -1,       -1 },
	{ "Firefox",  NULL,       NULL,       1 << 8,       0,           -1,       -1 },
	{ "st-",      NULL,       NULL,       0,            0,           -1,       -1,      1,          1 },
#include "rules.fragment"
};

/* layout(s) */
static const int dirs[3]      = { DirHor, DirVer, DirVer }; /* tiling dirs */
static const float facts[3]   = { 1.1,    1.1,    1.1 };    /* tiling facts */

static const int nmaster     = 1;    /* number of clients in master area */
static const int n_non_master_columns = 2;
static       int resizehints = 1;    /* 1 means respect size hints in tiled resizals */
static const int lockfullscreen = 1; /* 1 will force focus on the fullscreen window */
static int lose_focus = 0; /* 1 will allow windows to lose focus when the pointer mouses out of the window area */
static int attach_bottom_p = 1;	/* 1 will make new clients attach at the bottom of the stack instead of the top. */
static int zoom_swap_p = 1; /* 1 will make zoom swap the current client with master rather than pushing the previous master down the stack */

#include "gaplessgrid.c"
#include "tatami.c"

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[]=",      tile },    /* first entry is default */
	{ "[M]",      monocle },
	{ "[G]",      gaplessgrid },
	{ "|+|",      tatami },
};

static int raise_on_click = 1; //madhu 230510

#include "patch-madhu.c"

/* key definitions */
#define MODKEY Mod1Mask
#define MODKEY_ALT Mod4Mask
#define MODKEY Mod4Mask //XXX
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },

#define TILEKEYS(MOD,G,M,S)						\
	{ MOD, XK_a, setdirs,  {.v = (int[])  { INC(G * +1),   INC(M * +1),   INC(S * +1) } } }, \
	{ MOD, XK_x, setfacts, {.v = (float[]){ INC(G * -0.1), INC(M * -0.1), INC(S * -0.1) } } }, \
	{ MOD, XK_z, setfacts, {.v = (float[]){ INC(G * +0.1), INC(M * +0.1), INC(S * +0.1) } } },

/*
#define TILEKEYS(MOD,G,M,S)						\
	{ MOD, XK_r, setdirs,  {.v = (int[])  { INC(G * +1),   INC(M * +1),   INC(S * +1) } } }, \
	{ MOD, XK_h, setfacts, {.v = (float[]){ INC(G * -0.1), INC(M * -0.1), INC(S * -0.1) } } }, \
	{ MOD, XK_l, setfacts, {.v = (float[]){ INC(G * +0.1), INC(M * +0.1), INC(S * +0.1) } } },
*/

#define STACKKEYS(MOD,ACTION)				   \
	{ MOD, XK_j,     ACTION##stack, {.i = INC(+1) } }, \
	{ MOD, XK_k,     ACTION##stack, {.i = INC(-1) } }, \
	{ MOD, XK_grave, ACTION##stack, {.i = PREVSEL } }, \
	{ MOD, XK_F1,    ACTION##stack, {.i = 0 } }, \
	{ MOD, XK_F2,    ACTION##stack, {.i = 1 } }, \
	{ MOD, XK_F3,    ACTION##stack, {.i = 2 } }, \
	{ MOD, XK_F4,    ACTION##stack, {.i = 3 } }, \
	{ MOD, XK_F5,    ACTION##stack, {.i = 4 } }, \
	{ MOD, XK_F6,    ACTION##stack, {.i = 5 } }, \
	{ MOD | ShiftMask, XK_F1,     ACTION##stack, {.i = -1 } },

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static char dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
static const char *dmenucmd[] = { "dmenu_run", "-m", dmenumon, /* "-fn", "fixed", */ "-nb", col_gray1, "-nf", col_gray3, "-sb", col_cyan, "-sf", col_gray4, NULL };
//static const char *termcmd[]  = { "st", NULL };
static const char *termcmd[]  = { "xterm", NULL };

enum placement_style placement_style = under_mouse;

static const char *roficmd[] = { "/usr/bin/rofi", "-show", "window", "-nb", "#2b4e5e", NULL };

static const Key keys[] = {
	/* modifier                     key        function        argument */
	{ MODKEY|ControlMask|ShiftMask, XK_g,      identify_wintitle,   {0} },
	{ MODKEY|ShiftMask|ControlMask, XK_s,	   toggle_systray,	{0} }, // madhu 130424

	{ MODKEY,		        XK_p,      spawn,          {.v = dmenucmd } },
	{ MODKEY|ShiftMask,             XK_Return, spawn,          {.v = termcmd } },
	{ MODKEY|ShiftMask,             XK_b,      togglebar,      {0} },
//stack	{ MODKEY,                       XK_j,      focusstack,     {.i = +1 } },
//stack	{ MODKEY,                       XK_k,      focusstack,     {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_i,      incnmaster,     {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_d,      incnmaster,     {.i = -1 } },

	{ MODKEY|ShiftMask|ControlMask, XK_i,      incnstackcols,  {.i = +1 } },
	{ MODKEY|ShiftMask|ControlMask, XK_d,      incnstackcols,  {.i = -1 } },

//xtile	{ MODKEY|ShiftMask,             XK_h,      setmfact,       {.f = -0.05} },
//xtile	{ MODKEY|ShiftMask,             XK_l,      setmfact,       {.f = +0.05} },
	{ MODKEY,                       XK_Return, zoom,           {0} },
	{ MODKEY,                       XK_Tab,    view,           {0} },
	{ MODKEY|ShiftMask,             XK_c,      killclient,     {0} },
	{ MODKEY|ShiftMask,             XK_t,      setlayout,      {.v = &layouts[0]} },
	{ MODKEY|ShiftMask,             XK_f,      setlayout,      {.v = &layouts[1]} },
	{ MODKEY|ShiftMask,             XK_m,      setlayout,      {.v = &layouts[2]} },
	{ MODKEY|ShiftMask,             XK_g,      setlayout,      {.v = &layouts[3]} },
	{ MODKEY|ShiftMask,              XK_y,      setlayout,      {.v = &layouts[4]} }, /* tatami */
	{ MODKEY|ShiftMask,             XK_space,  setlayout,      {0} },
	{ MODKEY|ShiftMask|ControlMask, XK_space,  togglefloating, {0} },
	{ MODKEY,                       XK_0,      view,           {.ui = ~0 } },
	{ MODKEY|ShiftMask,             XK_0,      tag,            {.ui = ~0 } },
//	{ MODKEY,                       XK_comma,  focusmon,       {.i = -1 } },
//	{ MODKEY,                       XK_period, focusmon,       {.i = +1 } },
//	{ MODKEY|ShiftMask,             XK_comma,  tagmon,         {.i = -1 } },
//	{ MODKEY|ShiftMask,             XK_period, tagmon,         {.i = +1 } },
	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_KP_6,                   5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)
	{ MODKEY|ShiftMask|ControlMask, XK_q,      quit,           {0} },

	//toggleopacity
	{ MODKEY|ShiftMask|ControlMask, XK_l,      toggleopacity,  {0}, },
	{ MODKEY|ShiftMask|ControlMask, XK_j,      toggleopacity,  {.f = +0.1}, },
	{ MODKEY|ShiftMask|ControlMask, XK_k,      toggleopacity,  {.f = -0.1}, },
	//xtile
	TILEKEYS(MODKEY_ALT,                                       1, 0, 0)
	TILEKEYS(MODKEY_ALT|ShiftMask,                             0, 1, 0)
	TILEKEYS(MODKEY_ALT|ControlMask,                           0, 0, 1)
	TILEKEYS(MODKEY_ALT|ShiftMask|ControlMask,                 1, 1, 1)
//	{ MODKEY_ALT|ShiftMask,         XK_t,      setdirs,        {.v = (int[]){ DirHor, DirVer, DirVer } } },
//	{ MODKEY_ALT|ControlMask,       XK_t,      setdirs,        {.v = (int[]){ DirVer, DirHor, DirHor } } },

	{ MODKEY_ALT|ShiftMask,         XK_v,      setdirs,        {.v = (int[]){ DirHor, DirVer, DirVer } } },
	{ MODKEY_ALT|ControlMask,       XK_v,      setdirs,        {.v = (int[]){ DirVer, DirHor, DirHor } } },

	//stacker
	STACKKEYS(MODKEY,                          focus)
	STACKKEYS(MODKEY|ShiftMask,                push)

	{ MODKEY|ShiftMask,             XK_e,      cycle_layouts,  {0}, }, // madhu 101213
	{ MODKEY|ShiftMask,		XK_q,	   startwm,	   {.v = "exec dwm < /dev/null" } }, // madhu 070530
	{ MODKEY|ShiftMask,             XK_r,	   toggle_resizehints, {0}}, // madhu 080917
	{ MODKEY|ShiftMask|ControlMask, XK_r,	   toggle_fixed,   {0}}, // madhu 120923
	{ MODKEY|ShiftMask,             XK_w,      windowlist,     {0} }, // madhu 130402
	{ MODKEY|ShiftMask|ControlMask, XK_f,      myfocus,        {0} }, // madhu 090403
	{ MODKEY|ShiftMask|ControlMask, XK_u,      unswallow_now,   {0} }, //;madhu 231223
	{ MODKEY|ShiftMask,             XK_u,      focusurgent,    {0} }, //madhu 170621
	{ MODKEY|ShiftMask|ControlMask, XK_w,	   mywarp,	   {0} }, // madhu 170814
	{ MODKEY|ShiftMask|ControlMask,	XK_p,	   toggle_placement_style, {0} }, //madhu 180601
	{ MODKEY|ShiftMask|ControlMask, XK_o,      toggle_raise_on_click, {0} }, //madhu 230510

	{ MODKEY,			XK_w,	   spawn,	   {.v = roficmd} }, // madhu 230816

	{ MODKEY|ShiftMask,             XK_o,      togglelosefocus, {0} }, //madhu 231016
	{ MODKEY|ShiftMask|ControlMask, XK_b,      toggle_attach_bottom, {0} }, //;madhu 240201
	{ MODKEY|ShiftMask|ControlMask, XK_n,      toggle_zoom_swap, {0} }, //;madhu 240201
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static const Button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClkLtSymbol,          0,              Button1,        setlayout,      {0} },
	{ ClkLtSymbol,          0,              Button3,        setlayout,      {.v = &layouts[2]} },
	{ ClkWinTitle,          0,              Button2,        zoom,           {0} },
	{ ClkStatusText,        0,              Button2,        spawn,          {.v = termcmd } },
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
	{ ClkTagBar,            0,              Button1,        view,           {0} },
	{ ClkTagBar,            0,              Button3,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },

	{ ClkClientWin,         0,              Button1,        myfocus,        {0} }, // madhu 090403
	{ ClkWinTitle,          0,              Button1,        focusclienttaskbar, {0} }, // taskbar_click
};

