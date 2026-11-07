/*
HALO_FIREFIGHT_ROUTE.H

A dropship's flight route, placed in Forge as Firefight markers of their own
type. The spawner flies it and Forge previews it, both through this curve, so
the line drawn in Forge is the line flown.

A route has two legs, the approach (points, then the hover) and the departure
(the hover, then points). Each leg's outer end has a style: how the ship comes
into being there, or leaves it. A style can run the leg on past the last point
as a tail, out of the map if it must, while the ship is still unseen: points
can't be placed out there, and the map's edge shouldn't be what ends a flight.
A zone with no route flies this too, on the automatic path's two points.

A marker's usage (netgame flag team_index) holds:

	bits 0-3    spawn type: FF_ROUTE_SPAWN_TYPE on a route's point
	bits 4-7    a point's order in its leg (a zone's rounds)
	bit  8      a point's leg (a zone's waves)
	bits 9-10   a point's leg's style, the same on all of them
	bits 11-14  route id, on the point and on the dropship zone it belongs to:
	            0 on a zone means it has none, and flies the automatic path

Bit 15 stays clear, as a usage below 0 means a default elsewhere.
*/

#ifndef HALO_FIREFIGHT_ROUTE_H
#define HALO_FIREFIGHT_ROUTE_H

#include <math.h>

enum
{
	FF_ROUTE_SPAWN_TYPE = 7,
	FF_ROUTE_MAX_ID = 15,
	FF_ROUTE_MAX_LEG_POINTS = 16,
	FF_ROUTE_APPROACH = 0,
	FF_ROUTE_DEPARTURE = 1,
	FF_ROUTE_LEG_COUNT = 2,
	FF_ROUTE_STEPS_PER_SEGMENT = 8,

	/* (the hover and the tail take a place each) */
	FF_ROUTE_LEG_SLOTS = FF_ROUTE_MAX_LEG_POINTS + 2,

	FF_ROUTE_STYLE_SLIPSPACE = 0, /* a flash, and it grows out of it, fast */
	FF_ROUTE_STYLE_CLOAK = 1,     /* fades in where the route starts */
	FF_ROUTE_STYLE_SKY = 2,       /* straight down from far overhead */
	FF_ROUTE_STYLE_LONG_HAUL = 3, /* from far away along the route's line */
	FF_ROUTE_STYLE_COUNT = 4,

	FF_ROUTE_TICKS_PER_SECOND = 30,
	FF_ROUTE_SHORTEST_LEG_TICKS = 90,
	FF_ROUTE_LONGEST_LEG_TICKS = 30 * FF_ROUTE_TICKS_PER_SECOND,
	FF_ROUTE_HOVER_TICKS = 60,

	FF_ROUTE_PHASE_APPROACH = 1,
	FF_ROUTE_PHASE_HOVER = 2,
	FF_ROUTE_PHASE_DEPARTURE = 3
};

/* (world units, from the zone) */
#define FF_ROUTE_HOVER_HEIGHT 3.5f
#define FF_ROUTE_START_BEHIND 35.0f
#define FF_ROUTE_START_HEIGHT 16.0f
#define FF_ROUTE_EXIT_AHEAD 45.0f
#define FF_ROUTE_EXIT_HEIGHT 22.0f

/* (world units per second: the route itself, and a tail, flown much faster) */
#define FF_ROUTE_SPEED 14.0f
#define FF_ROUTE_TAIL_SPEED 70.0f

/* (the smallest a ship is drawn: 0 would mean an object's default scale) */
#define FF_ROUTE_SMALLEST_SCALE 0.02f

#define FF_ROUTE_ID(usage) ((((int)(usage)) >> 11) & 0x0F)
#define FF_ROUTE_WITH_ID(usage, id) \
	((short)((((int)(usage)) & 0x07FF) | ((((int)(id)) & 0x0F) << 11)))
