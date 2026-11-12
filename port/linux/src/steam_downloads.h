/*
STEAM_DOWNLOADS.H

The Steam Workshop screen's model (Settings > Network Setup > STEAM
WORKSHOP: tools/port_settings.py's _steam_workshop, menu_functions.c's
"port steam" functions): signing in with Steam (a QR code the Steam mobile
app scans; the refresh token kept in steam/session.txt of the data root),
MCC's shared resources and the Halo 1 Workshop's items, browsed a page at a
time, and their downloads into mcc_maps where the MCC loader reads them
(docs/mcc_maps.md):

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

/* the rows of the list, the first shown and the one chosen */
int steam_downloads_count(void);
short *steam_downloads_first(void);
short *steam_downloads_chosen(void);
void steam_downloads_row(int place, char *name, int name_size, char *state, int state_size);
void steam_downloads_details(int place, char *title, int title_size, char *text, int text_size);
/* A on a row */
void steam_downloads_choose(int place);
/* B: nonzero when it was taken (a sign-in under way cancelled) */
int steam_downloads_back(void);

/* the QR code shown while signing in: its side in modules (0: none), and
whether a module is dark; serial changes with the code */
int steam_downloads_qr_size(unsigned *serial);
int steam_downloads_qr_dark(int x, int y);

#endif
