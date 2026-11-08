/*
SOCIAL_MENU.H

The Social screen (social_menu.c): the main menu's Y, and Settings >
Gameplay > Social > Chat's BLOCKED & MUTED. The account's friends and what
they are doing, the friend requests, the friends' latest matches, the
players met in the chat, adding a friend by code, any player's records
(totals, matches, every medal's count) and what can be done about them
(friends, a private line, joining the game they host, muting, blocking,
reporting), and the players muted and blocked. Everything is the community
server's (api-opence's /friends, /players, /activity, /moderation), asked as
the install's account (social.h).

The lists, each opened from the one before (B goes back, from the first
away), as the Map Manager's are (map_manager.h): menu_functions.c's "port
social" functions show their rows and the row chosen's details.
	HOME          MY ACCOUNT, FRIENDLIST, ACTIVITY, SETTINGS (the social
	              settings screen)
	ACCOUNT       ACCOUNT SUMMARY, SYNC ACCOUNT
	FRIENDLIST    YOUR CODE, FRIEND REQUESTS, ADD FRIEND, FRIENDS,
	              BLOCKED & MUTED
	ACTIVITY      RECENT PLAYERS, MATCH HISTORY
	IDENTITY      CHAT NAME, CHAT COLOR (opened from Chat settings)
	FRIENDS       ADD FRIEND, then the friends, online first (a JOIN GAME
	              under each whose game may be joined)
	REQUESTS      those that asked to be friends, then those asked
	MATCH HISTORY the friends' latest matches and levels
	RECENT        the players of the chat (this game's room, and those
	              written to)
	CARD          one player's card, drawn where the rows are (as the
	              level's summary is: social_menu_card_draw): what they are
	              doing, their multiplayer and campaign totals, every
	              medal's count; its OPTIONS button (another's only) shows
	              what can be done
	PLAYER        LATEST MATCHES, then what can be done (JOIN GAME, SEND
	              MESSAGE, the friend request's, MUTE, BLOCK, REPORT)
	MATCHES       its latest matches and levels
	MODERATION    the players muted and blocked
	REPORT        why it is reported (A twice reports it)
*/

#ifndef __SOCIAL_MENU_H
#define __SOCIAL_MENU_H

/* ---------- constants */

/* what the menu does after a row is chosen (social_menu_choose) */
enum
{
	/* it stays, its rows as they are now */
	_social_menu_stays,
	/* the keyboard was opened for a friend's code (social_menu_poll takes
	what is typed) */
	_social_menu_typing,
	/* a friend's game joined: its invite (social_menu_join_invite), which
	Direct Link shows */
	_social_menu_join,
	/* a friend's public game with a password: its listing's key
	(social_menu_join_host_key), which the server browser finds it by and
	asks for its password */
	_social_menu_join_listed,
	/* SETTINGS: the social settings opened (menu_functions.c) */
	_social_menu_settings,
};

/* where the screen opens (social_menu_open_at) */
enum
{
	_social_menu_open_home,
	_social_menu_open_moderation,
	/* (Settings > Gameplay > Community > Social's MY PROFILE: your card) */
	_social_menu_open_profile,
	/* Chat settings' CHAT IDENTITY: chat name and colour */
	_social_menu_open_identity,
	/* (the main menu's social column: a friend's card,
	social_menu_home_friend_open) */
	_social_menu_open_player,
	/* the player's first setup (social_menu_setup_tick) */
	_social_menu_open_setup,
};

/* ---------- prototypes/SOCIAL_MENU.C */

/* (social.c, each frame) the players muted and blocked asked for once the
account is had, again after one is changed: the chat's (social_chat.h) */
void social_menu_update(void);
/* (social_chat.c's poll, social.c's presence) the key of the account's
friendships the server said ("" for none said): the friends asked for again
when it changed, and what changed said in the chat (a request to you, yours
accepted or declined) */
void social_menu_friends_key(char const *key);

/* the list the screen opens at next (_social_menu_open_*), set before the
screen is opened */
void social_menu_open_at(short where);

/* the player's first setup: name, chat colour and emblem, on the Social
screen. Each frame of the main menu: an account's rank caught up, a profile
made for a new device of an account set up elsewhere, and TRUE when the screen
is to open now (the main menu's Y). wanted: the Y opens it. closing: once,
as it is done and goes (B) */
boolean social_menu_setup_tick(void);
boolean social_menu_setup_wanted(void);
boolean social_menu_setup_closing(void);
/* whether a profile of the player's own exists (the stock ones aside) */
boolean social_menu_has_profile(void);

/* the screen opened (the server asked for its lists), or closed */
void social_menu_open(void);
void social_menu_close(void);

/* each frame the screen is shown, before its rows are: the server's answers
taken, the lists asked for again a while after, and what was typed taken */
void social_menu_poll(void);

/* the list shown (an index of its own, which changes as lists are opened),
how many rows it has, and the first shown and the one chosen (the screen's
to keep) */
short social_menu_list(void);
short social_menu_count(void);
short *social_menu_first(void);
short *social_menu_chosen(void);

struct firefight_mcc_rank_info;
struct firefight_emblem_info;

/* a row's name and state (UTF-8 text) */
void social_menu_row(short row, char *name, long name_size, char *state, long state_size);
/* the platform icon (platform_icons.h) of a friend's row, none for any other */
short social_menu_row_platform(short row);
/* the service rank and emblem of a person row (_row_friend, _row_incoming, _row_outgoing, _row_recent_player) */
/* the emblem a row shows in its state's place (CUSTOMISE EMBLEM's) */
boolean social_menu_row_emblem(short row, struct firefight_emblem_info *out_emblem, short *out_part);
boolean social_menu_row_person_icons(short row, struct firefight_mcc_rank_info *out_rank, struct firefight_emblem_info *out_emblem);

