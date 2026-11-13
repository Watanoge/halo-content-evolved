/*
SOCIAL_CHAT.C

This fork's chat (social_chat.h): one box for the upstream chat's lines
(network_chat.c, whose own lines it hides) and the fork's, which go through
the community server as the install's account (social.c; api-opence's
/chat/messages), in the game and the menus alike.

- The channels: MATCH (the upstream chat's everyone, and the fork's players
  in the same network game), TEAM (the team's, both ways too), PRIVATE (one
  account: a player of the fork's in the same game, or one who wrote to this
  one) and GLOBAL (every player of the fork's online). A line goes through
  the upstream chat where the game's host lets it (network_chat_available),
  and through the fork's wherever it can (the community server reached, a
  network game's room for MATCH and TEAM): its fork players hear it whatever
  the host's settings or the server browser's rules, its other players as
  the host lets them.
- A line heard both ways (a fork player's, said both ways) is shown once:
  the second, the same player's same text within SOCIAL_CHAT_SAME_MILLISECONDS,
  marks the first as heard both ways. A MATCH or TEAM line shows where it
  came from: U (the upstream chat), F (the fork's), U+F; a GLOBAL or PRIVATE
  one is the fork's alone, and says nothing. Locally sent lines merge even
  when the upstream profile name and the fork account name differ, and
  whichever echo arrives first.
- A line's name is the one its account picked (Social's CHAT NAME, the same
  on every device) in the colour it picked (NAME COLOR), else its profile's.
- A GLOBAL or PRIVATE line may be SOCIAL_CHAT_SAID_SIZE - 1 characters; a MATCH
  or TEAM one the upstream chat's (SOCIAL_CHAT_UPSTREAM_SAID_SIZE - 1), which
  its 64-byte lines hold. The text is drawn at SOCIAL_CHAT_TEXT_SCALE.
- A network game's room is a key every machine in it makes alike: a hash of
  the game's name, its map and its random seed (network_game), which the
  host sends every client.
- Opening it: in a network game, the upstream chat's keys (network_chat.c
  asks social_chat_open: T, Y, D-pad Up and the controls' chat actions);
  elsewhere (the menus, the campaign), T and Y on the keyboard; in the
  campaign, D-pad Up (the touch controls' too) and the controls' chat
  actions as well (network_chat.c); on any controller, L, R and the d-pad's
  up together (overlay_menu.c). While it is
  open: Tab (or L and R) the channel, Up and Down (or Y) who a private line
  goes to, Page Up and Page Down (or the wheel) back through the lines,
  Enter (or Start) sends, Escape (or B) closes. A controller types on the
  on-screen keyboard as the upstream chat's (the stick and the D-pad move,
  A presses, X deletes), the touchscreen taps its keys; on Android, the
  touchscreen types with Android's keyboard instead, as a player profile's
  name does (halo_system_keyboard), which never comes up by itself (to read
  the lines, change the channel, tap a name): a tap on the line typed, with
  its keyboard button, brings it; the back closes the box. A tap on the
  docked box (menus, lobby) opens it and goes on to it: a name tapped opens
  its menu at once. A click or a tap on a channel's name picks it, on
  the private line's player the next one, and outside the box (its keyboard,
  a player's menu) closes it. No hint of the keys is shown. The platform's
  input takes it as the upstream chat's typing (network_chat_typing and the
  rest ask here).
- Docked (social_chat_dock: the main menu's social column asks each frame
  it is drawn), the box is in that place, its lines shown closed too and not
  faded, its channels and where a line is typed with them; opened from
  there (social_chat_open_docked: A on it, or a click), its on-screen
  keyboard left of it. While it is open, and a moment after it closes, the
  menus get none of the keys or the controller (social_chat_busy).
- Closed, the latest lines show a while and fade (not in a cinematic);
  display.text_chat hides it all. social.chat turns it on (else the upstream
  chat is as it was); social.global_chat whether GLOBAL is heard, and
  social.global_everywhere whether its lines show with the others in a
  network game (else only with the box open at GLOBAL, which then shows them
  alone; outside one they always show);
  social.private_chat whether PRIVATE is (the server keeps others' private
  lines from it too); social.show_reported whether the lines of players many
  others reported are (marked REPORTED: the server leaves them out else).
- A player's name in the open box, clicked or tapped, opens its menu beside
  the box: SEND PRIVATE MESSAGE (social.private_chat), ADD FRIEND
  (social.friends, not a friend yet), VIEW PROFILE (its card, as the
  Social screen's, over the middle of the screen), MUTE or UNMUTE, REPORT (A
  twice); Up and Down, Enter, Escape on the keyboard. A name the fork's chat
  knows no account of (an upstream line's player not of the fork's) opens
  MUTE IN THIS MATCH (or UNMUTE) alone: its lines hidden here, by its name,
  until the game's room changes. No player's code is shown: another
  account is known by the public id the server gives (social_menu.c asks
  the server).
- The players muted or blocked (social_menu.c's moderation list) are never
  heard: the server leaves their fork lines out, and their upstream lines
  (known by their names alone, as the upstream chat has no accounts) are
  hidden here.

The fork's lines are asked for every SOCIAL_CHAT_POLL_FAST_MILLISECONDS in a
network game or with the box open, else every SOCIAL_CHAT_POLL_MILLISECONDS:
the server answers those after the last one had (its cursor). As IRC, there
is no history: the first time it answers only where to go on from (a server
that still sends the last few lines has them left out here), and the lines
are kept in memory alone, gone with the game.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "input/input.h"
#include "interface/hud.h"
#include "interface/interface.h"
#include "interface/ui_widget.h"
#include "main/console.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"

#include "halo_firefight.h"
#include "halo_keyboard.h"
#include "network_chat.h"
#include "network_replay.h"
#include "social.h"
#include "social_chat.h"
#include "social_menu.h"
#include "halo_ui_pointer.h"
#include "../src/community_net.h"
#include "../src/port_config.h"
#include "../src/server_json.h"

#include <stdio.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);
unsigned long config_changes(void);
int platform_clipboard_get(char *text, int size);
long halo_screen_width(void);

/* ---------- constants */

/* the chat's text drawn at this part of the font's size, to fit more of it */
#define SOCIAL_CHAT_TEXT_SCALE 0.85f

enum
{
	/* a line's text, with its end: the upstream chat's (a line said both
	ways is the same both ways); one of the fork's heard, longer */
	SOCIAL_CHAT_SAID_SIZE = 128,
	/* ... a MATCH or TEAM line stays the upstream chat's size: it goes to the
	game's other machines in its 64-byte lines (network_chat.c) */
	SOCIAL_CHAT_UPSTREAM_SAID_SIZE = 64,
	SOCIAL_CHAT_TEXT_SIZE = 204,
	SOCIAL_CHAT_NAME_SIZE = 33,
	SOCIAL_CHAT_CODE_SIZE = 16,
	SOCIAL_CHAT_ROOM_SIZE = 24,
	/* the lines kept, and those shown at once: open, and closed */
	SOCIAL_CHAT_MAXIMUM_LINES = 96,
	SOCIAL_CHAT_LINES_OPEN = 10,
	SOCIAL_CHAT_LINES_CLOSED = 5,
	/* the rows a line wraps to, at most */
	SOCIAL_CHAT_LINE_ROWS = 4,
	/* a line shown closed, then faded out */
	SOCIAL_CHAT_LINE_MILLISECONDS = 10000,
	SOCIAL_CHAT_FADE_MILLISECONDS = 1000,
	/* the same line heard the other way, this soon */
	SOCIAL_CHAT_SAME_MILLISECONDS = 8000,
	/* the fork's lines asked for */
	SOCIAL_CHAT_POLL_FAST_MILLISECONDS = 1500,
	SOCIAL_CHAT_POLL_MILLISECONDS = 6000,
	/* ... again after the server was not reached */
	SOCIAL_CHAT_POLL_RETRY_MILLISECONDS = 15000,
	/* the players a private line may go to, at most */
	SOCIAL_CHAT_MAXIMUM_PEERS = 32,
	/* the names of the players muted or blocked kept, at most; and of those
	muted in this match alone */
	SOCIAL_CHAT_MAXIMUM_UNHEARD = 64,
	SOCIAL_CHAT_MAXIMUM_MUTED_HERE = 32,
	/* the names that can be clicked, at most (the lines shown open) */
	SOCIAL_CHAT_MAXIMUM_NAME_HITS = 16,
	/* a player's menu: its options at most, its width in hundredths of the
	screen's, and its profile's lines */
	SOCIAL_CHAT_PLAYER_OPTIONS = 8,
	SOCIAL_CHAT_PLAYER_WIDTH_PERCENT = 30,
	SOCIAL_CHAT_PLAYER_TEXT_SIZE = 512,
	SOCIAL_CHAT_PLAYER_CONFIRM_MILLISECONDS = 4000,
	SOCIAL_CHAT_CARET_MILLISECONDS = 500,
	/* a toggle this soon after the box opened is the same press (L, R and
	up in a network game: its up opens it too, network_chat.c) */
	SOCIAL_CHAT_TOGGLE_MILLISECONDS = 300,
	SOCIAL_CHAT_PAGE_LINES = 3,
	/* the on-screen keyboard: as the upstream chat's (network_chat.c) */
	SOCIAL_CHAT_KEYBOARD_COLUMNS = 10,
	SOCIAL_CHAT_KEYBOARD_ROWS = 5,
	SOCIAL_CHAT_KEYBOARD_WIDE_COLUMNS = 2,
	SOCIAL_CHAT_KEYBOARD_REPEAT_FIRST_MILLISECONDS = 350,
	SOCIAL_CHAT_KEYBOARD_REPEAT_MILLISECONDS = 120,
	SOCIAL_CHAT_KEYBOARD_PERCENT = 44,
	SOCIAL_CHAT_KEY_GAP = 2,
	/* the box: its left and width, and (closed, or with no touch controls)
	its bottom, in hundredths of the screen's; with the touch controls its
	top instead */
	SOCIAL_CHAT_LEFT_PERCENT = 2,
	SOCIAL_CHAT_WIDTH_PERCENT = 46,
	SOCIAL_CHAT_BOTTOM_PERCENT = 72,
	SOCIAL_CHAT_TOUCH_TOP_PERCENT = 14,
	SOCIAL_CHAT_PADDING = 6,
	/* docked, its lines further in, for the arrow by the line chosen */
	SOCIAL_CHAT_DOCK_INDENT = 6,
	/* a key's Ctrl (struct key_stroke's modifier_flags: menu_functions.c's) */
	SOCIAL_CHAT_KEY_MODIFIER_CONTROL_BIT = 1,
};

/* where a line came from (a bit each) */
enum
{
	_social_chat_upstream_bit,
	_social_chat_fork_bit,
};

/* a line's name's colour */
enum
{
	_social_chat_color_none,
	_social_chat_color_red,
	_social_chat_color_blue,
	_social_chat_color_notice,
	_social_chat_color_mine,
};

/* a player's menu's options */
enum
{
	_social_chat_option_private,
	_social_chat_option_friend,
	_social_chat_option_profile,
	_social_chat_option_mute,
	_social_chat_option_unmute,
	_social_chat_option_report,
	/* (a player of the upstream chat's alone, by its name, in this match) */
	_social_chat_option_mute_here,
	_social_chat_option_unmute_here,
	/* (the lobby's, a player's row: social_chat_lobby_player_menu) */
	_social_chat_option_kick,
	_social_chat_option_remove_bot,
};

/* the on-screen keyboard's last row's wide keys */
enum
{
	_social_chat_key_shift,
	_social_chat_key_symbols,
	_social_chat_key_space,
	_social_chat_key_delete,
	_social_chat_key_send,
};

/* ---------- structures */

struct social_chat_line
{
	wchar_t name[SOCIAL_CHAT_NAME_SIZE];
	char text[SOCIAL_CHAT_TEXT_SIZE];
	/* its player's account's code ("" for one not known: a notice, an
	upstream line of a player not of the fork's chat) */
	char code[SOCIAL_CHAT_CODE_SIZE];
	/* a private line's other account: its code and name */
	char peer[SOCIAL_CHAT_CODE_SIZE];
	wchar_t peer_name[SOCIAL_CHAT_NAME_SIZE];
	short channel;
	short color;
	byte sources;
	boolean mine;
	boolean notice;
	/* its player was reported by many (shown with social.show_reported) */
	boolean reported;
	/* one of this machine's, shown as it went, that the server refused */
	boolean failed;
	/* the colour its player picked for their name (0x1000000 | 0xRRGGBB; 0:
	none) */
	long name_color;
	/* (system_milliseconds; 0: an old line, the history's, never shown as new) */
	unsigned long time;
};

/* an account a private line may go to */
struct social_chat_peer
{
	char code[SOCIAL_CHAT_CODE_SIZE];
	wchar_t name[SOCIAL_CHAT_NAME_SIZE];
	/* in this game's room (else one that wrote, or was written to) */
	boolean in_room;
};

/* ---------- globals */

static struct
{
	/* the lines, the newest at newest */
	struct social_chat_line lines[SOCIAL_CHAT_MAXIMUM_LINES];
	short line_count;
	short newest;
	/* the box open: the channel, the line typed, the lines scrolled past,
	and the port of the controller typing (NONE: the keyboard) */
	boolean open;
	short channel;
	short port;
	unsigned long opened_at;
	char typed[SOCIAL_CHAT_SAID_SIZE];
	short scroll;
	/* when it last closed (social_chat_busy) */
	unsigned long closed_at;
	/* where it is docked (the menus' 640, which the menus' drawing centres
	in the screen: shift, the screen's coordinates' from them), whether it
	is chosen there, and when it was last drawn there (docked while that was
	lately) */
	rectangle2d dock;
	short dock_shift;
	boolean dock_chosen;
	unsigned long docked_at;
	/* docked and closed: a line chosen (the right stick), and the newest
	lines left out to show it (both lines back from the newest) */
	boolean dock_picked;
	short dock_pick;
	short dock_scroll;
	/* opened for a player's menu alone (the docked line's, by a
	controller): its keyboard not shown, and closed with the menu */
	boolean menu_only;
	/* the keyboard's chat keys held last frame; a toggle asked for, by a
	controller's port */
	unsigned long keys_held;
	short toggle_port;
	/* the private line's account (in peers) */
	struct social_chat_peer peers[SOCIAL_CHAT_MAXIMUM_PEERS];
	short peer_count;
	short peer;
	/* the names of the players muted or blocked, whose upstream lines are hidden */
	wchar_t unheard[SOCIAL_CHAT_MAXIMUM_UNHEARD][SOCIAL_CHAT_NAME_SIZE];
	short unheard_count;
	/* the upstream chat's players muted in this match (its room's) alone, by
	their names */
	wchar_t muted_here[SOCIAL_CHAT_MAXIMUM_MUTED_HERE][SOCIAL_CHAT_NAME_SIZE];
	short muted_here_count;
	char muted_here_room[SOCIAL_CHAT_ROOM_SIZE];
	/* social.show_reported as the last poll had it (the history asked
	again when it changes) */
	boolean reported_shown;
	/* the fork's lines: the request asked, the last line had, and when to
	ask next; the account's code the server last said */
	int poll_ticket;
	int send_ticket;
	/* the line being sent, as shown (marked NOT SENT if refused): checked
	still to be it by its text */
	struct social_chat_line *sending_line;
	char sending_text[SOCIAL_CHAT_TEXT_SIZE];
	long cursor;
	boolean polled;
	unsigned long poll_at;
	char room[SOCIAL_CHAT_ROOM_SIZE];
	char answer[COMMUNITY_NET_ANSWER_MAXIMUM];
} social_chat = { { 0 }, 0, 0, FALSE, 0, NONE, 0 };

/* a player's menu: its player, its options and the one chosen, its
profile shown (else its options), a second REPORT waited for, and where its
options were drawn (the screen's coordinates, a click's); and the names
drawn last that can be clicked */
static struct
{
	boolean open;
	char code[SOCIAL_CHAT_CODE_SIZE];
	wchar_t name[SOCIAL_CHAT_NAME_SIZE];
	short options[SOCIAL_CHAT_PLAYER_OPTIONS + 1];
	short option_count;
	short chosen;
	boolean profile;
	unsigned long confirm_time;
	short top;
	boolean drawn;
	rectangle2d option_bounds[SOCIAL_CHAT_PLAYER_OPTIONS + 1];
	rectangle2d panel;
	/* (opened from the lobby's row: its player's machine and the host's
	actions on it, SOCIAL_CHAT_LOBBY_*) */
	long lobby_machine;
	unsigned long lobby_actions;
	short hit_count;
	struct
	{
		rectangle2d bounds;
		char code[SOCIAL_CHAT_CODE_SIZE];
		wchar_t name[SOCIAL_CHAT_NAME_SIZE];
	} hits[SOCIAL_CHAT_MAXIMUM_NAME_HITS];
} social_chat_player;

