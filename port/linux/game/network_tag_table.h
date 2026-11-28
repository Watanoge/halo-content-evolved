/*
NETWORK_TAG_TABLE.H

The host's tag table, as a client of the distributed netcode has it
(network_tag_table.c). The distributed messages name tags by their index in
the level's table, which is the same on every machine only when every
machine loaded the same map: a PAL disc's campaign maps hold a tag the NTSC
discs' do not, and a10's tags are in another order from the Warthog on, so
every tag after the first that differs (and every tag the Tag Injector, the
randomizer or the skulls added after the map's own) has another index on
each.

The host sends a client, when it is ready and before its objects, a key of
each of its tags (a hash of its group and its path) and its datum index. The
client finds each of the host's tags in its own table by its key, and the
tags the host's messages name are made its own as they come in
(network_distributed_handle_message), and its own the host's as its hit
reports go out. A client whose table is the host's, and a client of a host
without it (upstream's: no table comes), changes nothing.
*/

#ifndef __NETWORK_TAG_TABLE_H
#define __NETWORK_TAG_TABLE_H
#pragma once

#include "cseries.h"

/* (network_distributed_new_game) a new map: nothing of the last one's host */
void network_tag_table_reset(void);

/* (the host) its table, to a client that is ready */
void network_tag_table_send(long machine_index);

/* a replay's recording begins (network_replay.c): the host's table in it,
as a client of the host would have read it, so that its playback finds the
recorded tags on any machine. The host's own goes to the recorder; a
client's, the host's as it came, when it came before the recording (one
coming after is recorded as it comes) */
void network_tag_table_record(boolean client);

/* (a client) the host's table, as it comes */
boolean network_tag_table_handles_message(unsigned short const *message, unsigned short size);
void network_tag_table_handle_message(long machine_index, unsigned short const *data, unsigned short size);

/* whether the host's tags are other indices here, all of its table come:
the messages' tags then want translating */
boolean network_tag_table_translating(void);

/* (a client) the host's tags found here, and those not, once its table came
and differs: FALSE before it, or when it is this machine's */
boolean network_tag_table_differs(long *host_count, long *found, long *missing);

/* (a client) the tag the host's datum index names, as this machine has it:
NONE for one this machine does not have; the index unchanged for NONE, or
when there is nothing to translate */
long network_tag_from_host(long tag_index);

/* (a client) the host's datum index of a tag of this machine's: NONE for
one the host does not have; unchanged when there is nothing to translate */
long network_tag_to_host(long tag_index);

#endif