#define FF_ROUTE_POINT_ORDER(usage) ((((int)(usage)) >> 4) & 0x0F)
#define FF_ROUTE_POINT_LEG(usage) ((((int)(usage)) >> 8) & 0x01)
#define FF_ROUTE_POINT_STYLE(usage) ((((int)(usage)) >> 9) & 0x03)
#define FF_ROUTE_POINT_USAGE(id, leg, order, style) \
	((short)(FF_ROUTE_SPAWN_TYPE | ((((int)(order)) & 0x0F) << 4) | \
		((((int)(leg)) & 0x01) << 8) | ((((int)(style)) & 0x03) << 9) | ((((int)(id)) & 0x0F) << 11)))

struct ff_route_point
{
	float x, y, z;
};

struct ff_route_leg
{
	struct ff_route_point points[FF_ROUTE_LEG_SLOTS];
	signed char orders[FF_ROUTE_LEG_SLOTS];
	int count;
	float distances[(FF_ROUTE_LEG_SLOTS - 1) * FF_ROUTE_STEPS_PER_SEGMENT + 1];
	float length;
	float tail_length;
};

/* a whole flight, for the spawner to fly and Forge to test */
struct ff_route_flight
{
	struct ff_route_leg legs[FF_ROUTE_LEG_COUNT];
	int styles[FF_ROUTE_LEG_COUNT];
	int ticks[FF_ROUTE_LEG_COUNT];
	float hover_heading;
};

/* where the ship is on a tick, and how it looks there */
struct ff_route_pose
{
	struct ff_route_point position;
	float heading;
	float pitch;
	float scale;
	float cloak;
	int phase;
};

static inline char const *ff_route_style_name(int style, int leg)
{
	switch (style)
	{
	case FF_ROUTE_STYLE_CLOAK:     return leg == FF_ROUTE_APPROACH ? "DECLOAK" : "CLOAK";
	case FF_ROUTE_STYLE_SKY:       return leg == FF_ROUTE_APPROACH ? "SKY DROP" : "SKY CLIMB";
	case FF_ROUTE_STYLE_LONG_HAUL: return leg == FF_ROUTE_APPROACH ? "LONG APPROACH" : "LONG EXIT";
	default:                       return "SLIPSPACE";
	}
}

/* where a new route's leg starts or ends, to drag from there */
static inline struct ff_route_point ff_route_default_point(
	float zone_x, float zone_y, float zone_z, float facing, int leg)
{
	struct ff_route_point point;
	float along = cosf(facing);
	float across = sinf(facing);

	if (leg == FF_ROUTE_APPROACH)
	{
		point.x = zone_x - along * FF_ROUTE_START_BEHIND;
		point.y = zone_y - across * FF_ROUTE_START_BEHIND;
		point.z = zone_z + FF_ROUTE_START_HEIGHT;
	}
	else
	{
		point.x = zone_x + along * FF_ROUTE_EXIT_AHEAD;
		point.y = zone_y + across * FF_ROUTE_EXIT_AHEAD;
		point.z = zone_z + FF_ROUTE_EXIT_HEIGHT;
	}
	return point;
}

/* points arrive in any order: kept sorted, the same order first come first */
static inline void ff_route_leg_add(struct ff_route_leg *leg, int order, float x, float y, float z)
{
	int slot;

	if (leg->count >= FF_ROUTE_MAX_LEG_POINTS)
		return;
	for (slot = leg->count; slot > 0 && leg->orders[slot - 1] > order; slot--)
	{
		leg->points[slot] = leg->points[slot - 1];
		leg->orders[slot] = leg->orders[slot - 1];
	}
	leg->points[slot].x = x;
	leg->points[slot].y = y;
	leg->points[slot].z = z;
	leg->orders[slot] = (signed char)order;
	leg->count++;
}