/* the on-screen keyboard */
static struct
{
	boolean open;
	/* open with the system's keyboard in its place, not drawn
	(halo_system_keyboard: Android's, which types keys): the controller's
	buttons still close the box and send the line, a tap brings that
	keyboard up again */
	boolean system;
	/* (that keyboard asked for: docked, in the menus or the lobby, only
	once the line where it is typed is tapped, so that a tap on a player's
	name is not under it) */
	boolean system_up;
	short row;
	short column;
	boolean shift;
	boolean symbols;
	/* the controller's buttons last frame, and when a held direction moves
	the key again */
	unsigned long held;
	unsigned long opening;
	unsigned long repeat_at;
	/* where its keys were last drawn, in the screen's coordinates (a
	click's or a tap's place) */
	boolean drawn;
	rectangle2d bounds;
} social_chat_keyboard;

/* where the open box was drawn last, its channels and who a private line
goes to (none drawn: empty), in the screen's coordinates (a click's or a
tap's): one picks the channel, the next player, elsewhere outside the box
(its keyboard, a player's menu) it closes */
static struct
{
	boolean drawn;
	rectangle2d box;
	rectangle2d channels[NUMBER_OF_SOCIAL_CHAT_CHANNELS];
	rectangle2d peer;
	/* (where the line is typed) */
	rectangle2d input;
} social_chat_hits;

static char const *const social_chat_letters[SOCIAL_CHAT_KEYBOARD_ROWS - 1] =
	{ "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?" };
static char const *const social_chat_symbols[SOCIAL_CHAT_KEYBOARD_ROWS - 1] =
	{ "1234567890", "!@#$%^&*()", "-_=+[]{};:", "\"'<>/\\?~,." };
static char const *const social_chat_channel_names[NUMBER_OF_SOCIAL_CHAT_CHANNELS] =
	{ "MATCH", "TEAM", "PRIVATE", "GLOBAL" };
static wchar_t const *const social_chat_channel_tags[NUMBER_OF_SOCIAL_CHAT_CHANNELS] =
	{ L"", L"[TEAM] ", L"[PM] ", L"[GLOBAL] " };

static void social_chat_close(void);
static boolean social_chat_network_game(void);
static void social_chat_player_close(void);
static void social_chat_player_back(void);
static void social_chat_player_pick(void);
static void social_chat_player_step(short step);
static void social_chat_utf8_from_wide(wchar_t const *wide, char *text, long size);
static void social_chat_player_open(char const *code, wchar_t const *name, short top);

/* ---------- settings */

/* a setting read again only when Settings changes it */
static boolean social_chat_setting(
	char const *name,
	unsigned long *read_at,
	boolean *value)
{
	if (*read_at != config_changes())
	{
		*read_at = config_changes();
		*value = config_boolean(name) != 0;
	}
	return *value;
}

boolean social_chat_replaces_upstream(
	void)
{
	static unsigned long read_at = (unsigned long)-1;
	static boolean on;

	return social_chat_setting("social.chat", &read_at, &on);
}

static boolean social_chat_shown(
	void)
{
	static unsigned long read_at = (unsigned long)-1;
	static boolean shown;

	return social_chat_replaces_upstream() && social_chat_setting("display.text_chat", &read_at, &shown);
}

boolean social_chat_dock_shown(
	void)
{
	return social_enabled() && social_chat_shown();
}

static boolean social_chat_global_heard(
	void)
{
	static unsigned long read_at = (unsigned long)-1;
	static boolean heard;

	return social_chat_setting("social.global_chat", &read_at, &heard);
}

static boolean social_chat_global_everywhere(
	void)
{
	static unsigned long read_at = (unsigned long)-1;
	static boolean everywhere;

	return social_chat_setting("social.global_everywhere", &read_at, &everywhere);
}

static boolean social_chat_private_heard(
	void)
{
	static unsigned long read_at = (unsigned long)-1;
	static boolean heard;

	return social_chat_setting("social.private_chat", &read_at, &heard);
}

static boolean social_chat_reported_shown(
	void)
{
	static unsigned long read_at = (unsigned long)-1;
	static boolean shown;

	return social_chat_setting("social.show_reported", &read_at, &shown);
}

/* the fork's chat can be reached (the account's requests) */
static boolean social_chat_online(
	void)
{
	return social_chat_replaces_upstream() && social_enabled();
}

/* ---------- text */

/* printable ASCII but "|" (the game's text drawing takes it for a code's
start): "?" for any other, at most size - 1 characters */
static void social_chat_clean(
	char *text,
	char const *source,
	long size)
{
	long length = 0;
	unsigned char const *cursor = (unsigned char const *)source;

	while (*cursor && length < size - 1)
	{
		unsigned char character = *cursor++;

		/* (a UTF-8 sequence is one character the font has not) */
		if (character >= 0x80)
		{
			while ((*cursor & 0xC0) == 0x80)
				cursor++;
			character = '?';
		}
		else if (character < ' ' || character == 0x7F || character == '|')
		{
			character = character < ' ' ? ' ' : '?';
		}
		text[length++] = (char)character;
	}
	text[length] = 0;
}

static void social_chat_wide_from_ascii(
	char const *text,
	wchar_t *wide,
	long count)
{
	long index;

	for (index = 0; index < count - 1 && text[index]; index++)
		wide[index] = (wchar_t)(unsigned char)text[index];
	wide[index] = 0;
}

/* a name as the game's text (the basic plane of UTF-8; '?' past it) */
static void social_chat_wide_from_utf8(
	char const *text,
	wchar_t *wide,
	long count)
{
	long length = 0;
	unsigned char const *cursor = (unsigned char const *)text;

	while (*cursor && length < count - 1)
	{
		unsigned long character = *cursor++;

		if (character >= 0xE0 && (cursor[0] & 0xC0) == 0x80 && (cursor[1] & 0xC0) == 0x80 && character < 0xF0)
		{
			character = ((character & 0x0F) << 12) | ((cursor[0] & 0x3F) << 6) | (cursor[1] & 0x3F);
			cursor += 2;
		}
		else if (character >= 0xC0 && character < 0xE0 && (cursor[0] & 0xC0) == 0x80)
		{
			character = ((character & 0x1F) << 6) | (cursor[0] & 0x3F);
			cursor++;
		}
		else if (character >= 0x80)
		{
			while ((*cursor & 0xC0) == 0x80)
				cursor++;
			character = '?';
		}
		wide[length++] = (wchar_t)character;
	}
	wide[length] = 0;
}

static wchar_t social_chat_lower(
	wchar_t character)
{
	return character >= 'A' && character <= 'Z' ? (wchar_t)(character - 'A' + 'a') : character;
}

/* two names the same, in either case (the upstream chat's players' names
are its game's, the fork's its accounts') */
static boolean social_chat_names_equal(
	wchar_t const *a,
	wchar_t const *b)
{
	while (*a && *b && social_chat_lower(*a) == social_chat_lower(*b))
	{
		a++;
		b++;
	}
	return !*a && !*b;
}

/* how long a line to a channel may be (its end included): the fork's alone
(GLOBAL, PRIVATE) longer than one that goes through the upstream chat */
static long social_chat_said_limit(
	short channel)
{
	return channel == _social_chat_global || channel == _social_chat_private ?
		SOCIAL_CHAT_SAID_SIZE : SOCIAL_CHAT_UPSTREAM_SAID_SIZE;
}

static void social_chat_append(
	char *text,
	char const *more)
{
	long length = (long)strlen(text);
	long limit = social_chat_said_limit(social_chat.channel);

	while (*more && length < limit - 1)
	{
		if (*more >= ' ' && *more <= '~' && *more != '|')
			text[length++] = *more;
		more++;
	}
	text[length] = 0;
}

/* ---------- the lines */

static struct social_chat_line *social_chat_line_at(
	short back)
{
	short index = (short)((social_chat.newest - back + SOCIAL_CHAT_MAXIMUM_LINES) % SOCIAL_CHAT_MAXIMUM_LINES);

	return back >= 0 && back < social_chat.line_count ? &social_chat.lines[index] : NULL;
}

static boolean social_chat_name_is_local(
	wchar_t const *name);

/* the same line heard the other way lately (its player's name and text, a
channel that is the same both ways), NULL for none */
static struct social_chat_line *social_chat_same_line(
	wchar_t const *name,
	short channel,
	char const *text,
	byte source_bit,
	boolean local_name)
{
	unsigned long now = system_milliseconds();
	short back;

	for (back = 0; back < social_chat.line_count; back++)
	{
		struct social_chat_line *line = social_chat_line_at(back);

		if (!line->time || now - line->time > SOCIAL_CHAT_SAME_MILLISECONDS)
			break;
		if (!line->notice && !TEST_FLAG(line->sources, source_bit) && line->channel == channel &&
			!strncmp(line->text, text, SOCIAL_CHAT_SAID_SIZE - 1) &&
			(social_chat_names_equal(line->name, name) ||
				(local_name && (line->mine || (TEST_FLAG(line->sources, _social_chat_upstream_bit) &&
					social_chat_name_is_local(line->name))))))
		{
			return line;
		}
	}
	return NULL;
}

static struct social_chat_line *social_chat_line_new(
	unsigned long time)
{
	struct social_chat_line *line;

	social_chat.newest = (short)((social_chat.newest + 1) % SOCIAL_CHAT_MAXIMUM_LINES);
	if (social_chat.line_count < SOCIAL_CHAT_MAXIMUM_LINES)
		social_chat.line_count++;
	line = &social_chat.lines[social_chat.newest];
	csmemset(line, 0, sizeof(*line));
	line->time = time;
	/* (the box's place kept while it is scrolled back; the docked box's
	line chosen kept, gone when its line is) */
	if (social_chat.scroll)
		social_chat.scroll++;
	if (social_chat.dock_scroll)
		social_chat.dock_scroll++;
	if (social_chat.dock_picked && ++social_chat.dock_pick >= SOCIAL_CHAT_MAXIMUM_LINES)
	{
		social_chat.dock_picked = FALSE;
		social_chat.dock_scroll = 0;
	}
	return line;
}

static void social_chat_notice(
	char const *text)
{
	struct social_chat_line *line = social_chat_line_new(system_milliseconds());

	social_chat_clean(line->text, text, SOCIAL_CHAT_TEXT_SIZE);
	line->notice = TRUE;
	line->color = _social_chat_color_notice;
	line->channel = _social_chat_match;
	error(_error_log, "social chat notice: %s", line->text);
}

void social_chat_notice_text(
	char const *text)
{
	if (text && text[0])
		social_chat_notice(text);
}

void social_chat_upstream_line(
	wchar_t const *name,
	short color,
	boolean team,
	char const *text)
{
	short channel = team ? _social_chat_team : _social_chat_match;
	struct social_chat_line *line;
	short index;

	/* (a player muted or blocked, by its name) */
	for (index = 0; index < social_chat.unheard_count; index++)
	{
		if (social_chat_names_equal(social_chat.unheard[index], name))
			return;
	}
	line = social_chat_same_line(name, channel, text, _social_chat_upstream_bit,
		social_chat_name_is_local(name));
	if (line)
	{
		SET_FLAG(line->sources, _social_chat_upstream_bit, TRUE);
		return;
	}
	line = social_chat_line_new(system_milliseconds());
	ustrncpy(line->name, name, SOCIAL_CHAT_NAME_SIZE - 1);
	social_chat_clean(line->text, text, SOCIAL_CHAT_TEXT_SIZE);
	line->channel = channel;
	line->color = color == 0 ? _social_chat_color_red : color == 1 ? _social_chat_color_blue : _social_chat_color_none;
	/* (its account, when a player of this game's room has its name) */
	for (index = 0; index < social_chat.peer_count; index++)
	{
		if (social_chat.peers[index].in_room && social_chat_names_equal(social_chat.peers[index].name, name))
		{
			csstrncpy(line->code, social_chat.peers[index].code, SOCIAL_CHAT_CODE_SIZE - 1);
			break;
		}
	}
	SET_FLAG(line->sources, _social_chat_upstream_bit, TRUE);
}

void social_chat_upstream_notice(
	char const *text)
{
	struct social_chat_line *line = social_chat_line_new(system_milliseconds());

	social_chat_clean(line->text, text, SOCIAL_CHAT_TEXT_SIZE);
	line->notice = TRUE;
	line->color = _social_chat_color_notice;
	SET_FLAG(line->sources, _social_chat_upstream_bit, TRUE);
}

void social_chat_unheard_set(
	wchar_t const (*names)[32],
	short count)
{
	short index;

	social_chat.unheard_count = 0;
	for (index = 0; index < count && index < SOCIAL_CHAT_MAXIMUM_UNHEARD; index++)
	{
		if (!names[index][0])
			continue;
		ustrncpy(social_chat.unheard[social_chat.unheard_count], names[index], SOCIAL_CHAT_NAME_SIZE - 1);
		social_chat.unheard[social_chat.unheard_count][SOCIAL_CHAT_NAME_SIZE - 1] = 0;
		social_chat.unheard_count++;
	}
}

/* ---------- the players muted in this match */

static short social_chat_muted_here_find(
	wchar_t const *name)
{
	short index;

	for (index = 0; index < social_chat.muted_here_count; index++)
	{
		if (social_chat_names_equal(social_chat.muted_here[index], name))
			return index;
	}
	return NONE;
}

static void social_chat_mute_here(
	wchar_t const *name,
	boolean muted)
{
	short index = social_chat_muted_here_find(name);

	if (muted && index == NONE && social_chat.muted_here_count < SOCIAL_CHAT_MAXIMUM_MUTED_HERE)
	{
		ustrncpy(social_chat.muted_here[social_chat.muted_here_count], name, SOCIAL_CHAT_NAME_SIZE - 1);
		social_chat.muted_here[social_chat.muted_here_count][SOCIAL_CHAT_NAME_SIZE - 1] = 0;
		social_chat.muted_here_count++;
	}
	else if (!muted && index != NONE)
	{
		csmemmove(&social_chat.muted_here[index], &social_chat.muted_here[index + 1],
			(social_chat.muted_here_count - index - 1) * sizeof(social_chat.muted_here[0]));
		social_chat.muted_here_count--;
	}
}

/* a name one of this machine's players has (its own upstream lines) */
static boolean social_chat_name_is_local(
	wchar_t const *name)
{
	short local_player_index;

	for (local_player_index = local_player_get_next(NONE); local_player_index != NONE;
		local_player_index = local_player_get_next(local_player_index))
	{
		long player_index = local_player_get_player_index(local_player_index);
		struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
		wchar_t own[SOCIAL_CHAT_NAME_SIZE];

		if (!player)
			continue;
		ustrncpy(own, player->name, MIN(NUMBEROF(player->name), SOCIAL_CHAT_NAME_SIZE - 1));
		own[MIN(NUMBEROF(player->name), SOCIAL_CHAT_NAME_SIZE - 1)] = 0;
		if (social_chat_names_equal(own, name))
			return TRUE;
	}
	return FALSE;
}

/* a line shown now: not of an upstream player muted in this match; a global
one with social.global_everywhere or outside a network game (the menus, the
campaign: no match's lines to keep it from), else only with the box open at
GLOBAL, which then shows global lines alone (and notices) */
static boolean social_chat_line_shown(
	struct social_chat_line const *line)
{
	boolean global_open = social_chat.open && social_chat.channel == _social_chat_global;

	if (!line->notice && !TEST_FLAG(line->sources, _social_chat_fork_bit) &&
		social_chat_muted_here_find(line->name) != NONE)
	{
		return FALSE;
	}
	if (line->notice || social_chat_global_everywhere() || !social_chat_network_game())
		return TRUE;
	return line->channel == _social_chat_global ? global_open : !global_open;
}

/* ---------- the private lines' accounts */

static short social_chat_peer_find(
	char const *code)
{
	short index;

	for (index = 0; index < social_chat.peer_count; index++)
	{
		if (!strcmp(social_chat.peers[index].code, code))
			return index;
	}
	return NONE;
}

