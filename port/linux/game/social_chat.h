/*
SOCIAL_CHAT.H

This fork's chat (social_chat.c): one chat box, in the game and the menus
alike, whose lines are network_chat.c's (the upstream chat, which it hides)
and the community server's (the fork's chat: social.c's account), each line
marked with where it came from. What the main loop, the drawing, the
keyboard and the controllers (through network_chat.c's typing state) and the
upstream chat ask of it.
*/

#ifndef __SOCIAL_CHAT_H
#define __SOCIAL_CHAT_H

/* where a line goes: the match's players (the upstream chat's everyone, and
the fork's players in the same game), the team's, one account, or every
player of the fork's online */
enum
{
	_social_chat_match,
	_social_chat_team,
	_social_chat_private,
	_social_chat_global,
	NUMBER_OF_SOCIAL_CHAT_CHANNELS
};

/* (social.c) each frame: the lines asked for and sent, the box opened, and
what is typed */
void social_chat_update(void);
/* (interface.c, over the whole screen before the overlay menu) the box */
void social_chat_draw(void);

/* social.chat: the box is shown in place of the upstream chat's lines, and
the chat's keys open it (network_chat.c asks) */
boolean social_chat_replaces_upstream(void);
/* the main menu's chat box is on (social.enabled, social.chat, display.text_chat) */
boolean social_chat_dock_shown(void);
/* (network_chat.c, as its keys are pressed in a network game) the box opened
to a channel (_social_chat_*), by a controller's port (NONE: the keyboard,
which types) */
void social_chat_open(short channel, short port);
/* (social_menu.c's SEND MESSAGE) the box opened to PRIVATE, to the account
of this code (its name as shown), typed on the keyboard or the on-screen
one as the player last used */
void social_chat_open_private(char const *code, wchar_t const *name);
/* (social_menu.c's RECENT PLAYERS) the accounts a private line may go to
(this game's room's, those that wrote or were written to), the newest first:
how many, at most maximum */
short social_chat_recent_players(char (*codes)[16], wchar_t (*names)[32], short maximum);
/* (social_menu.c) the names of the players muted or blocked: their
upstream lines hidden (the server leaves out their fork lines) */
void social_chat_unheard_set(wchar_t const (*names)[32], short count);
/* (social_menu.c) a line of the box's own: what a player's menu asked did */
void social_chat_notice_text(char const *text);
/* (ui_widget.c, as the overlay menu's) the box open takes the pointer, in
the game and the menus: a player's name clicked or tapped opens its menu */
struct halo_ui_pointer;
int social_chat_wants_pointer(void);
void social_chat_pointer(struct halo_ui_pointer const *pointer);
/* (menu_functions.c, as the main menu's social column is drawn) the box
drawn docked there, with the menus (in their 640; shift: the screen's
coordinates' from them), chosen there or not; it stays docked while drawn
again within a moment (the overlay draws its on-screen keyboard and a
player's menu by it). Opened from there: its keyboard is the controller's of
this port (the keyboard's, when it was typed on last) */
void social_chat_draw_docked(short x0, short y0, short x1, short y1, short shift, boolean chosen);
void social_chat_open_docked(short port);
/* (menu_functions.c, the main menu's right stick) docked and closed, a line
chosen, shown by an arrow: step -1 one older (chosen first: the newest),
1 one newer (past the newest: none chosen); whether that changed anything.
The chosen line's player's card: the Social screen set to open at it
(social_menu.h), and where it opens (_social_menu_open_*; NONE: none, a
notice's or an upstream player's not of the fork's chat) */
boolean social_chat_dock_pick_step(short step);
/* (menu_functions.c's lobby) whether a docked chat's line is chosen */
boolean social_chat_dock_picked(void);
short social_chat_dock_pick_open(void);
/* (menu_functions.c's lobby) a lobby player's row picked: the chat's
player's menu for them (their account's, if a line of the chat names them
with one), opened on the port's controller (NONE: the keys), its top at top
(the screen's); and the host's own on them: FLAG(SOCIAL_CHAT_LOBBY_*_BIT). FALSE when
there is nothing to do */
enum
{
	SOCIAL_CHAT_LOBBY_KICK_BIT = 0,
	SOCIAL_CHAT_LOBBY_REMOVE_BOT_BIT,
	SOCIAL_CHAT_LOBBY_BOT_BIT,
};
boolean social_chat_lobby_player_menu(wchar_t const *name, long machine_index, unsigned long actions, short port,
	short top);
/* (a controller's Right) the chosen line's player's menu (as a name
clicked: send a private line, add, profile, mute, report), the box opened
for it alone by this port and closed with it; FALSE for none (a notice, your
own line) */
boolean social_chat_dock_pick_menu(short port);
/* (menu_functions.c) the box takes the keys and the controller: open, or
closed a moment ago (its B and Start not the menus') */
boolean social_chat_busy(void);
/* (overlay_menu.c) L, R and the d-pad's up pressed together on a
controller's port: the box opened, in the game or the menus, or closed */
void social_chat_toggle_request(short port);

/* (network_chat.c's typing state, which the platform's input reads) a line
is typed in the box; with the on-screen keyboard; with the system's keyboard
in its place (halo_system_keyboard); and its controller's port (NONE for
none) */
boolean social_chat_typing(void);
boolean social_chat_keyboard_open(void);
boolean social_chat_system_keyboard(void);
/* open on a touchscreen, typed with the system's keyboard, up or not */
boolean social_chat_system_open(void);
short social_chat_controller(void);

/* (network_chat.c) a line the upstream chat shows: its player's name, its
team's colour (0 red, 1 blue, NONE none), to the team or not, its text; and
a notice of the host's */
void social_chat_upstream_line(wchar_t const *name, short color, boolean team, char const *text);
void social_chat_upstream_notice(char const *text);

#endif