/* the row chosen's details: its title and its text, the last thing done
first */
void social_menu_details(short row, char *title, long title_size, char *text, long text_size);

/* Left/Right step on customizable rows (e.g. MY ACCOUNT -> CUSTOMISE EMBLEM) */
boolean social_menu_step(short row, short delta);
/* Custom overlay rendered inside social_details_text (e.g. Live Emblem & Nameplate preview) */
void social_menu_details_overlay_draw(short row, rectangle2d const *bounds);

/* (menu_functions.c, as the screen's social_card text box is drawn: its
bounds, in the view's pixels) the player's card, while it is shown */
void social_menu_card_draw(short x0, short y0, short x1, short y1);

/* (menu_functions.c) the main menu's social column: drawn over its
social_home text box (the menus' 640; the column focused, and the row
chosen: a friend's, or past them the chat's), its friends' rows, the row a
click hits (NONE for none) or whether it is in the column at all, and the
Social screen set to open at a friend's card (or, from the docked chat, at
the card of the player of this code and name) */
void social_menu_home_draw(short x0, short y0, short x1, short y1, boolean focused, short chosen);
short social_menu_home_rows(void);
short social_menu_home_hit(short x, short y);
boolean social_menu_home_inside(short x, short y);
void social_menu_home_friend_open(short row);
void social_menu_player_open_next(char const *code, char const *name);
/* (menu_functions.c, the main menu's Social hint, yellow while there are
any) the friend requests waiting for you */
long social_menu_requests_waiting(void);

/* a player's card is shown (the list's details and frame hidden); it has
the OPTIONS button (another player's); the button pressed: what can be done
about them */
boolean social_menu_card_shown(void);
/* the card's CREATOR PROFILE / SERVICE RECORD button (A with the focus on
it, Up from the button bar giving it the focus; or a click on where it was
drawn): the card turned to the other (FALSE: no card to turn); whether it
has the focus, and giving it or taking it (FALSE: no card's button) */
boolean social_menu_card_creator_toggle(void);
boolean social_menu_card_button_hit(short x, short y);
boolean social_menu_card_button_focused(void);
boolean social_menu_card_button_focus(boolean focused);
boolean social_menu_card_has_options(void);
void social_menu_card_options(void);
/* ... and the JOIN button (another's card, a game that may be joined: its
invite or its listing's key), pressed: _social_menu_join, _join_listed or
_stays, as a JOIN GAME row's */
boolean social_menu_card_has_join(void);
short social_menu_card_join(void);

/* A on a row: _social_menu_stays, _typing, _join, _join_listed or _settings */
short social_menu_choose(short row);

/* B: up a list; FALSE in the first (the screen leaves) */
boolean social_menu_back(void);

/* the invite of the game _social_menu_join joins ("" for none) */
char const *social_menu_join_invite(void);
/* the listing key (hex) of the game _social_menu_join_listed joins ("" for
none) */
char const *social_menu_join_host_key(void);

/* (menu_functions.c) the join could not be started: said on the screen */
void social_menu_join_failed(void);

/* (menu_functions.c) the join's panel over every screen while it goes on:
begun as the join is (_social_menu_join or _join_listed), ended once the
screen behind it is left or what it waited for came (NULL), or with why it
failed (said for a few seconds); it ends by itself once the game's lobby
is reached */
void social_menu_join_begin(short kind);
void social_menu_join_end(char const *failure);
boolean social_menu_join_active(void);
/* (Direct Link's) the host key hash (hex) of the friend's game to choose
and join as soon as it shows, NULL for none; the A press posted on it */
char const *social_menu_join_seeking(void);
void social_menu_join_posted(void);
/* (social_chat.c's draw, over everything) the panel, in the view's pixels */
void social_menu_join_draw(short screen_width, short screen_height);

/* ---------- the chat's: a player's name clicked (social_chat.c) */

/* what the chat's player menu asks of a player */
enum
{
	_social_menu_act_friend,
	_social_menu_act_mute,
	_social_menu_act_unmute,
	_social_menu_act_report,
};

/* a friend request, a mute, an unmute or a report (as from chat) of the
player of this code (its name as shown): what it did, or why not, said as
a notice of the chat's */
void social_menu_player_act(char const *code, char const *name, short action);
/* the player's records asked for (the Social screen's player page's,
shared), and a few lines of them: 1 had, 0 still asked, -1 not answered
(why in text) */
void social_menu_profile_ask(char const *code, char const *name);
short social_menu_profile_text(char const *code, char *text, long size);

/* the widest the player's card is, in the screen's 480 lines' units */
#define SOCIAL_MENU_CARD_MOST_WIDTH 860

/* (social_chat.c's VIEW PROFILE) the player's card, as the Social screen
draws it (asked for: social_menu_profile_ask), in the view's pixels from x0
to x1 and from y0 down, `unit` pixels to one of the screen's 480 lines: its
bottom (y0 for none drawn) */
short social_menu_profile_card_draw(char const *code, short x0, short y0, short x1, real unit);
/* whether the player is a friend, and muted, as the server last said */
boolean social_menu_is_friend(char const *code);
boolean social_menu_is_muted(char const *code);

#endif