/* an account noted (the room's, or one that wrote or was written to) */
static void social_chat_peer_note(
	char const *code,
	wchar_t const *name,
	boolean in_room)
{
	short index;

	if (!code[0] || !strcmp(code, social_account_code()))
		return;
	index = social_chat_peer_find(code);
	if (index == NONE)
	{
		if (social_chat.peer_count >= SOCIAL_CHAT_MAXIMUM_PEERS)
		{
			/* (the oldest that is not the one picked gives way) */
			short oldest = (short)(social_chat.peer == 0 ? 1 : 0);

			csmemmove(&social_chat.peers[oldest], &social_chat.peers[oldest + 1],
				(SOCIAL_CHAT_MAXIMUM_PEERS - oldest - 1) * sizeof(social_chat.peers[0]));
			social_chat.peer_count--;
			if (social_chat.peer > oldest)
				social_chat.peer--;
		}
		index = social_chat.peer_count++;
		csmemset(&social_chat.peers[index], 0, sizeof(social_chat.peers[index]));
		csstrncpy(social_chat.peers[index].code, code, SOCIAL_CHAT_CODE_SIZE - 1);
	}
	ustrncpy(social_chat.peers[index].name, name, SOCIAL_CHAT_NAME_SIZE - 1);
	social_chat.peers[index].in_room = social_chat.peers[index].in_room || in_room;
}

static struct social_chat_peer const *social_chat_peer_picked(
	void)
{
	return social_chat.peer >= 0 && social_chat.peer < social_chat.peer_count ? &social_chat.peers[social_chat.peer] :
		NULL;
}

static void social_chat_peer_next(
	short step)
{
	if (social_chat.peer_count > 0)
		social_chat.peer = (short)((social_chat.peer + step + social_chat.peer_count) % social_chat.peer_count);
}

/* ---------- the game it is in */

static boolean social_chat_network_game(
	void)
{
	short connection = game_connection();

	return connection == _game_connection_network_client || connection == _game_connection_network_server;
}

/* the network game's room: a key every machine in it makes alike ("" for
none), from the game's name and its seed, which the host draws as it starts
hosting and gives each machine as it joins: the same in the lobby and the
games after it (not the map's, which the lobby changes), so the lobby's
MATCH lines are the game's */
static void social_chat_room(
	char *room,
	long size)
{
	struct network_game *game = network_game_get_game();
	unsigned long hash = 2166136261UL;
	short index;

	room[0] = 0;
	if (!game || !social_chat_network_game())
		return;
	for (index = 0; index < (short)NUMBEROF(game->name) && game->name[index]; index++)
		hash = ((hash ^ (unsigned long)(unsigned short)game->name[index]) * 16777619UL) & 0xFFFFFFFFUL;
	for (index = 0; index < 4; index++)
		hash = ((hash ^ ((game->random_seed >> (index * 8)) & 0xFF)) * 16777619UL) & 0xFFFFFFFFUL;
	snprintf(room, (size_t)size, "g:%08lx%08lx", hash, game->random_seed & 0xFFFFFFFFUL);
}

/* player 1's team in a team game (NONE: none, or no game) */
static short social_chat_team(
	void)
{
	short local_player_index = local_player_get_next(NONE);
	long player_index = local_player_index != NONE ? local_player_get_player_index(local_player_index) : NONE;
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

	if (!player || !game_in_progress() || !game_engine_running() || !game_engine_has_teams())
		return NONE;
	return player->team_index;
}

/* ---------- the fork's lines */

/* text into a query's value (letters, digits and -._~ as they are) */
static void social_chat_query_value(
	char *query,
	long size,
	char const *text)
{
	long length = (long)strlen(query);

	for (; *text && length < size - 4; text++)
	{
		unsigned char character = (unsigned char)*text;

		if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '-' || character == '.' || character == '_' ||
			character == '~')
		{
			query[length++] = (char)character;
		}
		else
		{
			snprintf(query + length, 4, "%%%02X", character);
			length += 3;
		}
	}
	query[length] = 0;
}

static void social_chat_poll_start(
	unsigned long now)
{
	char path[512];
	char name[64];
	short team = social_chat_team();

	social_chat_room(social_chat.room, sizeof(social_chat.room));
	social_player_name(name, sizeof(name));
	/* (the reported players' lines shown or hidden from the next ones on:
	there is no history to ask again) */
	social_chat.reported_shown = social_chat_reported_shown();
	snprintf(path, sizeof(path), "/chat/messages?after=%ld&global=%d&reported=%d",
		social_chat.polled ? social_chat.cursor : 0L, social_chat_global_heard() ? 1 : 0,
		social_chat.reported_shown ? 1 : 0);
	if (social_chat.room[0])
	{
		snprintf(path + strlen(path), sizeof(path) - strlen(path), "&room=%s", social_chat.room);
		if (team != NONE)
			snprintf(path + strlen(path), sizeof(path) - strlen(path), "&team=%d", (int)team);
	}
	if (name[0])
	{
		snprintf(path + strlen(path), sizeof(path) - strlen(path), "&name=");
		social_chat_query_value(path, sizeof(path), name);
	}
	social_chat.poll_ticket = social_send(path, NULL);
	if (!social_chat.poll_ticket)
		social_chat.poll_at = now + SOCIAL_CHAT_POLL_RETRY_MILLISECONDS;
}

static void social_chat_string(
	struct server_json_document const *document,
	int token,
	char const *key,
	char *text,
	int size)
{
	int member = server_json_member(document, token, key);

	text[0] = 0;
	if (member < 0 || !server_json_get_string(document, member, text, size))
		text[0] = 0;
}

static long social_chat_number(
	struct server_json_document const *document,
	int token,
	char const *key,
	long otherwise)
{
	int member = server_json_member(document, token, key);
	double value;

	return member >= 0 && server_json_get_number(document, member, &value) ? (long)value : otherwise;
}

/* a line of the fork's as the server sent it */
static void social_chat_take(
	struct server_json_document const *document,
	int token,
	boolean old)
{
	char channel_name[16];
	char text[SOCIAL_CHAT_TEXT_SIZE];
	char name[SOCIAL_CHAT_NAME_SIZE * 3];
	char from[SOCIAL_CHAT_CODE_SIZE];
	char to[SOCIAL_CHAT_CODE_SIZE];
	char to_name[SOCIAL_CHAT_NAME_SIZE * 3];
	char name_color_text[16];
	long name_color = 0;
	wchar_t wide_name[SOCIAL_CHAT_NAME_SIZE];
	short channel;
	boolean mine;
	int reported = 0;
	struct social_chat_line *line;
	int reported_token = server_json_member(document, token, "reported");

	if (reported_token < 0 || !server_json_get_boolean(document, reported_token, &reported))
		reported = 0;
	social_chat_string(document, token, "channel", channel_name, sizeof(channel_name));
	social_chat_string(document, token, "text", text, sizeof(text));
	social_chat_string(document, token, "name", name, sizeof(name));
	social_chat_string(document, token, "from", from, sizeof(from));
	social_chat_string(document, token, "to", to, sizeof(to));
	social_chat_string(document, token, "toName", to_name, sizeof(to_name));
	social_chat_string(document, token, "nameColor", name_color_text, sizeof(name_color_text));
	if (!strcmp(channel_name, "match"))
		channel = _social_chat_match;
	else if (!strcmp(channel_name, "team"))
		channel = _social_chat_team;
	else if (!strcmp(channel_name, "private"))
		channel = _social_chat_private;
	else if (!strcmp(channel_name, "global"))
		channel = _social_chat_global;
	else
		return;
	if (!text[0])
		return;
	/* (no history: a line said before this game first asked is not taken) */
	if (old)
		return;
	mine = from[0] && !strcmp(from, social_account_code());
	if (strlen(name_color_text) == 6)
		name_color = 0x1000000 | strtol(name_color_text, NULL, 16);
	social_chat_wide_from_utf8(name, wide_name, NUMBEROF(wide_name));
	if (channel == _social_chat_private)
	{
		wchar_t peer_name[SOCIAL_CHAT_NAME_SIZE];

		social_chat_wide_from_utf8(mine ? to_name : name, peer_name, NUMBEROF(peer_name));
		social_chat_peer_note(mine ? to : from, peer_name, FALSE);
	}
	/* (a line of this account's own said here, or one heard the upstream
	way first: marked heard this way too) */
	{
		char cleaned[SOCIAL_CHAT_TEXT_SIZE];

		social_chat_clean(cleaned, text, SOCIAL_CHAT_TEXT_SIZE);
		if (!old && (line = social_chat_same_line(wide_name, channel, cleaned, _social_chat_fork_bit, mine)) != NULL)
		{
			SET_FLAG(line->sources, _social_chat_fork_bit, TRUE);
			if (!line->name_color)
				line->name_color = name_color;
			return;
		}
	}
	/* (one this machine said, which it showed as it sent it) */
	if (mine && !old)
		return;
	line = social_chat_line_new(old ? 0 : system_milliseconds());
	ustrncpy(line->name, wide_name, SOCIAL_CHAT_NAME_SIZE - 1);
	social_chat_clean(line->text, text, SOCIAL_CHAT_TEXT_SIZE);
	line->channel = channel;
	line->mine = mine;
	line->name_color = name_color;
	line->reported = reported != 0;
	if (!mine)
		csstrncpy(line->code, from, SOCIAL_CHAT_CODE_SIZE - 1);
	line->color = mine ? _social_chat_color_mine : _social_chat_color_none;
	if (channel == _social_chat_private)
	{
		csstrncpy(line->peer, mine ? to : from, SOCIAL_CHAT_CODE_SIZE - 1);
		social_chat_wide_from_utf8(mine ? to_name : name, line->peer_name, NUMBEROF(line->peer_name));
	}
	if (channel == _social_chat_team && social_chat_network_game() && game_engine_has_teams())
	{
		short team = social_chat_team();

		line->color = team == 0 ? _social_chat_color_red : team == 1 ? _social_chat_color_blue : line->color;
	}
	SET_FLAG(line->sources, _social_chat_fork_bit, TRUE);
}

static void social_chat_poll_answer(
	unsigned long now)
{
	int status = 0;
	int result = community_net_poll(social_chat.poll_ticket, &status, social_chat.answer, sizeof(social_chat.answer));
	struct server_json_document document;
	int data;
	int messages;
	int members;
	int index;
	boolean old;

	if (result == _community_net_waiting)
		return;
	social_chat.poll_ticket = 0;
	social_chat.poll_at = now + (social_chat_network_game() || social_chat.open ? SOCIAL_CHAT_POLL_FAST_MILLISECONDS :
		SOCIAL_CHAT_POLL_MILLISECONDS);
	if (result != _community_net_answered || status != 200)
	{
		social_chat.poll_at = now + SOCIAL_CHAT_POLL_RETRY_MILLISECONDS;
		return;
	}
	if (!server_json_parse(&document, social_chat.answer, (int)strlen(social_chat.answer), 16))
		return;
	data = server_json_member(&document, 0, "data");
	if (data >= 0)
	{
		old = !social_chat.polled;
		social_chat.cursor = social_chat_number(&document, data, "cursor", social_chat.cursor);
		social_chat.polled = TRUE;
		/* (the friendships' key: the friends asked for again when it changed) */
		{
			char key[SOCIAL_CHAT_CODE_SIZE * 2];

			social_chat_string(&document, data, "friends", key, sizeof(key));
			social_menu_friends_key(key);
		}
		/* (the room's players: those a private line may go to) */
		members = server_json_member(&document, data, "members");
		for (index = 0; members >= 0; index++)
		{
			int member = server_json_element(&document, members, index);
			char code[SOCIAL_CHAT_CODE_SIZE];
			char name[SOCIAL_CHAT_NAME_SIZE * 3];
			wchar_t wide[SOCIAL_CHAT_NAME_SIZE];

			if (member < 0)
				break;
			social_chat_string(&document, member, "code", code, sizeof(code));
			social_chat_string(&document, member, "name", name, sizeof(name));
			social_chat_wide_from_utf8(name, wide, NUMBEROF(wide));
			social_chat_peer_note(code, wide, TRUE);
			/* (its rank and emblem, by the name the game shows it by: who
			has neither here is off the fork) */
			{
				char player[SOCIAL_CHAT_NAME_SIZE * 3];

				social_chat_string(&document, member, "player", player, sizeof(player));
				social_person_note(&document, member, code, player[0] ? player : name);
			}
		}
		messages = server_json_member(&document, data, "messages");
		for (index = 0; messages >= 0; index++)
		{
			int message = server_json_element(&document, messages, index);

			if (message < 0)
				break;
			social_chat_take(&document, message, old);
		}
	}
	server_json_document_free(&document);
}

static void social_chat_send_answer(
	void)
{
	int status = 0;
	int result = community_net_poll(social_chat.send_ticket, &status, social_chat.answer, sizeof(social_chat.answer));

	if (result == _community_net_waiting)
		return;
	social_chat.send_ticket = 0;
	/* (refused, or not reached: the line shown as it went marked so) */
	if ((result != _community_net_answered || status != 200) && social_chat.sending_line &&
		social_chat.sending_line->mine && !strcmp(social_chat.sending_line->text, social_chat.sending_text))
	{
		social_chat.sending_line->failed = TRUE;
	}
	social_chat.sending_line = NULL;
	if (result != _community_net_answered)
		social_chat_notice("The fork's chat could not be reached");
	else if (status == 429)
		social_chat_notice("Too many lines at once: wait a moment");
	else if (status == 404)
		social_chat_notice("That player's account was not found");
	else if (status == 403 && strstr(social_chat.answer, "private_off"))
		social_chat_notice("That player takes no private lines");
	else if (status == 403 && strstr(social_chat.answer, "own_friends_only"))
		social_chat_notice("You take private lines from friends alone, so you write to friends alone");
	else if (status == 403 && strstr(social_chat.answer, "friends_only"))
		social_chat_notice("That player takes private lines from friends alone");
	else if (status == 403 && strstr(social_chat.answer, "\"blocked\""))
		social_chat_notice("You blocked that player: unblock them in Social to write to them");
	else if (status == 403)
		social_chat_notice("That player cannot be written to");
	else if (status != 200)
		social_chat_notice("The fork's chat refused the line");
}

/* ---------- saying a line */

