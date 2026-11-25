#ifndef NETWORK_LEGACY_H
#define NETWORK_LEGACY_H
#include "cseries.h"
/* joining a host of another network version by its layout (network_legacy.c) */
boolean network_legacy_joinable(unsigned version);
void network_legacy_set_host(unsigned version);
unsigned network_legacy_host_version(void);
boolean network_legacy_host_without_epoch(void);
long network_legacy_join_request_cut(void);
long network_legacy_settings_size(void);
boolean network_legacy_settings_to_native(void const *record, long size, void *game, long game_size);
word network_legacy_outgoing(void *message, word size);
word network_legacy_incoming(word const *message, word size, void *buffer, word buffer_size);
#endif
