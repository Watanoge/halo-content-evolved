/*
FORGE_SLIM_MAP.H

A Forge map saved on a campaign level (forge_cache_builder.c's
forge_build_xbox_flg1_map, a base of scenario type solo) is written slim:
the level's map rebuilt with only what a map of one section plays. Its
structure BSPs are the section's alone (every one of the scenario's entries
names that one, so any index loads it), the other sections' lightmaps are
left out, and so are the sounds no stripped level plays (forge_hooks.c's
forge_campaign_level_strip): its music, its cinematics' sounds and its
mission and cutscene dialogue (the combat dialogue of the AI Forge can place
stays). What is left out is counted none (a sound's samples) or shares the
section's bytes (a lightmap no BSP of the map draws). The rest is the level's,
moved closer: the tags, at their addresses, keep their offsets into the new
layout.

The map is read and written as streams (a campaign level is ~275 MB
inflated), the base inflated twice: once for its tag data, at its end, and
once for the bytes kept.
*/

#ifndef __FORGE_SLIM_MAP_H
#define __FORGE_SLIM_MAP_H

#include <stdint.h>
#include <stdio.h>

/* a slim map's header: 'SLM1' at FORGE_SLIM_MAP_SIGNATURE_OFFSET, then the
section it kept (the base's, unused by anything else of the header) */
#define FORGE_SLIM_MAP_SIGNATURE_OFFSET 0x198u
#define FORGE_SLIM_MAP_SECTION_OFFSET 0x19Cu
#define FORGE_SLIM_MAP_SIGNATURE 0x534C4D31u

/* whether an Xbox map's header (0x800 bytes) is a slim map's */
int forge_slim_map_header_is_slim(uint8_t const *header);

/* the base (`base_size` bytes of `in_file`: its header and its zlib stream,
no saved map's trailer) rebuilt slim, keeping structure BSP `section`, to
`out_file` from its start: 0x800 bytes of header room, then the stream.
`header` gets the new header (the base's, its sizes and offsets the new
file's, the trailer's fields cleared); `*out_end` the bytes written. 0 when
the base is not one it can make slim (`out_file` then holds nothing of use) */
int forge_slim_map_write(FILE *in_file, uint32_t base_size, int section, FILE *out_file, uint8_t header[0x800],
	uint32_t *out_end);

#endif