/* a line said to a channel: through the upstream chat where its host lets
it, and the fork's where it can; shown here as it goes */
static void social_chat_say(
	short channel,
	char const *typed)
{
	char text[SOCIAL_CHAT_SAID_SIZE];
	char name[64];
	wchar_t wide_name[SOCIAL_CHAT_NAME_SIZE];
	byte sources = 0;
	short team = social_chat_team();
	struct social_chat_peer const *peer = social_chat_peer_picked();
	boolean fork = social_chat_online() && social_account_code()[0];
	struct social_chat_line *line;
	short index;
	real red;
	real green;
	real blue;

	social_chat_clean(text, typed, social_chat_said_limit(channel));
	for (index = 0; text[index] == ' '; index++)
		;
	if (!text[index])
		return;
	/* (one line on its way at a time: the server's rate is a few a second) */
	if (fork && social_chat.send_ticket)
	{
		social_chat_notice("Still sending the last line: try again");
		return;
	}
	if ((channel == _social_chat_match || channel == _social_chat_team) && network_chat_available())
	{
		network_chat_say(channel == _social_chat_team ? _chat_channel_team : _chat_channel_everyone, text);
		SET_FLAG(sources, _social_chat_upstream_bit, TRUE);
	}
	if (fork && channel == _social_chat_private && !peer)
	{
		social_chat_notice("No one to write to: pick a player with Up and Down");
		return;
	}
	social_chat_room(social_chat.room, sizeof(social_chat.room));
	if (fork &&
		(channel == _social_chat_global || channel == _social_chat_private ||
			(social_chat.room[0] && (channel == _social_chat_match || team != NONE))))
	{
		struct server_json json;

		social_player_name(name, sizeof(name));
		server_json_begin(&json);
		server_json_object_begin(&json, NULL);
		server_json_string(&json, "channel", channel == _social_chat_match ? "match" :
			channel == _social_chat_team ? "team" : channel == _social_chat_private ? "private" : "global");
		server_json_string(&json, "text", text);
		if (name[0])
			server_json_string(&json, "name", name);
		if (channel == _social_chat_match || channel == _social_chat_team)
			server_json_string(&json, "room", social_chat.room);
		if (channel == _social_chat_team)
			server_json_integer(&json, "team", team);
		if (channel == _social_chat_private)
			server_json_string(&json, "to", peer->code);
		server_json_object_end(&json);
		social_chat.send_ticket = social_send("/chat/messages", server_json_text(&json));
		server_json_free(&json);
		if (social_chat.send_ticket)
			SET_FLAG(sources, _social_chat_fork_bit, TRUE);
	}
	if (!sources)
	{
		/* (the reason named: off in the settings, the account's code not had
		yet from the server, or none of the requests' slots free) */
		if (channel != _social_chat_global && channel != _social_chat_private)
			social_chat_notice("No one can hear that channel here");
		else if (!community_net_enabled())
			social_chat_notice("The fork's chat is offline: ONLINE is off (community.enabled)");
		else if (!social_chat_online())
			social_chat_notice("The fork's chat is off (social.enabled, social.chat)");
		else if (!social_account_code()[0])
			social_chat_notice("The fork's chat has not signed in yet: the server did not answer");
		else
			social_chat_notice("The fork's chat is busy: try again");
		return;
	}
	/* (sent the fork's way: shown now, and the upstream chat's echo from the
	host marks it; sent the upstream way alone, that echo is the line) */
	if (TEST_FLAG(sources, _social_chat_fork_bit))
	{
		social_account_name(name, sizeof(name));
		social_chat_wide_from_utf8(name, wide_name, NUMBEROF(wide_name));
		line = social_chat_same_line(wide_name, channel, text, _social_chat_fork_bit, TRUE);
		if (!line)
		{
			line = social_chat_line_new(system_milliseconds());
			ustrncpy(line->name, wide_name, SOCIAL_CHAT_NAME_SIZE - 1);
			csstrncpy(line->text, text, SOCIAL_CHAT_TEXT_SIZE - 1);
			line->channel = channel;
		}
		else
		{
			ustrncpy(line->name, wide_name, SOCIAL_CHAT_NAME_SIZE - 1);
			SET_FLAG(line->sources, _social_chat_fork_bit, TRUE);
		}
		if (social_account_name_color(&red, &green, &blue))
			line->name_color = 0x1000000 | ((long)(red * 255.0f + 0.5f) << 16) | ((long)(green * 255.0f + 0.5f) << 8) |
				(long)(blue * 255.0f + 0.5f);
		line->mine = TRUE;
		line->color = _social_chat_color_mine;
		SET_FLAG(line->sources, _social_chat_fork_bit, TRUE);
		if (channel == _social_chat_private && peer)
		{
			csstrncpy(line->peer, peer->code, SOCIAL_CHAT_CODE_SIZE - 1);
			ustrncpy(line->peer_name, peer->name, SOCIAL_CHAT_NAME_SIZE - 1);
		}
		social_chat.sending_line = line;
		csstrncpy(social_chat.sending_text, line->text, SOCIAL_CHAT_TEXT_SIZE - 1);
	}
}

/* ---------- opening, closing, typing */

boolean social_chat_typing(
	void)
{
	return social_chat.open;
}

boolean social_chat_keyboard_open(
	void)
{
	return social_chat.open && social_chat_keyboard.open;
}

boolean social_chat_system_open(
	void)
{
	return social_chat.open && social_chat_keyboard.open && social_chat_keyboard.system && !social_chat.menu_only;
}

boolean social_chat_system_keyboard(
	void)
{
	return social_chat.open && social_chat_keyboard.open && social_chat_keyboard.system &&
		social_chat_keyboard.system_up && !social_chat.menu_only;
}

short social_chat_controller(
	void)
{
	return social_chat.open && social_chat_keyboard.open ? social_chat.port : NONE;
}

/* a channel the box may be opened to here: MATCH and TEAM only in a network
game (TEAM in a team game), the others the fork's */
static short social_chat_channel_usable(
	short channel)
{
	if (channel == _social_chat_team && social_chat_team() == NONE)
		channel = _social_chat_match;
	if (channel == _social_chat_match && !social_chat_network_game())
		channel = _social_chat_global;
	if (channel == _social_chat_private && !social_chat_private_heard())
		channel = social_chat_network_game() ? _social_chat_match : _social_chat_global;
	return channel;
}

void social_chat_open(
	short channel,
	short port)
{
	struct key_stroke key;

	if (!social_chat_shown() || social_chat.open)
		return;
	social_chat.open = TRUE;
	social_chat.opened_at = system_milliseconds();
	social_chat.channel = social_chat_channel_usable(channel);
	social_chat.port = port;
	social_chat.typed[0] = 0;
	social_chat.scroll = 0;
	social_chat.dock_picked = FALSE;
	social_chat.dock_scroll = 0;
	social_chat.menu_only = FALSE;
	csmemset(&social_chat_keyboard, 0, sizeof(social_chat_keyboard));
	if (port != NONE)
	{
		social_chat_keyboard.open = TRUE;
		/* (the touchscreen's, on Android: its own keyboard, as a player
		profile's name has) */
		social_chat_keyboard.system = port == 0 && halo_system_keyboard();
		/* (not up by itself, so that the lines can be read and the channel
		changed: the keyboard button on the line typed brings it,
		social_chat_click) */
		social_chat_keyboard.system_up = FALSE;
		social_chat_keyboard.row = 1;
		/* (the buttons that opened it held over: none of them a key) */
		social_chat_keyboard.opening = halo_chat_pad_buttons_for_port(port);
	}
	/* (the keys pressed before, and the fork's lines asked for now) */
	while (input_get_key(&key))
		;
	social_chat.poll_at = system_milliseconds();
	if (social_chat.channel == _social_chat_private && !social_chat_peer_picked())
		social_chat_peer_next(0);
}

void social_chat_open_private(
	char const *code,
	wchar_t const *name)
{
	short index;

	if (!social_chat_shown() || !social_chat_private_heard())
		return;
	social_chat_peer_note(code, name, FALSE);
	index = social_chat_peer_find(code);
	if (index == NONE)
		return;
	social_chat_close();
	social_chat_open(_social_chat_private, halo_keyboard_typed_last() ? NONE : 0);
	social_chat.channel = _social_chat_private;
	social_chat.peer = index;
	/* (asked to write: the system's keyboard up at once) */
	if (social_chat_keyboard.system)
	{
		social_chat_keyboard.system_up = TRUE;
		halo_system_keyboard_again();
	}
}

short social_chat_recent_players(
	char (*codes)[16],
	wchar_t (*names)[32],
	short maximum)
{
	short count = 0;
	short index;

	/* (the newest first: those of this game's room, then those written to) */
	for (index = (short)(social_chat.peer_count - 1); index >= 0 && count < maximum; index--)
	{
		csstrncpy(codes[count], social_chat.peers[index].code, 15);
		codes[count][15] = 0;
		ustrncpy(names[count], social_chat.peers[index].name, 31);
		names[count][31] = 0;
		count++;
	}
	return count;
}

static void social_chat_close(
	void)
{
	if (social_chat.open)
		social_chat.closed_at = system_milliseconds() | 1;
	social_chat.open = FALSE;
	social_chat_keyboard.open = FALSE;
	social_chat_keyboard.system = FALSE;
	social_chat_keyboard.system_up = FALSE;
	social_chat_keyboard.drawn = FALSE;
	social_chat.scroll = 0;
	social_chat_player_close();
	social_chat_player.hit_count = 0;
}

enum
{
	/* the menus' keys held off after it closes (its B, its Start) */
	SOCIAL_CHAT_BUSY_MILLISECONDS = 300,
	/* a dock not drawn again this long is gone (the main menu left) */
	SOCIAL_CHAT_DOCK_MILLISECONDS = 1000,
};

boolean social_chat_busy(
	void)
{
	return social_chat.open || (social_chat.closed_at &&
		(long)(system_milliseconds() - social_chat.closed_at) < SOCIAL_CHAT_BUSY_MILLISECONDS);
}

static boolean social_chat_docked(
	void)
{
	/* (signed: docked_at, made odd, may be a millisecond ahead of now, the
	menus' drawing and the overlay's often in the same millisecond) */
	return social_chat.docked_at && (long)(system_milliseconds() - social_chat.docked_at) < SOCIAL_CHAT_DOCK_MILLISECONDS &&
		social_chat.dock.x1 > social_chat.dock.x0 && social_chat.dock.y1 > social_chat.dock.y0;
}

void social_chat_open_docked(
	short port)
{
	if (social_chat.open)
		return;
	social_chat_open(social_chat.channel, halo_keyboard_typed_last() ? NONE : port);
}

boolean social_chat_dock_picked(void)
{
	return social_chat_docked() && !social_chat.open && social_chat.dock_picked;
}

boolean social_chat_dock_pick_step(
	short step)
{
	short back;

	if (!social_chat_docked() || social_chat.open)
		return FALSE;
	/* (older: from the one chosen, else the newest shown; newer: to the
	one chosen's newer, else none) */
	back = social_chat.dock_picked ? (short)(social_chat.dock_pick + step) : step < 0 ? social_chat.dock_scroll : NONE;
	while (back >= 0 && back < social_chat.line_count && !social_chat_line_shown(social_chat_line_at(back)))
		back = (short)(back + (step < 0 ? 1 : -1));
	if (back >= social_chat.line_count)
		return FALSE;
	if (back < 0)
	{
		if (!social_chat.dock_picked)
			return FALSE;
		social_chat.dock_picked = FALSE;
		social_chat.dock_scroll = 0;
		return TRUE;
	}
	social_chat.dock_picked = TRUE;
	social_chat.dock_pick = back;
	if (social_chat.dock_scroll > back)
		social_chat.dock_scroll = back;
	return TRUE;
}

boolean social_chat_dock_pick_menu(
	short port)
{
	struct social_chat_line const *line;
	char code[SOCIAL_CHAT_CODE_SIZE];
	wchar_t name[SOCIAL_CHAT_NAME_SIZE];

	if (!social_chat_docked() || social_chat.open || !social_chat.dock_picked)
		return FALSE;
	line = social_chat_line_at(social_chat.dock_pick);
	/* (as a name clicked: an account's, or an upstream player's not of this
	machine; not your own) */
	if (!line || line->notice || line->mine || !line->name[0] ||
		(line->code[0] && !strcmp(line->code, social_account_code())) ||
		(!line->code[0] && (!TEST_FLAG(line->sources, _social_chat_upstream_bit) ||
			social_chat_name_is_local(line->name))))
	{
		return FALSE;
	}
	csstrncpy(code, line->code, SOCIAL_CHAT_CODE_SIZE - 1);
	code[SOCIAL_CHAT_CODE_SIZE - 1] = 0;
	ustrncpy(name, line->name, SOCIAL_CHAT_NAME_SIZE - 1);
	name[SOCIAL_CHAT_NAME_SIZE - 1] = 0;
	/* (the box opened to the controller's port, its keyboard hidden) */
	social_chat_open(social_chat.channel, port);
	if (!social_chat.open)
		return FALSE;
	social_chat.menu_only = TRUE;
	social_chat_player_open(code, name, social_chat.dock.y0);
	if (!social_chat_player.open)
	{
		social_chat_close();
		return FALSE;
	}
	return TRUE;
}

boolean social_chat_lobby_player_menu(
	wchar_t const *name,
	long machine_index,
	unsigned long actions,
	short port,
	short top)
{
	char code[SOCIAL_CHAT_CODE_SIZE];
	short back;

	code[0] = 0;
	/* (its account's code, from the newest line of a name the same that
	has one: the lobby knows its players by their names alone) */
	for (back = 0; !TEST_FLAG(actions, SOCIAL_CHAT_LOBBY_BOT_BIT) && back < social_chat.line_count; back++)
	{
		struct social_chat_line const *line = social_chat_line_at(back);

		if (line && !line->notice && line->code[0] && social_chat_names_equal(line->name, name))
		{
			csstrncpy(code, line->code, SOCIAL_CHAT_CODE_SIZE - 1);
			code[SOCIAL_CHAT_CODE_SIZE - 1] = 0;
			break;
		}
	}
	social_chat_open(social_chat.channel, port);
	if (!social_chat.open)
		return FALSE;
	social_chat.menu_only = TRUE;
	social_chat_player.lobby_actions = actions;
	social_chat_player.lobby_machine = machine_index;
	social_chat_player_open(code, name, top);
	if (!social_chat_player.open)
	{
		social_chat_player.lobby_actions = 0;
		social_chat_close();
		return FALSE;
	}
	return TRUE;
}

short social_chat_dock_pick_open(
	void)
{
	struct social_chat_line const *line;
	char name[SOCIAL_CHAT_NAME_SIZE * 3];

	if (!social_chat_docked() || social_chat.open || !social_chat.dock_picked)
		return NONE;
	line = social_chat_line_at(social_chat.dock_pick);
	if (!line || line->notice)
		return NONE;
	if (line->mine || (line->code[0] && !strcmp(line->code, social_account_code())))
		return social_account_code()[0] ? _social_menu_open_profile : NONE;
	if (!line->code[0])
		return NONE;
	social_chat_utf8_from_wide(line->name, name, sizeof(name));
	social_menu_player_open_next(line->code, name);
	return _social_menu_open_player;
}

void social_chat_toggle_request(
	short port)
{
	social_chat.toggle_port = (short)(port + 1);
}

static void social_chat_channel_step(
	short step)
{
	short channel = social_chat.channel;
	short tries;

	for (tries = 0; tries < NUMBER_OF_SOCIAL_CHAT_CHANNELS; tries++)
	{
		channel = (short)((channel + step + NUMBER_OF_SOCIAL_CHAT_CHANNELS) % NUMBER_OF_SOCIAL_CHAT_CHANNELS);
		if (social_chat_channel_usable(channel) == channel)
			break;
	}
	social_chat.channel = channel;
	if (channel == _social_chat_private && !social_chat_peer_picked())
		social_chat_peer_next(0);
}

/* a channel's name clicked or tapped: that channel, where it can be used */
static void social_chat_channel_pick(
	short channel)
{
	if (social_chat_channel_usable(channel) != channel)
		return;
	social_chat.channel = channel;
	if (channel == _social_chat_private && !social_chat_peer_picked())
		social_chat_peer_next(0);
}

static void social_chat_delete(
	void)
{
	long length = (long)strlen(social_chat.typed);

	if (length)
		social_chat.typed[length - 1] = 0;
}

static void social_chat_send_typed(
	void)
{
	social_chat_say(social_chat.channel, social_chat.typed);
	social_chat_close();
}

static char social_chat_keyboard_character(
	short row,
	short column)
{
	char const *const *rows = social_chat_keyboard.symbols ? social_chat_symbols : social_chat_letters;
	char character = rows[row][column];

	return social_chat_keyboard.shift && character >= 'a' && character <= 'z' ? (char)(character - 'a' + 'A') :
		character;
}

static void social_chat_keyboard_press(
	void)
{
	if (social_chat_keyboard.row < SOCIAL_CHAT_KEYBOARD_ROWS - 1)
	{
		char typed[2];

		typed[0] = social_chat_keyboard_character(social_chat_keyboard.row, social_chat_keyboard.column);
		typed[1] = 0;
		social_chat_append(social_chat.typed, typed);
		return;
	}
	switch (social_chat_keyboard.column / SOCIAL_CHAT_KEYBOARD_WIDE_COLUMNS)
	{
	case _social_chat_key_shift:
		social_chat_keyboard.shift = !social_chat_keyboard.shift;
		break;
	case _social_chat_key_symbols:
		social_chat_keyboard.symbols = !social_chat_keyboard.symbols;
		break;
	case _social_chat_key_space:
		social_chat_append(social_chat.typed, " ");
		break;
	case _social_chat_key_delete:
		social_chat_delete();
		break;
	default:
		social_chat_send_typed();
		break;
	}
}

/* the key at this place in the keyboard (from its top left, of its width
and height) pressed */
static void social_chat_keyboard_press_at(
	long x,
	long width,
	long y,
	long height)
{
	short row = (short)(y * SOCIAL_CHAT_KEYBOARD_ROWS / MAX(height, 1));
	short column = (short)(x * SOCIAL_CHAT_KEYBOARD_COLUMNS / MAX(width, 1));

	social_chat_keyboard.row = (short)PIN(row, 0, SOCIAL_CHAT_KEYBOARD_ROWS - 1);
	social_chat_keyboard.column = (short)PIN(column, 0, SOCIAL_CHAT_KEYBOARD_COLUMNS - 1);
	social_chat_keyboard_press();
}

