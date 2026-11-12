#ifndef MCC_UI_NETWORK_H
#define MCC_UI_NETWORK_H

struct network_game_server;
boolean mcc_ui_network_defer_map(struct network_game_server *server, char const *name);
boolean mcc_ui_network_map_pending(struct network_game_server *server);
/* the MCC map this machine's server is checking before it takes the place of
 * the temporary stock map its game names meanwhile (NULL: none), for the
 * lobby to show */
char const *mcc_ui_network_pending_map_name(void);
void mcc_ui_network_update_map(struct network_game_server *server);
boolean mcc_ui_restart_network_game(void);
boolean mcc_ui_network_restart_settings(struct network_game_server *server);
boolean mcc_ui_network_restart_pregame(void);
void mcc_ui_network_server_dispose(struct network_game_server *server);

#endif
