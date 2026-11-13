/*
STEAM_DOWNLOADS.H

The Steam Workshop screen's model (Settings > Network Setup > STEAM
WORKSHOP: tools/port_settings.py's _steam_workshop, menu_functions.c's
"port steam" functions): signing in with Steam (a QR code the Steam mobile
app scans; the refresh token kept in steam/session.txt of the data root),
then a hub of three lists: MANAGE ACCOUNT (the account, LOG OUT), MCC FILE
STATUS (the shared sets, and the Workshop packages in mcc_maps: updated or
removed) and BROWSE WORKSHOP (the Halo 1 Workshop by sort and type, a page
at a time, with each item's preview picture). Downloads go into mcc_maps,
where the MCC loader reads them (docs/mcc_maps.md):

- the shared sets, from Halo: CE Anniversary's depot of MCC (976733):
  halo1/maps/bitmaps.map to mcc_maps/_shared/singleplayer/bitmaps.map,
  halo1/maps/custom_edition/bitmaps.map and sounds.map to
  mcc_maps/_shared/multiplayer/;
- an item, from MCC's Workshop depot, to mcc_maps/<item id>/: its .map files
  (of maps/ or halo1/maps/) at the folder's top, its JSON and PNG files at
  their paths, lowercase, and workshop_id.txt (its id and title), as the
  browser's importer lays a package out. It is made in steam/staging and
  moved into place whole, an older copy of the item replaced.

The account must own MCC and Halo: CE Anniversary: Steam gives the depots'
keys only then. Steam's servers are reached by threads of this module's;
the screen asks for what it shows each frame.
*/

#ifndef STEAM_DOWNLOADS_H
#define STEAM_DOWNLOADS_H

/* the screen opened and closed (a sign-in kept is resumed at the opening) */
void steam_downloads_open(void);
void steam_downloads_close(void);
/* each frame the screen is up: nonzero once when files were put in
mcc_maps (the caller rescans the MCC catalog: mcc_maps_rescan) */
int steam_downloads_poll(void);

/* the rows of the list shown, the first shown and the one chosen */
int steam_downloads_count(void);
short *steam_downloads_first(void);
short *steam_downloads_chosen(void);
void steam_downloads_row(int place, char *name, int name_size, char *state, int state_size);
void steam_downloads_details(int place, char *title, int title_size, char *text, int text_size);
/* A on a row */
void steam_downloads_choose(int place);
/* B: nonzero when it was taken (a sign-in under way cancelled, a list left
for the hub, or, at the hub, the screen kept while downloads are under way
or queued: A on a Workshop item while one downloads queues it, A on a
queued one takes it out) */
int steam_downloads_back(void);

/* the list shown: 0 the hub (MANAGE ACCOUNT, MCC FILE STATUS, BROWSE
WORKSHOP, SERVER LISTS; signed out, the two ways to sign in and SERVER
LISTS), 1 the account, 2 MCC's files and the installed Workshop items, 3 the
Workshop, 4 the servers' curated lists (the community server's: api-opence's
/workshop/lists), 5 one of them, its maps */
int steam_downloads_view(void);

/* the chosen row's preview picture (a Workshop item's), 256 by 256
0xAARRGGBB pixels copied to pixels (may be NULL): 256, with *key the item's
id, or 0 while there is none */
int steam_downloads_picture(unsigned long long *key, unsigned long *pixels);

/* text the screen is to have typed (the on-screen keyboard: a search, the
account's name, its password, a Steam Guard code): its kind (0: none),
asked for once, with the text to start from and whether it is a secret;
and what was typed (saved: OK pressed, not cancelled) */
int steam_downloads_text_wanted(char *initial, int initial_size, int *secret);
/* the keyboard's caption for a kind of text ("Steam account name"...) */
char const *steam_downloads_text_caption(int kind);
void steam_downloads_text_typed(int kind, char const *text, int saved);

/* the QR code shown while signing in: its side in modules (0: none), and
whether a module is dark; serial changes with the code */
int steam_downloads_qr_size(unsigned *serial);
int steam_downloads_qr_dark(int x, int y);

/* the Workshop item (its decimal id; title for the messages, may be "") a
host's game is on that this machine lacks (mcc_share.c's DOWNLOAD WITH
STEAM): asked of Steam and queued as soon as an account is signed in (now,
or when the screen next signs in) */
void steam_downloads_want_item(char const *id, char const *title);
/* whether this build can reach Steam (not the web build's) */
int steam_downloads_supported(void);

/* (for the tests) the community server's lists' text (cut up) read into
MAXIMUM_LISTS-like arrays of the model's own: the lists' count, -1 when it
is not that text */
int steam_downloads_parse_lists(char *text, void *lists, int list_maximum, void *items, int item_maximum,
	int *item_count);

#endif