static boolean social_chat_inside(
	rectangle2d const *bounds,
	short x,
	short y)
{
	return x >= bounds->x0 && x < bounds->x1 && y >= bounds->y0 && y < bounds->y1;
}

/* a click or a tap at this place in the screen, past a player's menu and
the names: a channel's name picks it, the private line's player the next
one, a key presses it, the line typed (its keyboard button) brings the
system's keyboard up, elsewhere in the box nothing, and outside the box it
closes */
static void social_chat_click(
	short x,
	short y)
{
	short channel;

	if (social_chat_keyboard.open && social_chat_keyboard.drawn &&
		social_chat_inside(&social_chat_keyboard.bounds, x, y))
	{
		rectangle2d const *keys = &social_chat_keyboard.bounds;

		social_chat_keyboard_press_at(x - keys->x0, keys->x1 - keys->x0, y - keys->y0, keys->y1 - keys->y0);
		return;
	}
	if (!social_chat_hits.drawn)
		return;
	for (channel = 0; channel < NUMBER_OF_SOCIAL_CHAT_CHANNELS; channel++)
	{
		if (social_chat_inside(&social_chat_hits.channels[channel], x, y))
		{
			social_chat_channel_pick(channel);
			return;
		}
	}
	if (social_chat.channel == _social_chat_private && social_chat_inside(&social_chat_hits.peer, x, y))
	{
		social_chat_peer_next(1);
		return;
	}
	if (social_chat_keyboard.system && !social_chat.menu_only &&
		social_chat_inside(&social_chat_hits.input, x, y))
	{
		social_chat_keyboard.system_up = TRUE;
		halo_system_keyboard_again();
		return;
	}
	if (social_chat_inside(&social_chat_hits.box, x, y))
		return;
	social_chat_close();
}

static void social_chat_keyboard_move(
	short rows_down,
	short columns_right)
{
	short row = (short)((social_chat_keyboard.row + rows_down + SOCIAL_CHAT_KEYBOARD_ROWS) % SOCIAL_CHAT_KEYBOARD_ROWS);
	short column = social_chat_keyboard.column;

	if (row == SOCIAL_CHAT_KEYBOARD_ROWS - 1)
	{
		column = (short)(column / SOCIAL_CHAT_KEYBOARD_WIDE_COLUMNS * SOCIAL_CHAT_KEYBOARD_WIDE_COLUMNS);
		column = (short)((column + columns_right * SOCIAL_CHAT_KEYBOARD_WIDE_COLUMNS + SOCIAL_CHAT_KEYBOARD_COLUMNS) %
			SOCIAL_CHAT_KEYBOARD_COLUMNS);
	}
	else
	{
		column = (short)((column + columns_right + SOCIAL_CHAT_KEYBOARD_COLUMNS) % SOCIAL_CHAT_KEYBOARD_COLUMNS);
	}
	social_chat_keyboard.row = row;
	social_chat_keyboard.column = column;
}

/* the on-screen keyboard's controller (as the upstream chat's: B closes,
Start sends, D-pad Up and Down the channel, the stick and D-pad Left and
Right the key, A presses it, X (the left button) deletes, Y (the top
button) types a space, R3 the next player to write to in PRIVATE) and
taps */
static void social_chat_keyboard_update(
	void)
{
	unsigned long held = halo_chat_pad_buttons();
	unsigned long pressed;
	unsigned long now = system_milliseconds();
	short rows_down = 0;
	short columns_right = 0;
	float x;
	float y;

	/* (the buttons that opened it, until let go) */
	social_chat_keyboard.opening &= held;
	held &= ~social_chat_keyboard.opening;
	pressed = held & ~social_chat_keyboard.held;
	social_chat_keyboard.held = held;
	/* (a player's menu open, by a tap: the D-pad and the stick choose, A
	picks, B goes back) */
	if (social_chat_player.open)
	{
		if (TEST_FLAG(pressed, HALO_CHAT_PAD_B))
			social_chat_player_back();
		else if (TEST_FLAG(pressed, HALO_CHAT_PAD_A))
			social_chat_player_pick();
		else if (TEST_FLAG(pressed, HALO_CHAT_PAD_UP) || TEST_FLAG(pressed, HALO_CHAT_PAD_STICK_UP))
			social_chat_player_step(-1);
		else if (TEST_FLAG(pressed, HALO_CHAT_PAD_DOWN) || TEST_FLAG(pressed, HALO_CHAT_PAD_STICK_DOWN))
			social_chat_player_step(1);
		return;
	}
	if (TEST_FLAG(pressed, HALO_CHAT_PAD_B))
	{
		social_chat_close();
		return;
	}
	if (TEST_FLAG(pressed, HALO_CHAT_PAD_START))
	{
		social_chat_send_typed();
		return;
	}
	/* (L and R the channel; the D-pad moves on the keys, as the stick does) */
	if (TEST_FLAG(pressed, HALO_CHAT_PAD_LEFT_SHOULDER))
		social_chat_channel_step(-1);
	if (TEST_FLAG(pressed, HALO_CHAT_PAD_RIGHT_SHOULDER))
		social_chat_channel_step(1);
	/* (a tap, outside the menus: as a click, social_chat_click) */
	if (social_chat.port == 0 && halo_chat_taps(&x, &y))
	{
		social_chat_click((short)(x * (render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0)),
			(short)(y * (render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0)));
		if (!social_chat.open)
			return;
	}
	/* (the system's keyboard types in its place: no key to pick, the D-pad
	who a private line goes to) */
	if (social_chat_keyboard.system)
	{
		if (TEST_FLAG(pressed, HALO_CHAT_PAD_UP))
			social_chat_peer_next(-1);
		if (TEST_FLAG(pressed, HALO_CHAT_PAD_DOWN))
			social_chat_peer_next(1);
		return;
	}
	if (TEST_FLAG(held, HALO_CHAT_PAD_STICK_UP) || TEST_FLAG(held, HALO_CHAT_PAD_UP))
		rows_down = -1;
	else if (TEST_FLAG(held, HALO_CHAT_PAD_STICK_DOWN) || TEST_FLAG(held, HALO_CHAT_PAD_DOWN))
		rows_down = 1;
	if (TEST_FLAG(held, HALO_CHAT_PAD_STICK_LEFT) || TEST_FLAG(held, HALO_CHAT_PAD_LEFT))
		columns_right = -1;
	else if (TEST_FLAG(held, HALO_CHAT_PAD_STICK_RIGHT) || TEST_FLAG(held, HALO_CHAT_PAD_RIGHT))
		columns_right = 1;
	if (!rows_down && !columns_right)
	{
		social_chat_keyboard.repeat_at = 0;
	}
	else if (!social_chat_keyboard.repeat_at || (long)(now - social_chat_keyboard.repeat_at) >= 0)
	{
		social_chat_keyboard_move(rows_down, columns_right);
		social_chat_keyboard.repeat_at = now + (social_chat_keyboard.repeat_at ?
			SOCIAL_CHAT_KEYBOARD_REPEAT_MILLISECONDS : SOCIAL_CHAT_KEYBOARD_REPEAT_FIRST_MILLISECONDS);
	}
	if (TEST_FLAG(pressed, HALO_CHAT_PAD_A))
		social_chat_keyboard_press();
	if (social_chat.open && TEST_FLAG(pressed, HALO_CHAT_PAD_X))
		social_chat_delete();
	/* (Y, the top button, a space; R3 the next player to write to in
	PRIVATE) */
	if (social_chat.open && TEST_FLAG(pressed, HALO_CHAT_PAD_Y))
		social_chat_append(social_chat.typed, " ");
	if (social_chat.open && social_chat.channel == _social_chat_private &&
		TEST_FLAG(pressed, HALO_CHAT_PAD_RIGHT_THUMB))
		social_chat_peer_next(1);
}

/* ---------- a player's menu (its name clicked) */

/* a name as UTF-8 (the server's names) */
static void social_chat_utf8_from_wide(
	wchar_t const *wide,
	char *text,
	long size)
{
	long length = 0;
	long index;

	for (index = 0; wide[index] && length < size - 1; index++)
	{
		unsigned long character = (unsigned short)wide[index];

		if (character < 0x80)
			text[length++] = (char)character;
		else if (character < 0x800 && length + 2 < size)
		{
			text[length++] = (char)(0xC0 | (character >> 6));
			text[length++] = (char)(0x80 | (character & 0x3F));
		}
		else if (length + 3 < size)
		{
			text[length++] = (char)(0xE0 | (character >> 12));
			text[length++] = (char)(0x80 | ((character >> 6) & 0x3F));
			text[length++] = (char)(0x80 | (character & 0x3F));
		}
		else
			break;
	}
	text[length] = 0;
}

static void social_chat_player_close(
	void)
{
	social_chat_player.open = FALSE;
	social_chat_player.drawn = FALSE;
	social_chat_player.lobby_actions = 0;
	social_chat_player.lobby_machine = NONE;
}

/* the options for its player, as the settings and the server say now */
static void social_chat_player_options(
	void)
{
	boolean online = social_chat_online() && social_account_code()[0];
	char const *code = social_chat_player.code;

	social_chat_player.option_count = 0;
	/* (a bot's row in the lobby: the host may take it out, nothing else) */
	if (TEST_FLAG(social_chat_player.lobby_actions, SOCIAL_CHAT_LOBBY_BOT_BIT))
	{
		if (TEST_FLAG(social_chat_player.lobby_actions, SOCIAL_CHAT_LOBBY_REMOVE_BOT_BIT))
			social_chat_player.options[social_chat_player.option_count++] = _social_chat_option_remove_bot;
		social_chat_player.chosen = 0;
		return;
	}
	/* (the host's, on a player's lobby row: kicked from the game) */
	if (TEST_FLAG(social_chat_player.lobby_actions, SOCIAL_CHAT_LOBBY_KICK_BIT))
		social_chat_player.options[social_chat_player.option_count++] = _social_chat_option_kick;
	/* (a player of the upstream chat's alone: muted in this match, or not) */
	if (!code[0])
	{
		social_chat_player.options[social_chat_player.option_count++] =
			social_chat_muted_here_find(social_chat_player.name) != NONE ? _social_chat_option_unmute_here :
			_social_chat_option_mute_here;
		social_chat_player.chosen = (short)PIN(social_chat_player.chosen, 0, social_chat_player.option_count - 1);
		return;
	}
	if (online && social_chat_private_heard())
		social_chat_player.options[social_chat_player.option_count++] = _social_chat_option_private;
	if (online && config_boolean("social.friends") && !social_menu_is_friend(code))
		social_chat_player.options[social_chat_player.option_count++] = _social_chat_option_friend;
	if (online)
	{
		social_chat_player.options[social_chat_player.option_count++] = _social_chat_option_profile;
		social_chat_player.options[social_chat_player.option_count++] = social_menu_is_muted(code) ?
			_social_chat_option_unmute : _social_chat_option_mute;
		social_chat_player.options[social_chat_player.option_count++] = _social_chat_option_report;
	}
	social_chat_player.chosen = (short)PIN(social_chat_player.chosen, 0, MAX(social_chat_player.option_count - 1, 0));
}

/* the menu of the player of this code ("" for an upstream player's: its
name alone, as the line shows it), beside the line at top; none with
nothing to do */
static void social_chat_player_open(
	char const *code,
	wchar_t const *name,
	short top)
{
	if ((!code[0] && !name[0]) || (code[0] && !strcmp(code, social_account_code())))
		return;
	csmemset(&social_chat_player.options, 0, sizeof(social_chat_player.options));
	csstrncpy(social_chat_player.code, code, SOCIAL_CHAT_CODE_SIZE - 1);
	social_chat_player.code[SOCIAL_CHAT_CODE_SIZE - 1] = 0;
	ustrncpy(social_chat_player.name, name, SOCIAL_CHAT_NAME_SIZE - 1);
	social_chat_player.name[SOCIAL_CHAT_NAME_SIZE - 1] = 0;
	social_chat_player.open = TRUE;
	social_chat_player.profile = FALSE;
	social_chat_player.chosen = 0;
	social_chat_player.confirm_time = 0;
	social_chat_player.top = top;
	social_chat_player.drawn = FALSE;
	social_chat_player_options();
	if (!social_chat_player.option_count)
		social_chat_player_close();
}

/* B, Escape, a right click: the profile back to the options, else closed */
static void social_chat_player_back(
	void)
{
	if (social_chat_player.profile)
		social_chat_player.profile = FALSE;
	else
		social_chat_player_close();
}

static void social_chat_player_pick(
	void)
{
	char name[SOCIAL_CHAT_NAME_SIZE * 3];
	char const *code = social_chat_player.code;
	short option;

	if (social_chat_player.profile)
	{
		social_chat_player_back();
		return;
	}
	if (social_chat_player.chosen < 0 || social_chat_player.chosen >= social_chat_player.option_count)
		return;
	option = social_chat_player.options[social_chat_player.chosen];
	social_chat_utf8_from_wide(social_chat_player.name, name, sizeof(name));
	switch (option)
	{
	case _social_chat_option_private:
		{
			short index;

			social_chat_peer_note(code, social_chat_player.name, FALSE);
			index = social_chat_peer_find(code);
			if (index != NONE)
			{
				social_chat.channel = _social_chat_private;
				social_chat.peer = index;
			}
			/* (opened for the menu alone: kept open to write, its keyboard shown) */
			social_chat.menu_only = FALSE;
			if (social_chat_keyboard.system && !social_chat_keyboard.system_up)
			{
				social_chat_keyboard.system_up = TRUE;
				halo_system_keyboard_again();
			}
			social_chat_player_close();
		}
		break;
	case _social_chat_option_friend:
		social_menu_player_act(code, name, _social_menu_act_friend);
		social_chat_player_close();
		break;
	case _social_chat_option_profile:
		social_chat_player.profile = TRUE;
		social_menu_profile_ask(code, name);
		break;
	case _social_chat_option_mute:
	case _social_chat_option_unmute:
		social_menu_player_act(code, name, option == _social_chat_option_mute ? _social_menu_act_mute :
			_social_menu_act_unmute);
		social_chat_player_close();
		break;
	case _social_chat_option_report:
		/* (a second press, soon after) */
		if (social_chat_player.confirm_time &&
			system_milliseconds() - social_chat_player.confirm_time <= SOCIAL_CHAT_PLAYER_CONFIRM_MILLISECONDS)
		{
			social_menu_player_act(code, name, _social_menu_act_report);
			social_chat_player_close();
		}
		else
		{
			social_chat_player.confirm_time = system_milliseconds();
		}
		break;
	case _social_chat_option_mute_here:
	case _social_chat_option_unmute_here:
		social_chat_mute_here(social_chat_player.name, option == _social_chat_option_mute_here);
		social_chat_player_close();
		break;
	case _social_chat_option_kick:
		/* (a second press, soon after, as REPORT's) */
		if (social_chat_player.confirm_time &&
			system_milliseconds() - social_chat_player.confirm_time <= SOCIAL_CHAT_PLAYER_CONFIRM_MILLISECONDS)
		{
			if (!network_game_server_kick_machine_of_player(social_chat_player.lobby_machine, FALSE))
				social_chat_notice("Could not kick that player");
			social_chat_player_close();
		}
		else
			social_chat_player.confirm_time = system_milliseconds();
		break;
	case _social_chat_option_remove_bot:
		/* (one bot fewer: the lobby's bots follow bots.count, bots.c) */
		{
			char count[16];

			snprintf(count, sizeof(count), "%ld", MAX(config_integer("bots.count") - 1, 0L));
			config_write("bots.count", count);
		}
		social_chat_player_close();
		break;
	default:
		social_chat_player_close();
		break;
	}
}

static void social_chat_player_step(
	short step)
{
	if (social_chat_player.profile || social_chat_player.option_count <= 0)
		return;
	social_chat_player.chosen = (short)((social_chat_player.chosen + step + social_chat_player.option_count) %
		social_chat_player.option_count);
}

void social_chat_pointer(
	struct halo_ui_pointer const *pointer)
{
	short x;
	short y;
	short index;

