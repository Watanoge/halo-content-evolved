/*
SOCIAL_CREATOR.H

What the account made (social_creator.c): its time in Forge and in the Tag
Injector's firing range, the Forge maps it saved and the tag profiles it
made, kept on the community server as the account's `creator` (api-opence's
POST /account/creator) and shown on the player card's CREATOR PROFILE.
*/

#ifndef SOCIAL_CREATOR_H
#define SOCIAL_CREATOR_H

struct server_json_document;

enum
{
	_social_creator_forge_seconds,
	_social_creator_range_seconds,
	_social_creator_forge_maps,
	_social_creator_tag_profiles,
	_social_creator_campaign_profiles,
	_social_creator_multiplayer_profiles,
	NUMBER_OF_SOCIAL_CREATOR_FIELDS
};

/* each field's name in the server's answers and requests */
extern char const *const social_creator_fields[NUMBER_OF_SOCIAL_CREATOR_FIELDS];

/* social.c's, each frame: the time counted, what was made sent */
void social_creator_update(unsigned long now);

/* social.c's, from an answer's account (its token): what the server keeps
of what it made (its `creator`: null for never counted, when the files on
disk are counted and sent as its seed; none from a server before it) */
void social_creator_take(struct server_json_document const *document, int account);

/* something made: a Forge map saved under a new name, a tag profile made
(field), `count` of it */
void social_creator_add(short field, long count);

/* this account's totals (the server's, and what was made since that it
has not heard yet): FALSE while the server never said them */
boolean social_creator_totals(long totals[NUMBER_OF_SOCIAL_CREATOR_FIELDS]);

/* when the account's counting began (the server's ISO time, "" for none) */
char const *social_creator_since(void);

#endif