static inline float ff_route_distance(struct ff_route_point const *a, struct ff_route_point const *b)
{
	float dx = b->x - a->x;
	float dy = b->y - a->y;
	float dz = b->z - a->z;

	return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* segment's curve at u (0-1): a Catmull-Rom whose tangents follow the point
spacing, so one point far off the others doesn't throw a loop into the line */
static inline struct ff_route_point ff_route_curve(struct ff_route_leg const *leg, int segment, float u)
{
	struct ff_route_point const *p0 = &leg->points[segment > 0 ? segment - 1 : 0];
	struct ff_route_point const *p1 = &leg->points[segment];
	struct ff_route_point const *p2 = &leg->points[segment + 1];
	struct ff_route_point const *p3 = &leg->points[segment + 2 < leg->count ? segment + 2 : segment + 1];
	float d1 = ff_route_distance(p0, p1);
	float d2 = ff_route_distance(p1, p2);
	float d3 = ff_route_distance(p2, p3);
	float scale1 = (d1 + d2) > 0.0001f ? d2 / (d1 + d2) : 0.0f;
	float scale2 = (d2 + d3) > 0.0001f ? d2 / (d2 + d3) : 0.0f;
	float u2 = u * u;
	float u3 = u2 * u;
	float h1 = 2.0f * u3 - 3.0f * u2 + 1.0f;
	float h2 = -2.0f * u3 + 3.0f * u2;
	float t1 = (u3 - 2.0f * u2 + u) * scale1;
	float t2 = (u3 - u2) * scale2;
	struct ff_route_point point;

	point.x = h1 * p1->x + h2 * p2->x + t1 * (p2->x - p0->x) + t2 * (p3->x - p1->x);
	point.y = h1 * p1->y + h2 * p2->y + t1 * (p2->y - p0->y) + t2 * (p3->y - p1->y);
	point.z = h1 * p1->z + h2 * p2->z + t1 * (p2->z - p0->z) + t2 * (p3->z - p1->z);
	return point;
}

static inline void ff_route_leg_measure(struct ff_route_leg *leg)
{
	struct ff_route_point previous = leg->points[0];
	float total = 0.0f;
	int segment, step;

	leg->distances[0] = 0.0f;
	for (segment = 0; segment + 1 < leg->count; segment++)
	{
		for (step = 1; step <= FF_ROUTE_STEPS_PER_SEGMENT; step++)
		{
			struct ff_route_point point =
				ff_route_curve(leg, segment, (float)step / (float)FF_ROUTE_STEPS_PER_SEGMENT);

			total += ff_route_distance(&previous, &point);
			leg->distances[segment * FF_ROUTE_STEPS_PER_SEGMENT + step] = total;
			previous = point;
		}
	}
	leg->length = total;
}

/* runs the leg on past its outer end, the way the style comes in or leaves */
static inline void ff_route_leg_add_tail(struct ff_route_leg *leg, int leg_index, int style, float facing)
{
	int outer = leg_index == FF_ROUTE_APPROACH ? 0 : leg->count - 1;
	int inner = leg_index == FF_ROUTE_APPROACH ? 1 : leg->count - 2;
	struct ff_route_point tail = leg->points[outer];
	float away_x = leg->points[outer].x - leg->points[inner].x;
	float away_y = leg->points[outer].y - leg->points[inner].y;
	float away = sqrtf(away_x * away_x + away_y * away_y);
	int slot;

	/* (straight up or down to the hover: the zone's facing tells which way) */
	if (away > 0.01f)
	{
		away_x /= away;
		away_y /= away;
	}
	else
	{
		float side = leg_index == FF_ROUTE_APPROACH ? -1.0f : 1.0f;

		/* (and from the sky it stays that way) */
		away_x = style == FF_ROUTE_STYLE_SKY ? 0.0f : cosf(facing) * side;
		away_y = style == FF_ROUTE_STYLE_SKY ? 0.0f : sinf(facing) * side;
	}

	switch (style)
	{
	case FF_ROUTE_STYLE_SLIPSPACE:
		tail.x += away_x * 30.0f;
		tail.y += away_y * 30.0f;
		tail.z += 6.0f;
		break;
	case FF_ROUTE_STYLE_SKY:
		tail.x += away_x * 20.0f;
		tail.y += away_y * 20.0f;
		tail.z += 140.0f;
		break;
	case FF_ROUTE_STYLE_LONG_HAUL:
		tail.x += away_x * 180.0f;
		tail.y += away_y * 180.0f;
		tail.z += 25.0f;
		break;
	default:
		return;
	}

	leg->tail_length = ff_route_distance(&leg->points[outer], &tail);
	if (leg_index == FF_ROUTE_APPROACH)
	{
		for (slot = leg->count; slot > 0; slot--)
			leg->points[slot] = leg->points[slot - 1];
		leg->points[0] = tail;
	}
	else
	{
		leg->points[leg->count] = tail;
	}
	leg->count++;
}

/* closes the two legs on the hover, a leg without points taking the automatic
path's end, adds the styles' tails (with styles; laying out points goes without
them) and measures them */
static inline void ff_route_legs_finish(
	struct ff_route_leg *legs, float zone_x, float zone_y, float zone_z, float facing, int const *styles)
{
	struct ff_route_point hover;
	struct ff_route_leg *approach = &legs[FF_ROUTE_APPROACH];
	struct ff_route_leg *departure = &legs[FF_ROUTE_DEPARTURE];
	int slot;

	hover.x = zone_x;
	hover.y = zone_y;
	hover.z = zone_z + FF_ROUTE_HOVER_HEIGHT;

	if (approach->count == 0)
	{
		approach->points[0] = ff_route_default_point(zone_x, zone_y, zone_z, facing, FF_ROUTE_APPROACH);
		approach->count = 1;
	}
	approach->points[approach->count++] = hover;

	if (departure->count == 0)
	{
		departure->points[0] = ff_route_default_point(zone_x, zone_y, zone_z, facing, FF_ROUTE_DEPARTURE);
		departure->count = 1;
	}
	for (slot = departure->count; slot > 0; slot--)
		departure->points[slot] = departure->points[slot - 1];
	departure->points[0] = hover;
	departure->count++;

	if (styles)
	{
		ff_route_leg_add_tail(approach, FF_ROUTE_APPROACH, styles[FF_ROUTE_APPROACH], facing);
		ff_route_leg_add_tail(departure, FF_ROUTE_DEPARTURE, styles[FF_ROUTE_DEPARTURE], facing);
	}

	ff_route_leg_measure(approach);
	ff_route_leg_measure(departure);
}

/* the point this far along the leg, at an even speed however the points lie */
static inline struct ff_route_point ff_route_leg_at(struct ff_route_leg const *leg, float travelled)
{
	int last = (leg->count - 1) * FF_ROUTE_STEPS_PER_SEGMENT;
	int low = 0;
	int high = last;
	float span, part;

	if (leg->count < 2 || travelled <= 0.0f)
		return leg->points[0];
	if (travelled >= leg->length)
		return leg->points[leg->count - 1];

	while (high - low > 1)
	{
		int middle = (low + high) / 2;

		if (leg->distances[middle] <= travelled)
			low = middle;
		else
			high = middle;
	}
	span = leg->distances[high] - leg->distances[low];
	part = span > 0.0001f ? (travelled - leg->distances[low]) / span : 0.0f;
	return ff_route_curve(
		leg,
		low / FF_ROUTE_STEPS_PER_SEGMENT,
		((float)(low % FF_ROUTE_STEPS_PER_SEGMENT) + part) / (float)FF_ROUTE_STEPS_PER_SEGMENT);
}

/* a route flown at its speed, a tail at its own; long ones speed up rather than
hold the wave's troops back */
static inline int ff_route_leg_ticks(struct ff_route_leg const *leg)
{
	float route = leg->length - leg->tail_length;
	float seconds = (route > 0.0f ? route : 0.0f) / FF_ROUTE_SPEED + leg->tail_length / FF_ROUTE_TAIL_SPEED;
	int ticks = (int)(seconds * (float)FF_ROUTE_TICKS_PER_SECOND);

	if (ticks < FF_ROUTE_SHORTEST_LEG_TICKS)
		return FF_ROUTE_SHORTEST_LEG_TICKS;
	return ticks > FF_ROUTE_LONGEST_LEG_TICKS ? FF_ROUTE_LONGEST_LEG_TICKS : ticks;
}

/* a zone with no route of its own: down out of the sky onto it, and back up */
static inline void ff_route_flight_automatic(
	struct ff_route_flight *flight, float zone_x, float zone_y, float zone_z)
{
	flight->styles[FF_ROUTE_APPROACH] = FF_ROUTE_STYLE_SKY;
	flight->styles[FF_ROUTE_DEPARTURE] = FF_ROUTE_STYLE_SKY;
	ff_route_leg_add(&flight->legs[FF_ROUTE_APPROACH], 0, zone_x, zone_y, zone_z + FF_ROUTE_START_HEIGHT);
	ff_route_leg_add(&flight->legs[FF_ROUTE_DEPARTURE], 0, zone_x, zone_y, zone_z + FF_ROUTE_EXIT_HEIGHT);
}

/* legs filled with their points, and their styles set */
static inline void ff_route_flight_finish(
	struct ff_route_flight *flight, float zone_x, float zone_y, float zone_z, float facing)
{
	ff_route_legs_finish(flight->legs, zone_x, zone_y, zone_z, facing, flight->styles);
	flight->ticks[FF_ROUTE_APPROACH] = ff_route_leg_ticks(&flight->legs[FF_ROUTE_APPROACH]);
	flight->ticks[FF_ROUTE_DEPARTURE] = ff_route_leg_ticks(&flight->legs[FF_ROUTE_DEPARTURE]);
	flight->hover_heading = facing;
}

static inline int ff_route_flight_total_ticks(struct ff_route_flight const *flight)
{
	return flight->ticks[FF_ROUTE_APPROACH] + FF_ROUTE_HOVER_TICKS + flight->ticks[FF_ROUTE_DEPARTURE];
}

/* how far along the approach by this time (0-1): in fast and braking to the
hover. A departure flies it backwards, so it leaves slowly and goes fast */
static inline float ff_route_ease(int style, float t)
{
	float left = 1.0f - t;

	return style == FF_ROUTE_STYLE_CLOAK ? 1.0f - left * left : 1.0f - left * left * left;
}

static inline float ff_route_smooth(float from, float to, float value)
{
	float amount = (value - from) / (to - from);

	if (amount <= 0.0f)
		return 0.0f;
	return amount >= 1.0f ? 1.0f : amount * amount * (3.0f - 2.0f * amount);
}

/* turns from one heading to the other the short way round */
static inline float ff_route_blend_heading(float from, float to, float amount)
{
	float turn = to - from;

	while (turn > 3.14159265f)
		turn -= 6.2831853f;
	while (turn < -3.14159265f)
		turn += 6.2831853f;
	return from + turn * amount;
}

/* how the ship looks this long after appearing (or this long before it's gone) */
static inline void ff_route_style_look(int style, float seconds, struct ff_route_pose *pose)
{
	switch (style)
	{
	case FF_ROUTE_STYLE_SLIPSPACE:
		pose->scale = FF_ROUTE_SMALLEST_SCALE +
			(1.0f - FF_ROUTE_SMALLEST_SCALE) * ff_route_smooth(0.0f, 0.8f, seconds);
		break;
	case FF_ROUTE_STYLE_CLOAK:
		pose->cloak = 1.0f - ff_route_smooth(0.0f, 2.5f, seconds);
		break;
	case FF_ROUTE_STYLE_SKY:
		pose->cloak = 1.0f - ff_route_smooth(0.3f, 1.6f, seconds);
		break;
	default:
		pose->cloak = 1.0f - ff_route_smooth(0.4f, 2.2f, seconds);
		break;
	}
}

/* the leg's point this far along, and the heading it flies there */
static inline void ff_route_leg_pose(
	struct ff_route_leg const *leg, float fraction, struct ff_route_pose *pose)
{
	float travelled = fraction * leg->length;
	struct ff_route_point ahead = ff_route_leg_at(leg, travelled + 1.0f);
	struct ff_route_point behind = ff_route_leg_at(leg, travelled - 1.0f);
	float dx = ahead.x - behind.x;
	float dy = ahead.y - behind.y;

	pose->position = ff_route_leg_at(leg, travelled);
	if (dx * dx + dy * dy > 0.0001f)
		pose->heading = atan2f(dy, dx);
}

/* the ship on this tick, a fraction of one if the caller's frames are finer */
static inline struct ff_route_pose ff_route_flight_pose(struct ff_route_flight const *flight, float tick)
{
	struct ff_route_pose pose;
	float approach_ticks = (float)flight->ticks[FF_ROUTE_APPROACH];
	float departure_ticks = (float)flight->ticks[FF_ROUTE_DEPARTURE];
	float hover_end = approach_ticks + (float)FF_ROUTE_HOVER_TICKS;

	pose.heading = flight->hover_heading;
	pose.pitch = 0.0f;
	pose.scale = 1.0f;
	pose.cloak = 0.0f;

	if (tick < approach_ticks)
	{
		/* Nose first along the route, swinging round to the zone's facing for the last stretch */
		float t = tick / approach_ticks;
		float eased = ff_route_ease(flight->styles[FF_ROUTE_APPROACH], t);

		ff_route_leg_pose(&flight->legs[FF_ROUTE_APPROACH], eased, &pose);
		pose.heading = ff_route_blend_heading(pose.heading, flight->hover_heading, ff_route_smooth(0.7f, 1.0f, eased));
		pose.pitch = 0.14f * sinf(t * 3.14159265f);
		pose.phase = FF_ROUTE_PHASE_APPROACH;
		ff_route_style_look(
			flight->styles[FF_ROUTE_APPROACH], tick / (float)FF_ROUTE_TICKS_PER_SECOND, &pose);
	}
	else if (tick < hover_end)
	{
		struct ff_route_leg const *approach = &flight->legs[FF_ROUTE_APPROACH];

		pose.position = approach->points[approach->count - 1];
		pose.position.z += 0.08f * sinf((tick - approach_ticks) * 0.15f);
		pose.phase = FF_ROUTE_PHASE_HOVER;
	}
	else
	{
		float t = (tick - hover_end) / departure_ticks;
		float eased = 1.0f - ff_route_ease(flight->styles[FF_ROUTE_DEPARTURE], t > 1.0f ? 0.0f : 1.0f - t);

		ff_route_leg_pose(&flight->legs[FF_ROUTE_DEPARTURE], eased, &pose);
		pose.heading = ff_route_blend_heading(flight->hover_heading, pose.heading, ff_route_smooth(0.0f, 0.3f, eased));
		pose.pitch = 0.20f * (t > 1.0f ? 1.0f : t);
		pose.phase = FF_ROUTE_PHASE_DEPARTURE;
		ff_route_style_look(
			flight->styles[FF_ROUTE_DEPARTURE],
			(departure_ticks - (tick - hover_end)) / (float)FF_ROUTE_TICKS_PER_SECOND,
			&pose);
	}
	return pose;
}

/* when a slipspace flash goes off: as a flight starts, and as it's about to end */
#define FF_ROUTE_FLASH_TICKS_BEFORE_END 8

#endif /* HALO_FIREFIGHT_ROUTE_H */