	if (!social_chat.open)
		return;
	if (pointer->wheel_steps && !social_chat_player.open)
		social_chat.scroll = (short)PIN(social_chat.scroll + pointer->wheel_steps, 0, MAX(social_chat.line_count - 1, 0));
	if (pointer->right_clicks && social_chat_player.open)
		social_chat_player_back();
	/* (the pointer over an option chooses it) */
	x = (short)(pointer->x + (halo_screen_width() - 640) / 2);
	y = pointer->y;
	if (social_chat_player.open && social_chat_player.drawn && pointer->moved && !social_chat_player.profile)
	{
		for (index = 0; index < social_chat_player.option_count; index++)
		{
			if (social_chat_inside(&social_chat_player.option_bounds[index], x, y))
				social_chat_player.chosen = index;
		}
	}
	if (!pointer->left_clicks)
		return;
	x = (short)(pointer->click_x + (halo_screen_width() - 640) / 2);
	y = pointer->click_y;
	if (social_chat_player.open && social_chat_player.drawn)
	{
		if (social_chat_player.profile && social_chat_inside(&social_chat_player.panel, x, y))
		{
			social_chat_player_back();
			return;
		}
		for (index = 0; index < social_chat_player.option_count; index++)
		{
			if (social_chat_inside(&social_chat_player.option_bounds[index], x, y))
			{
				social_chat_player.chosen = index;
				social_chat_player_pick();
				return;
			}
		}
		if (social_chat_inside(&social_chat_player.panel, x, y))
			return;
		social_chat_player_close();
	}
	for (index = 0; index < social_chat_player.hit_count; index++)
	{
		if (social_chat_inside(&social_chat_player.hits[index].bounds, x, y))
		{
			social_chat_player_open(social_chat_player.hits[index].code, social_chat_player.hits[index].name,
				social_chat_player.hits[index].bounds.y0);
			return;
		}
	}
	social_chat_click(x, y);
}

int social_chat_wants_pointer(
	void)
{
	return social_chat.open && !console_is_active();
}

/* the keyboard's keys, as the box takes them */
static void social_chat_keys_update(
	void)
{
	struct key_stroke key;

	while (social_chat.open && input_get_key(&key))
	{
		/* (a player's menu open: its keys alone) */
		if (social_chat_player.open)
		{
			if (key.key_code == _key_escape || key.key_code == _key_backspace)
				social_chat_player_back();
			else if (key.key_code == _key_up_arrow)
				social_chat_player_step(-1);
			else if (key.key_code == _key_down_arrow || key.key_code == _key_tab)
				social_chat_player_step(1);
			else if (key.key_code == _key_return || key.key_code == _keypad_enter)
				social_chat_player_pick();
			continue;
		}
		switch (key.key_code)
		{
		case _key_escape:
			social_chat_close();
			break;
		case _key_return:
		case _keypad_enter:
			social_chat_send_typed();
			break;
		case _key_tab:
			social_chat_channel_step(1);
			break;
		case _key_up_arrow:
			social_chat_peer_next(-1);
			break;
		case _key_down_arrow:
			social_chat_peer_next(1);
			break;
		case _key_backspace:
			social_chat_delete();
			break;
		case _key_page_up:
			social_chat.scroll = (short)(social_chat.scroll + SOCIAL_CHAT_PAGE_LINES);
			break;
		case _key_page_down:
			social_chat.scroll = (short)(social_chat.scroll - SOCIAL_CHAT_PAGE_LINES);
			break;
		case _key_backquote:
			break;
		default:
			if (TEST_FLAG(key.modifier_flags, SOCIAL_CHAT_KEY_MODIFIER_CONTROL_BIT))
			{
				char clipboard[SOCIAL_CHAT_SAID_SIZE];

				if (key.key_code == _key_v && platform_clipboard_get(clipboard, sizeof(clipboard)))
					social_chat_append(social_chat.typed, clipboard);
			}
			else if (key.ascii_code >= ' ' && key.ascii_code <= '~')
			{
				char typed[2];

				typed[0] = key.ascii_code;
				typed[1] = 0;
				social_chat_append(social_chat.typed, typed);
			}
			break;
		}
	}
	if (social_chat.open)
		social_chat.scroll = (short)(social_chat.scroll + halo_chat_wheel_steps());
	social_chat.scroll = (short)PIN(social_chat.scroll, 0, MAX(social_chat.line_count - 1, 0));
}

/* the keys that open it where the upstream chat's do not (the menus, a game
not a network one, a replay's playback, whose keys are its own:
xinput_sdl.c's replay_keyboard_gamepad): T to everyone (GLOBAL there), Y to
the team */
static void social_chat_open_keys(
	void)
{
	unsigned long held = 0;
	unsigned long pressed;

	if (!social_chat_network_game() || !game_in_progress() || network_replay_playing())
	{
		held = (game_in_progress() && !ui_widgets_active() && !network_replay_playing() ? halo_keyboard_actions(0) :
			halo_keyboard_actions_anywhere()) & (FLAG(HALO_KEYBOARD_CHAT) | FLAG(HALO_KEYBOARD_TEAM_CHAT));
	}
	pressed = held & ~social_chat.keys_held;
	social_chat.keys_held = held;
	if (pressed && !social_chat.open && !console_is_active())
		social_chat_open(TEST_FLAG(pressed, HALO_KEYBOARD_CHAT) ? _social_chat_global : _social_chat_team, NONE);
}

void social_chat_update(
	void)
{
	unsigned long now = system_milliseconds();

	if (!social_chat_replaces_upstream())
	{
		social_chat_close();
		return;
	}
	/* (the players muted in this match alone: heard again in the next) */
	{
		char room[SOCIAL_CHAT_ROOM_SIZE];

		social_chat_room(room, sizeof(room));
		if (strcmp(room, social_chat.muted_here_room))
		{
			csstrncpy(social_chat.muted_here_room, room, SOCIAL_CHAT_ROOM_SIZE - 1);
			social_chat.muted_here_count = 0;
		}
	}
	/* (the fork's lines) */
	if (social_chat_online())
	{
		if (social_chat.send_ticket)
			social_chat_send_answer();
		if (social_chat.poll_ticket)
			social_chat_poll_answer(now);
		else if (social_account_code()[0] && (long)(now - social_chat.poll_at) >= 0)
			social_chat_poll_start(now);
	}
	if (!social_chat_shown())
	{
		social_chat_close();
		social_chat.toggle_port = 0;
		return;
	}
	if (social_chat.toggle_port)
	{
		short port = (short)(social_chat.toggle_port - 1);

		social_chat.toggle_port = 0;
		if (social_chat.open && now - social_chat.opened_at > SOCIAL_CHAT_TOGGLE_MILLISECONDS)
			social_chat_close();
		else if (!console_is_active())
			social_chat_open(social_chat_network_game() ? _social_chat_match : _social_chat_global, port);
	}
	social_chat_open_keys();
	if (!social_chat.open || console_is_active())
		return;
	if (social_chat_keyboard.open)
		social_chat_keyboard_update();
	social_chat_keys_update();
	/* (opened for a player's menu alone: closed with it) */
	if (social_chat.open && social_chat.menu_only && !social_chat_player.open)
		social_chat_close();
	/* (a channel this game has not, as the game changes) */
	if (social_chat.open)
	{
		social_chat.channel = social_chat_channel_usable(social_chat.channel);
		/* (a line typed for GLOBAL, longer than a MATCH line may be: cut to it) */
		social_chat.typed[social_chat_said_limit(social_chat.channel) - 1] = 0;
	}
}

/* ---------- drawing */

static boolean social_chat_font(
	long *font_index,
	short *line_height)
{
	struct font_header *font;

	*font_index = tag_loaded(FONT_GROUP_TAG, "ui\\small_ui");
	if (*font_index == NONE)
		*font_index = interface_get_tag_index(_interface_font_terminal);
	if (*font_index == NONE)
		return FALSE;
	font = font_definition_get(*font_index);
	*line_height = (short)((font->ascending_height + font->descending_height + font->leading_height) *
		SOCIAL_CHAT_TEXT_SCALE + 0.5f);
	return *line_height > 0;
}

static void social_chat_color(
	short color,
	real alpha,
	real_argb_color *result)
{
	result->alpha = alpha;
	switch (color)
	{
	case _social_chat_color_red:
		result->red = 0.86f;
		result->green = 0.32f;
		result->blue = 0.28f;
		break;
	case _social_chat_color_blue:
		result->red = 0.34f;
		result->green = 0.51f;
		result->blue = 0.86f;
		break;
	case _social_chat_color_notice:
		result->red = 0.93f;
		result->green = 0.82f;
		result->blue = 0.45f;
		break;
	case _social_chat_color_mine:
		result->red = 0.55f;
		result->green = 0.85f;
		result->blue = 0.55f;
		break;
	default:
		result->red = 0.85f;
		result->green = 0.85f;
		result->blue = 0.85f;
		break;
	}
}

/* the width of a string's first count characters (NONE: all) in the draw mode's font */
static short social_chat_width(
	wchar_t const *string,
	short count)
{
	wchar_t part[SOCIAL_CHAT_TEXT_SIZE + 2 * SOCIAL_CHAT_NAME_SIZE + 32];
	rectangle2d measure;
	rectangle2d text_bounds;
	rectangle2d cursor_bounds;
	short index;

	for (index = 0; string[index] && (count == NONE || index < count) && index < (short)NUMBEROF(part) - 1; index++)
		part[index] = string[index];
	part[index] = 0;
	if (!index)
		return 0;
	measure.x0 = 0;
	measure.y0 = 0;
	measure.x1 = SHORT_MAX;
	measure.y1 = SHORT_MAX;
	draw_unicode_string_compute_bounds(&measure, part, &text_bounds, &cursor_bounds);
	return (short)(cursor_bounds.x0 * SOCIAL_CHAT_TEXT_SCALE + 0.5f);
}

/* how many characters of text from start fit width, broken at a space where
one fits (at least one), and the spaces after them */
static short social_chat_wrap(
	wchar_t const *text,
	short width,
	short *skip)
{
	short length;
	short low = 1;
	short high;
	short index;

	*skip = 0;
	for (length = 0; text[length]; length++)
		;
	if (!length || social_chat_width(text, NONE) <= width)
		return length;
	for (high = length; low < high;)
	{
		short middle = (short)((low + high + 1) / 2);

		if (social_chat_width(text, middle) <= width)
			low = middle;
		else
			high = (short)(middle - 1);
	}
	for (index = low; index > 0 && text[index] != ' '; index--)
		;
	if (index > 0)
		low = index;
	while (text[low + *skip] == ' ')
		(*skip)++;
	return low;
}

static void social_chat_text(
	long font_index,
	rectangle2d const *bounds,
	real_argb_color const *color,
	wchar_t const *text)
{
	rectangle2d scaled = *bounds;
	rectangle2d shadow;
	real_argb_color black;

	/* (the text's scale shrinks its clip rectangle too: widened to the box's) */
	scaled.x1 = (short)(bounds->x0 + (bounds->x1 - bounds->x0) / SOCIAL_CHAT_TEXT_SCALE + 0.5f);
	bounds = &scaled;
	shadow = *bounds;
	black.alpha = color->alpha * 0.6f;
	black.red = black.green = black.blue = 0.0f;
	offset_rectangle2d(&shadow, 1, 1);
	rasterizer_text_set_shadow_color(0x00000001);
	rasterizer_text_set_scale(SOCIAL_CHAT_TEXT_SCALE, (real)shadow.x0, (real)shadow.y0);
	draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &black);
	rasterizer_draw_unicode_string(&shadow, NULL, NULL, 0, text);
	rasterizer_text_set_scale(SOCIAL_CHAT_TEXT_SCALE, (real)bounds->x0, (real)bounds->y0);
	draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, color);
	rasterizer_draw_unicode_string(bounds, NULL, NULL, 0, text);
	rasterizer_text_set_scale(1.0f, 0.0f, 0.0f);
	rasterizer_text_set_shadow_color(0);
}

/* a line's head: its channel's tag, where it came from, and its name ("" for a notice) */
static void social_chat_head(
	struct social_chat_line const *line,
	wchar_t *head,
	long count,
	short *name_start,
	short *name_length)
{
	wchar_t source[8];
	long length;

	head[0] = 0;
	if (name_start)
		*name_start = *name_length = 0;
	if (line->notice)
		return;
	if (line->reported)
		ustrncpy(head, L"[REPORTED] ", count - 1);
	else if (line->failed)
		ustrncpy(head, L"[NOT SENT] ", count - 1);
	head[count - 1] = 0;
	ustrncpy(source, line->sources == (FLAG(_social_chat_upstream_bit) | FLAG(_social_chat_fork_bit)) ? L"U+F" :
		TEST_FLAG(line->sources, _social_chat_upstream_bit) ? L"U" : L"F", NUMBEROF(source) - 1);
	source[NUMBEROF(source) - 1] = 0;
	length = ustrlen(head);
	ustrncat(head, social_chat_channel_tags[line->channel], count - 1 - length);
	head[count - 1] = 0;
	length = ustrlen(head);
	/* (where a line came from is said for a match's alone: the global and
	private lines are all the fork's) */
	if ((line->channel == _social_chat_match || line->channel == _social_chat_team) && length < count - 1)
	{
		head[length++] = '[';
		head[length] = 0;
		ustrncat(head, source, count - 1 - length);
		length = ustrlen(head);
		if (length < count - 2)
		{
			head[length++] = ']';
			head[length++] = ' ';
			head[length] = 0;
		}
	}
	length = ustrlen(head);
	if (name_start)
		*name_start = (short)length;
	ustrncat(head, line->name, count - 1 - length);
	if (name_length)
		*name_length = (short)(ustrlen(head) - length);
	/* (a private line: who it went to, when it was this one's) */
	if (line->channel == _social_chat_private && line->mine && line->peer_name[0])
	{
		length = ustrlen(head);
		ustrncat(head, L" > ", count - 1 - length);
		length = ustrlen(head);
		ustrncat(head, line->peer_name, count - 1 - length);
	}
	length = ustrlen(head);
	ustrncat(head, L": ", count - 1 - length);
}

/* how many rows a line takes in a column of width (its head, then its text) */
static short social_chat_line_rows(
	struct social_chat_line const *line,
	short width,
	wchar_t *head,
	long head_count,
	wchar_t *text,
	long text_count)
{
	short head_width;
	short rows = 0;
	short start = 0;

	social_chat_head(line, head, head_count, NULL, NULL);
	social_chat_wide_from_ascii(line->text, text, text_count);
	head_width = social_chat_width(head, NONE);
	while (text[start] && rows < SOCIAL_CHAT_LINE_ROWS)
	{
		short skip;
		short room = (short)(rows ? width : width - head_width);
		short count = social_chat_wrap(text + start, MAX(room, 8), &skip);

		start = (short)(start + MAX(count, 1) + skip);
		rows++;
	}
	return MAX(rows, 1);
}

/* a line drawn from top, a row each line_height: its head in its colour,
then its text; its head's width */
static short social_chat_line_draw(
	long font_index,
	struct social_chat_line const *line,
	short left,
	short width,
	short top,
	short line_height,
	real alpha)
{
	wchar_t head[SOCIAL_CHAT_NAME_SIZE * 2 + 24];
	wchar_t text[SOCIAL_CHAT_TEXT_SIZE];
	wchar_t row_text[SOCIAL_CHAT_TEXT_SIZE];
	real_argb_color head_color;
	real_argb_color text_color;
	rectangle2d bounds;
	short head_width;
	short rows = 0;
	short start = 0;
	short name_start;
	short name_length;

	social_chat_head(line, head, NUMBEROF(head), &name_start, &name_length);
	social_chat_wide_from_ascii(line->text, text, NUMBEROF(text));
	social_chat_color(line->color, alpha, &head_color);
	social_chat_color(line->notice ? _social_chat_color_notice : _social_chat_color_none, alpha, &text_color);
	if (!line->notice)
	{
		text_color.red = 0.93f;
		text_color.green = 0.93f;
		text_color.blue = 0.93f;
	}
	if (line->channel == _social_chat_private && !line->notice)
	{
		text_color.red = 0.95f;
		text_color.green = 0.7f;
		text_color.blue = 0.95f;
	}
	else if (line->channel == _social_chat_global && !line->notice)
	{
		text_color.red = 0.75f;
		text_color.green = 0.88f;
		text_color.blue = 1.0f;
	}
	head_width = social_chat_width(head, NONE);
	bounds.x0 = left;
	bounds.x1 = (short)(left + width);
	bounds.y0 = top;
	bounds.y1 = (short)(top + line_height);
	if (head[0] && line->name_color && name_length)
	{
		/* (the name in the colour its player picked, the rest of the head in the line's) */
		wchar_t piece[SOCIAL_CHAT_NAME_SIZE * 2 + 24];
		real_argb_color name_color = head_color;
		short from[3];
		short to[3];
		short index;

		name_color.red = ((line->name_color >> 16) & 0xFF) / 255.0f;
		name_color.green = ((line->name_color >> 8) & 0xFF) / 255.0f;
		name_color.blue = (line->name_color & 0xFF) / 255.0f;
		from[0] = 0;
		to[0] = name_start;
		from[1] = name_start;
		to[1] = (short)(name_start + name_length);
		from[2] = to[1];
		to[2] = (short)ustrlen(head);
		for (index = 0; index < 3; index++)
		{
			short length = (short)(to[index] - from[index]);

			if (length <= 0)
				continue;
			ustrncpy(piece, head + from[index], length);
			piece[length] = 0;
			bounds.x0 = (short)(left + (from[index] ? social_chat_width(head, from[index]) : 0));
			social_chat_text(font_index, &bounds, index == 1 ? &name_color : &head_color, piece);
		}
		bounds.x0 = left;
	}
	else if (head[0])
		social_chat_text(font_index, &bounds, &head_color, head);
	while (text[start] && rows < SOCIAL_CHAT_LINE_ROWS)
	{
		short skip;
		short room = (short)(rows ? width : width - head_width);
		short count = social_chat_wrap(text + start, MAX(room, 8), &skip);
		short index;

		count = MAX(count, 1);
		for (index = 0; index < count && index < (short)NUMBEROF(row_text) - 1; index++)
			row_text[index] = text[start + index];
		row_text[index] = 0;
		bounds.x0 = (short)(rows ? left : left + head_width);
		bounds.y0 = (short)(top + rows * line_height);
		bounds.y1 = (short)(bounds.y0 + line_height);
		social_chat_text(font_index, &bounds, &text_color, row_text);
		start = (short)(start + count + skip);
		rows++;
	}
	return head_width;
}

/* the on-screen keyboard, under the box, its key picked lit; where it was
drawn kept, for a tap */
static void social_chat_keyboard_draw(
	long font_index,
	short line_height,
	short screen_width,
	short screen_height,
	short left,
	short top)
{
	static pixel32 const KEY = 0xC0102438UL;
	static pixel32 const PICKED_KEY = 0xF02896FFUL;
	static pixel32 const LIT_KEY = 0xE0205080UL;
	static wchar_t const *const wide_labels[] = { L"SHIFT", L"#+=", L"SPACE", L"DEL", L"SEND" };
	real_argb_color white = get_ui_argb_white();
	short key_width = (short)(screen_width * SOCIAL_CHAT_KEYBOARD_PERCENT / 100 / SOCIAL_CHAT_KEYBOARD_COLUMNS);
	short key_height = (short)MAX(key_width * 2 / 3, line_height + 2 * SOCIAL_CHAT_KEY_GAP + 4);
	rectangle2d panel;
	short row;
	short column;

	if (key_width <= 2 * SOCIAL_CHAT_KEY_GAP + 4 || top + key_height * SOCIAL_CHAT_KEYBOARD_ROWS > screen_height)
		return;
	panel.x0 = left;
	panel.y0 = top;
	panel.x1 = (short)(left + key_width * SOCIAL_CHAT_KEYBOARD_COLUMNS);
	panel.y1 = (short)(top + key_height * SOCIAL_CHAT_KEYBOARD_ROWS);
	draw_quad(&panel, (pixel32)0xA0 << 24);
	for (row = 0; row < SOCIAL_CHAT_KEYBOARD_ROWS; row++)
	{
		short span = row == SOCIAL_CHAT_KEYBOARD_ROWS - 1 ? SOCIAL_CHAT_KEYBOARD_WIDE_COLUMNS : 1;

		for (column = 0; column < SOCIAL_CHAT_KEYBOARD_COLUMNS; column += span)
		{
			boolean picked = social_chat_keyboard.row == row &&
				social_chat_keyboard.column / span == column / span;
			boolean lit = row == SOCIAL_CHAT_KEYBOARD_ROWS - 1 &&
				((column / span == _social_chat_key_shift && social_chat_keyboard.shift) ||
					(column / span == _social_chat_key_symbols && social_chat_keyboard.symbols));
			wchar_t label[8];
			rectangle2d key;
			rectangle2d bounds;

			key.x0 = (short)(panel.x0 + column * key_width + SOCIAL_CHAT_KEY_GAP);
			key.x1 = (short)(panel.x0 + (column + span) * key_width - SOCIAL_CHAT_KEY_GAP);
			key.y0 = (short)(panel.y0 + row * key_height + SOCIAL_CHAT_KEY_GAP);
			key.y1 = (short)(panel.y0 + (row + 1) * key_height - SOCIAL_CHAT_KEY_GAP);
			draw_quad(&key, picked ? PICKED_KEY : lit ? LIT_KEY : KEY);
			if (row < SOCIAL_CHAT_KEYBOARD_ROWS - 1)
			{
				label[0] = (wchar_t)(unsigned char)social_chat_keyboard_character(row, column);
				label[1] = 0;
			}
			else
			{
				ustrncpy(label, wide_labels[column / span], NUMBEROF(label) - 1);
				label[NUMBEROF(label) - 1] = 0;
			}
			bounds = key;
			bounds.y0 = (short)(key.y0 + (key.y1 - key.y0 - line_height) / 2);
			bounds.y1 = (short)(bounds.y0 + line_height);
			draw_string_set_draw_mode(font_index, NONE, 2 /* centred */, 0, &white);
			rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, label);
		}
	}
	social_chat_keyboard.bounds = panel;
	social_chat_keyboard.drawn = TRUE;
}

/* the box's foot while open: the channels (the one picked lit), who a
private line goes to, the line typed, and the keys' hint; where the
channels and the private line's player were drawn kept, for a click (the
screen's coordinates: shift from them) */
/* a keyboard drawn at the right end of the row given (its outline, two
rows of keys and a space bar), lit or not; its width and a gap */
static short social_chat_keyboard_button_draw(
	rectangle2d const *row,
	boolean lit)
{
	pixel32 const color = lit ? 0xFF4F9BE0UL : 0xFFD0D0D0UL;
	short height = (short)MAX(row->y1 - row->y0 - 4, 6);
	short width = (short)(height * 3 / 2);
	short thick = (short)MAX(height / 12, 1);
	rectangle2d icon;
	rectangle2d part;
	short key_row;
	short key;

	icon.x1 = (short)(row->x1 - 2);
	icon.x0 = (short)(icon.x1 - width);
	icon.y0 = (short)(row->y0 + (row->y1 - row->y0 - height) / 2);
	icon.y1 = (short)(icon.y0 + height);
	if (lit)
		draw_quad(&icon, 0x404F9BE0UL);
	part = icon; part.y1 = (short)(icon.y0 + thick); draw_quad(&part, color);
	part = icon; part.y0 = (short)(icon.y1 - thick); draw_quad(&part, color);
	part = icon; part.x1 = (short)(icon.x0 + thick); draw_quad(&part, color);
	part = icon; part.x0 = (short)(icon.x1 - thick); draw_quad(&part, color);
	/* (two rows of five keys, then the space bar) */
	for (key_row = 0; key_row < 2; key_row++)
	{
		for (key = 0; key < 5; key++)
		{
			part.x0 = (short)(icon.x0 + width * (2 + key * 4) / 22);
			part.x1 = (short)(part.x0 + MAX(width * 2 / 22, 1));
			part.y0 = (short)(icon.y0 + height * (2 + key_row * 3) / 11);
			part.y1 = (short)(part.y0 + MAX(height * 2 / 11, 1));
			draw_quad(&part, color);
		}
	}
	part.x0 = (short)(icon.x0 + width * 6 / 22);
	part.x1 = (short)(icon.x0 + width * 16 / 22);
	part.y0 = (short)(icon.y0 + height * 8 / 11);
	part.y1 = (short)(part.y0 + MAX(height * 2 / 11, 1));
	draw_quad(&part, color);
	return (short)(width + 6);
}

static short social_chat_open_draw(
	long font_index,
	short line_height,
	short left,
	short width,
	short top,
	short shift)
{
	static real_argb_color const blue = { 1.0f, 0x28 / 255.0f, 0x96 / 255.0f, 1.0f };
	static real_argb_color const grey = { 1.0f, 0.55f, 0.55f, 0.55f };
	real_argb_color white = get_ui_argb_white();
	rectangle2d bounds;
	wchar_t text[SOCIAL_CHAT_SAID_SIZE + 64];
	short x = left;
	short channel;

	bounds.y0 = top;
	bounds.y1 = (short)(top + line_height);
	for (channel = 0; channel < NUMBER_OF_SOCIAL_CHAT_CHANNELS; channel++)
	{
		wchar_t name[16];
		boolean usable = social_chat_channel_usable(channel) == channel;

		social_chat_wide_from_ascii(social_chat_channel_names[channel], name, NUMBEROF(name));
		bounds.x0 = x;
		bounds.x1 = (short)(left + width);
		social_chat_text(font_index, &bounds, channel == social_chat.channel ? &blue : usable ? &white : &grey, name);
		/* (half the gap either side of it its own) */
		social_chat_hits.channels[channel].x0 = (short)(x + shift - (channel ? line_height / 2 : 0));
		social_chat_hits.channels[channel].x1 = (short)(x + shift + social_chat_width(name, NONE) + line_height / 2);
		social_chat_hits.channels[channel].y0 = bounds.y0;
		social_chat_hits.channels[channel].y1 = bounds.y1;
		x = (short)(x + social_chat_width(name, NONE) + line_height);
	}
	top = (short)(top + line_height);
	csmemset(&social_chat_hits.peer, 0, sizeof(social_chat_hits.peer));
	if (social_chat.channel == _social_chat_private)
	{
		struct social_chat_peer const *peer = social_chat_peer_picked();

		ustrncpy(text, L"TO: ", NUMBEROF(text) - 1);
		text[NUMBEROF(text) - 1] = 0;
		if (peer)
			ustrncat(text, peer->name, NUMBEROF(text) - 1 - ustrlen(text));
		else
		{
			ustrncat(text, L"no one yet", NUMBEROF(text) - 1 - ustrlen(text));
		}
		bounds.x0 = left;
		bounds.x1 = (short)(left + width);
		bounds.y0 = top;
		bounds.y1 = (short)(top + line_height);
		social_chat_text(font_index, &bounds, &white, text);
		social_chat_hits.peer = bounds;
		social_chat_hits.peer.x0 = (short)(bounds.x0 + shift);
		social_chat_hits.peer.x1 = (short)(bounds.x1 + shift);
		top = (short)(top + line_height);
	}
	/* (the line typed, its end where it is wider than the box, and the caret) */
	{
		wchar_t typed[SOCIAL_CHAT_SAID_SIZE + 2];
		short start = 0;
		short length;

		social_chat_wide_from_ascii(social_chat.typed, typed, SOCIAL_CHAT_SAID_SIZE);
		length = (short)ustrlen(typed);
		/* (docked and closed, or open for a player's menu alone: where a
		line would be typed) */
		if (!social_chat.open || social_chat.menu_only)
		{
			social_chat_wide_from_ascii("A or click: write", typed, SOCIAL_CHAT_SAID_SIZE);
			length = 0;
		}
		/* (the system's keyboard not up yet: tapped here, it comes) */
		else if (social_chat_keyboard.system && !social_chat_keyboard.system_up && !length)
		{
			social_chat_wide_from_ascii("Tap to write", typed, SOCIAL_CHAT_SAID_SIZE);
		}
		else if ((system_milliseconds() / SOCIAL_CHAT_CARET_MILLISECONDS) % 2 == 0)
		{
			typed[length] = '_';
			typed[length + 1] = 0;
			length++;
		}
		bounds.x0 = left;
		bounds.x1 = (short)(left + width);
		bounds.y0 = top;
		bounds.y1 = (short)(top + line_height);
		social_chat_hits.input = bounds;
		social_chat_hits.input.x0 = (short)(bounds.x0 + shift);
		social_chat_hits.input.x1 = (short)(bounds.x1 + shift);
		/* (the system's keyboard, a touchscreen's: a keyboard button at the
		line's right end, lit while that keyboard is up) */
		if (social_chat.open && !social_chat.menu_only && social_chat_keyboard.system)
		{
			short button = social_chat_keyboard_button_draw(&bounds, social_chat_keyboard.system_up);

			bounds.x1 = (short)(bounds.x1 - button);
		}
		for (;;)
		{
			ustrncpy(text, L"> ", NUMBEROF(text) - 1);
			text[NUMBEROF(text) - 1] = 0;
			ustrncat(text, typed + start, NUMBEROF(text) - 1 - ustrlen(text));
			if (start >= length || social_chat_width(text, NONE) <= bounds.x1 - bounds.x0 - 4)
				break;
			start++;
		}
		social_chat_text(font_index, &bounds, social_chat.open ? &white : &grey, text);
		top = (short)(top + line_height);
	}
	/* (no hint of the keys: only that the fork's chat is not reached) */
	if (!social_chat_online())
	{
		wchar_t wide[64];

		social_chat_wide_from_ascii("(fork chat offline)", wide, NUMBEROF(wide));
		bounds.x0 = left;
		bounds.x1 = (short)(left + width);
		bounds.y0 = top;
		bounds.y1 = (short)(top + line_height);
		social_chat_text(font_index, &bounds, &grey, wide);
		top = (short)(top + line_height);
	}
	return top;
}

/* a player's menu, beside the box from top: its name, then its
options (the one chosen lit) or its profile's lines; where each was drawn
kept, for a click */
static void social_chat_player_draw(
	long font_index,
	short line_height,
	short screen_width,
	short screen_height,
	short left,
	short top)
{
	static real_argb_color const blue = { 1.0f, 0x28 / 255.0f, 0x96 / 255.0f, 1.0f };
	static real_argb_color const grey = { 1.0f, 0.65f, 0.65f, 0.65f };
	static char const *const labels[] = { "SEND PRIVATE MESSAGE", "ADD FRIEND", "VIEW PROFILE", "MUTE", "UNMUTE",
		"REPORT", "MUTE IN THIS MATCH", "UNMUTE", "KICK FROM GAME", "REMOVE BOT" };
	real_argb_color white = get_ui_argb_white();
	char text[SOCIAL_CHAT_PLAYER_TEXT_SIZE];
	wchar_t wide[SOCIAL_CHAT_TEXT_SIZE];
	short width = (short)(screen_width * SOCIAL_CHAT_PLAYER_WIDTH_PERCENT / 100);
	short rows;
	short row;
	short index;
	rectangle2d bounds;
	short status = 0;

	text[0] = 0;
	/* (VIEW PROFILE: the player's card, the Social screen's, in the middle
	of the screen, in its 480 lines; a click on it, B or Escape goes back) */
	if (social_chat_player.profile)
	{
		real scale = (real)screen_height / 480.0f;
		short card_width = (short)MIN(screen_width - 40 * scale, SOCIAL_MENU_CARD_MOST_WIDTH * scale);
		short card_left = (short)((screen_width - card_width) / 2);
		short card_top = (short)((screen_height - 335 * scale) / 2);
		short bottom = social_menu_profile_card_draw(social_chat_player.code, card_left, card_top,
			(short)(card_left + card_width), scale);

		if (bottom > card_top)
		{
			social_chat_player.panel.x0 = card_left;
			social_chat_player.panel.y0 = card_top;
			social_chat_player.panel.x1 = (short)(card_left + card_width);
			social_chat_player.panel.y1 = bottom;
			bounds.x0 = card_left;
			bounds.x1 = social_chat_player.panel.x1;
			bounds.y0 = (short)(bottom + 4);
			bounds.y1 = (short)(bounds.y0 + line_height);
			draw_string_set_draw_mode(font_index, NONE, 2 /* center */, 0, &grey);
			social_chat_wide_from_ascii("CLICK, B OR ESCAPE: BACK", wide, NUMBEROF(wide));
			rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, wide);
			draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &white);
			social_chat_player.drawn = TRUE;
			return;
		}
	}
	if (social_chat_player.profile)
	{
		status = social_menu_profile_text(social_chat_player.code, text, sizeof(text));
		if (!status)
			snprintf(text, sizeof(text), "Asking the server...");
		/* (its lines, and BACK) */
		rows = 1;
		for (index = 0; text[index]; index++)
			rows = (short)(rows + (text[index] == '\n'));
		rows = (short)(rows + 1);
	}
	else
	{
		social_chat_player_options();
		rows = social_chat_player.option_count;
	}
	/* (and its name) */
	rows = (short)(rows + 1);
	if (left + width > screen_width)
		left = (short)MAX(screen_width - width, 0);
	top = (short)PIN(top, 0, MAX(screen_height - rows * line_height - 2 * SOCIAL_CHAT_PADDING, 0));
	social_chat_player.panel.x0 = left;
	social_chat_player.panel.y0 = top;
	social_chat_player.panel.x1 = (short)(left + width);
	social_chat_player.panel.y1 = (short)(top + rows * line_height + 2 * SOCIAL_CHAT_PADDING);
	draw_quad(&social_chat_player.panel, (pixel32)0xD0 << 24);
	bounds.x0 = (short)(left + SOCIAL_CHAT_PADDING);
	bounds.x1 = (short)(left + width - SOCIAL_CHAT_PADDING);
	bounds.y0 = (short)(top + SOCIAL_CHAT_PADDING);
	bounds.y1 = (short)(bounds.y0 + line_height);
	social_chat_text(font_index, &bounds, &blue, social_chat_player.name);
	offset_rectangle2d(&bounds, 0, line_height);
	if (social_chat_player.profile)
	{
		char *line = text;

		while (line && *line)
		{
			char *end = strchr(line, '\n');

			if (end)
				*end = 0;
			social_chat_wide_from_utf8(line, wide, NUMBEROF(wide));
			social_chat_text(font_index, &bounds, &white, wide);
			offset_rectangle2d(&bounds, 0, line_height);
			line = end ? end + 1 : NULL;
		}
		social_chat_wide_from_ascii("< BACK", wide, NUMBEROF(wide));
		social_chat_text(font_index, &bounds, &blue, wide);
		social_chat_player.drawn = TRUE;
		return;
	}
	for (row = 0; row < social_chat_player.option_count; row++)
	{
		short option = social_chat_player.options[row];
		boolean chosen = row == social_chat_player.chosen;
		char label[48];

		snprintf(label, sizeof(label), "%s", (option == _social_chat_option_report || option == _social_chat_option_kick) &&
			social_chat_player.confirm_time &&
			system_milliseconds() - social_chat_player.confirm_time <= SOCIAL_CHAT_PLAYER_CONFIRM_MILLISECONDS ?
			(option == _social_chat_option_kick ? "KICK: AGAIN TO KICK" : "REPORT: AGAIN TO SEND") : labels[option]);
		social_chat_player.option_bounds[row] = bounds;
		social_chat_player.option_bounds[row].x0 = left;
		social_chat_player.option_bounds[row].x1 = (short)(left + width);
		if (chosen)
		{
			rectangle2d lit = social_chat_player.option_bounds[row];

			draw_quad(&lit, 0xC02896FFUL);
		}
		social_chat_wide_from_ascii(label, wide, NUMBEROF(wide));
		social_chat_text(font_index, &bounds, &white, wide);
		offset_rectangle2d(&bounds, 0, line_height);
	}
	social_chat_player.drawn = TRUE;
}

/* the box (box: its lines, its channels and what is typed) and what goes
with it (extras: a player's menu, the on-screen keyboard): as the screen's
overlay, or docked, from the main menu's drawing (in the menus' 640, its
names' places kept in the screen's: dock_shift). Docked, the overlay draws
the extras alone, by the dock's place in the screen */
static void social_chat_draw_in(
	boolean docked_pass)
{
	long font_index;
	short line_height;
	short screen_width;
	short screen_height;
	short left;
	short width;
	short rows_shown;
	short foot_rows;
	short rows_used = 0;
	short shown[SOCIAL_CHAT_MAXIMUM_LINES];
	short shown_count = 0;
	short skipped;
	short back;
	short top;
	short bottom;
	boolean touch = halo_chat_touch_layout() != 0;
	unsigned long now = system_milliseconds();
	/* (docked: where the main menu has it, shown closed too, not faded) */
	boolean docked = docked_pass || social_chat_docked();
	boolean shown_open = social_chat.open || docked;
	boolean box = docked_pass || !docked;
	boolean extras = !docked_pass;
	short shift = docked_pass ? social_chat.dock_shift : 0;
	/* (docked and closed: the line chosen there, the newest lines left out
	to show it; its lines in from the left, the arrow by it) */
	boolean picking = docked && !social_chat.open && social_chat.dock_picked;
	short indent = docked ? SOCIAL_CHAT_DOCK_INDENT : 0;
	short text_width;

	if (extras)
	{
		social_chat_keyboard.drawn = FALSE;
		social_chat_player.drawn = FALSE;
	}
	if (box)
	{
		social_chat_player.hit_count = 0;
		social_chat_hits.drawn = FALSE;
	}
	if (!social_chat_shown() || (!social_chat.open && cinematic_in_progress()))
		return;
	if (!shown_open && !social_chat.line_count)
		return;
	if (!social_chat_font(&font_index, &line_height))
		return;
	screen_width = (short)(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0);
	screen_height = (short)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0);
	if (screen_width <= 0 || screen_height <= 0)
		return;
	left = (short)(screen_width * SOCIAL_CHAT_LEFT_PERCENT / 100);
	width = (short)(screen_width * SOCIAL_CHAT_WIDTH_PERCENT / 100);
	/* (open or docked: the channels, who a private line goes to, the line
	typed, and that the fork's chat is offline) */
	foot_rows = shown_open ? (short)(2 + (social_chat.channel == _social_chat_private) +
		!social_chat_online()) : 0;
	rows_shown = social_chat.open ? SOCIAL_CHAT_LINES_OPEN : SOCIAL_CHAT_LINES_CLOSED;
	if (docked)
	{
		/* (the overlay's extras by the dock's place in the screen) */
		left = (short)(social_chat.dock.x0 + (docked_pass ? 0 : social_chat.dock_shift));
		width = (short)(social_chat.dock.x1 - social_chat.dock.x0);
		rows_shown = (short)MAX((social_chat.dock.y1 - social_chat.dock.y0 - 2 * SOCIAL_CHAT_PADDING) / line_height -
			foot_rows, 0);
	}
	text_width = (short)(width - 2 * SOCIAL_CHAT_PADDING - indent);
	{
		real_argb_color white = get_ui_argb_white();

		draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &white);
	}
	/* (the lines shown, the newest at the bottom: open, the latest, from
	where it is scrolled to; closed, the latest new ones not faded (docked,
	from the newest not left out); those not shown now,
	social_chat_line_shown, left out. Docked, the line chosen older than
	those that fit: the newest shown left out too, until it fits) */
	for (;;)
	{
		skipped = 0;
		rows_used = 0;
		shown_count = 0;
		for (back = 0; back < social_chat.line_count; back++)
		{
			struct social_chat_line const *line = social_chat_line_at(back);
			wchar_t head[SOCIAL_CHAT_NAME_SIZE * 2 + 24];
			wchar_t text[SOCIAL_CHAT_TEXT_SIZE];
			short rows;

			if (!social_chat_line_shown(line))
				continue;
			if (social_chat.open && skipped < social_chat.scroll)
			{
				skipped++;
				continue;
			}
			if (docked && !social_chat.open && back < social_chat.dock_scroll)
				continue;
			if (!shown_open && (!line->time || now - line->time > SOCIAL_CHAT_LINE_MILLISECONDS +
				SOCIAL_CHAT_FADE_MILLISECONDS))
			{
				break;
			}
			rows = social_chat_line_rows(line, text_width, head, NUMBEROF(head), text, NUMBEROF(text));
			if (rows_used + rows > rows_shown)
				break;
			rows_used = (short)(rows_used + rows);
			shown[shown_count++] = back;
		}
		if (!picking || !shown_count || social_chat.dock_pick <= shown[shown_count - 1])
			break;
		social_chat.dock_scroll = (short)(shown[0] + 1);
	}
	if (!shown_open && !rows_used)
		return;
	if (docked)
	{
		/* (the whole dock, its lines down to its foot) */
		top = social_chat.dock.y0;
		bottom = social_chat.dock.y1;
	}
	else if (touch)
	{
		top = (short)(screen_height * SOCIAL_CHAT_TOUCH_TOP_PERCENT / 100);
		bottom = (short)(top + (rows_used + foot_rows) * line_height + 2 * SOCIAL_CHAT_PADDING);
	}
	else
	{
		bottom = (short)(screen_height * SOCIAL_CHAT_BOTTOM_PERCENT / 100);
		top = (short)(bottom - (rows_used + foot_rows) * line_height - 2 * SOCIAL_CHAT_PADDING);
	}
	if (shown_open && box)
	{
		rectangle2d panel;

		panel.x0 = left;
		panel.x1 = (short)(left + width);
		panel.y0 = top;
		panel.y1 = bottom;
		draw_quad(&panel, (pixel32)0x99 << 24);
		social_chat_hits.box = panel;
		social_chat_hits.box.x0 = (short)(panel.x0 + shift);
		social_chat_hits.box.x1 = (short)(panel.x1 + shift);
		social_chat_hits.drawn = TRUE;
		/* (docked and chosen there, or open: its edge lit) */
		if (docked && (social_chat.dock_chosen || (social_chat.open && !social_chat.menu_only)))
		{
			rectangle2d edge = panel;

			edge.y1 = (short)(edge.y0 + 1);
			draw_quad(&edge, 0xE04F9BE0UL);
			edge.y0 = (short)(panel.y1 - 1);
			edge.y1 = panel.y1;
			draw_quad(&edge, 0xE04F9BE0UL);
			edge = panel;
			edge.x1 = (short)(edge.x0 + 1);
			draw_quad(&edge, 0xE04F9BE0UL);
			edge.x0 = (short)(panel.x1 - 1);
			edge.x1 = panel.x1;
			draw_quad(&edge, 0xE04F9BE0UL);
		}
	}
	/* (the lines, oldest first, down from the top; docked, down to its foot) */
	{
		short y = (short)(top + SOCIAL_CHAT_PADDING);
		short index;

		if (docked)
			y = (short)MAX(y, bottom - SOCIAL_CHAT_PADDING - (rows_used + foot_rows) * line_height);

		for (index = (short)(shown_count - 1); index >= 0 && box; index--)
		{
			struct social_chat_line const *line = social_chat_line_at(shown[index]);
			wchar_t head[SOCIAL_CHAT_NAME_SIZE * 2 + 24];
			wchar_t text[SOCIAL_CHAT_TEXT_SIZE];
			real alpha = 1.0f;
			short rows = social_chat_line_rows(line, text_width, head, NUMBEROF(head), text, NUMBEROF(text));

			if (!shown_open && now - line->time > SOCIAL_CHAT_LINE_MILLISECONDS)
			{
				alpha = 1.0f - (real)(now - line->time - SOCIAL_CHAT_LINE_MILLISECONDS) /
					(real)SOCIAL_CHAT_FADE_MILLISECONDS;
			}
			/* (the line chosen, docked: an arrow left of it, at its first row) */
			if (picking && shown[index] == social_chat.dock_pick)
			{
				short half = (short)MAX(line_height / 5, 2);
				short middle = (short)(y + line_height / 2);
				short column;

				for (column = 0; column <= half; column++)
				{
					rectangle2d arrow;

					arrow.x0 = (short)(left + SOCIAL_CHAT_PADDING / 2 + column);
					arrow.x1 = (short)(arrow.x0 + 1);
					arrow.y0 = (short)(middle - (half - column));
					arrow.y1 = (short)(middle + (half - column) + 1);
					draw_quad(&arrow, 0xFF4F9BE0UL);
				}
			}
			{
				short head_width = social_chat_line_draw(font_index, line, (short)(left + SOCIAL_CHAT_PADDING + indent),
					text_width, y, line_height, alpha);

				/* (its player's name can be clicked, open: one whose account is
				known, and an upstream player's not of this machine, muted in
				this match by its name) */
				if (shown_open && !line->mine && !line->notice && line->name[0] &&
					(line->code[0] || (TEST_FLAG(line->sources, _social_chat_upstream_bit) &&
						!social_chat_name_is_local(line->name))) &&
					social_chat_player.hit_count < SOCIAL_CHAT_MAXIMUM_NAME_HITS)
				{
					short hit = social_chat_player.hit_count++;

					social_chat_player.hits[hit].bounds.x0 = (short)(left + shift + SOCIAL_CHAT_PADDING + indent);
					social_chat_player.hits[hit].bounds.x1 = (short)(left + shift + SOCIAL_CHAT_PADDING + indent +
						head_width);
					social_chat_player.hits[hit].bounds.y0 = y;
					social_chat_player.hits[hit].bounds.y1 = (short)(y + line_height);
					csstrncpy(social_chat_player.hits[hit].code, line->code, SOCIAL_CHAT_CODE_SIZE - 1);
					social_chat_player.hits[hit].code[SOCIAL_CHAT_CODE_SIZE - 1] = 0;
					ustrncpy(social_chat_player.hits[hit].name, line->name, SOCIAL_CHAT_NAME_SIZE - 1);
					social_chat_player.hits[hit].name[SOCIAL_CHAT_NAME_SIZE - 1] = 0;
				}
			}
			y = (short)(y + rows * line_height);
		}
		if (shown_open && box)
			y = social_chat_open_draw(font_index, line_height, (short)(left + SOCIAL_CHAT_PADDING),
				(short)(width - 2 * SOCIAL_CHAT_PADDING), y, shift);
		if (social_chat.open && extras)
		{
			/* (docked: left of the box, over the main menu's items) */
			if (social_chat_player.open)
			{
				short player_left = docked ? (short)MAX(left - screen_width * SOCIAL_CHAT_PLAYER_WIDTH_PERCENT / 100 -
					SOCIAL_CHAT_PADDING, 0) : (short)(left + width + SOCIAL_CHAT_PADDING);

				social_chat_player_draw(font_index, line_height, screen_width, screen_height, player_left,
					social_chat_player.top);
			}
			if (social_chat_keyboard.open && !social_chat_keyboard.system && !social_chat.menu_only)
			{
				short keyboard_width = (short)(screen_width * SOCIAL_CHAT_KEYBOARD_PERCENT / 100);
				short keyboard_left = touch ? (short)((screen_width - keyboard_width) / 2) :
					(short)(screen_width - keyboard_width - left);
				short keyboard_top = touch ? (short)(bottom + SOCIAL_CHAT_PADDING) :
					(short)(screen_height - (keyboard_width / SOCIAL_CHAT_KEYBOARD_COLUMNS) * 2 / 3 *
						SOCIAL_CHAT_KEYBOARD_ROWS - screen_height * 4 / 100);

				/* (docked: left of the box, its bottom the box's, over the
				main menu's items) */
				if (docked)
				{
					keyboard_left = (short)MAX(left - keyboard_width - SOCIAL_CHAT_PADDING, 0);
					keyboard_top = (short)(bottom - (keyboard_width / SOCIAL_CHAT_KEYBOARD_COLUMNS) * 2 / 3 *
						SOCIAL_CHAT_KEYBOARD_ROWS);
				}

				social_chat_keyboard_draw(font_index, line_height, screen_width, screen_height, keyboard_left,
					MAX(keyboard_top, 0));
			}
		}
	}
}

void social_chat_draw(
	void)
{
	social_chat_draw_in(FALSE);
	/* (the XP of a game just left, over the menus and the box) */
	firefight_hud_xp_exit_draw((short)(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0),
		(short)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0));
	/* (a friend's game being joined: its panel, over the box too) */
	social_menu_join_draw((short)(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0),
		(short)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0));
}

void social_chat_draw_docked(
	short x0,
	short y0,
	short x1,
	short y1,
	short shift,
	boolean chosen)
{
	social_chat.dock.x0 = x0;
	social_chat.dock.y0 = y0;
	social_chat.dock.x1 = x1;
	social_chat.dock.y1 = y1;
	/* (docked again after a while away: no line chosen) */
	if (!social_chat_docked())
	{
		social_chat.dock_picked = FALSE;
		social_chat.dock_scroll = 0;
	}
	social_chat.dock_shift = shift;
	social_chat.dock_chosen = chosen;
	social_chat.docked_at = system_milliseconds() | 1;
	social_chat_draw_in(TRUE);
}
