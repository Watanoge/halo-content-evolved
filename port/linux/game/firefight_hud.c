/*
FIREFIGHT_HUD.C

Halo 1 CE-styled Firefight HUD module:
- (the set, round, wave, lives, hostiles and time are the score panel's:
  campaign_score.c)
- Pre-Round Preparation Countdown Banner ("ROUND X STARTS IN XXs — RESUPPLY & BOARD VEHICLES")
- Animated Wave / Boss / Skull / Ordnance Announcement Banners
- Generator Defense Alpha / Beta / Gamma Shield & Health Bars + Lockdown status
- 3D->2D World-to-Screen Navpoints for Stragglers (<= 3 enemies) & Ordnance Drops
- Scoreboard header formatter, a multiplayer report's extras, and the
  postgame's XP screen
*/

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cseries/cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/hud.h"
#include "interface/hud_definitions.h"
#include "interface/hud_messaging.h"
#include "interface/interface.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "render/render_camera_projection.h"
#include "render/render_cameras.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "units/units.h"

#include "halo_firefight.h"
#include "halo_skulls.h"
#include "campaign_score.h"
#include "campaign_medals.h"
#include "service_rank.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "interface/ui_widget.h"
#include "rasterizer/rasterizer_swizzle.h"
#include <xtl.h>
#include "rasterizer/xbox/rasterizer_xbox.h"
#include <stdlib.h>

/* (the C library's allocator, not the game's tracked one (cseries.h): the
rank icons' file comes from config_file_read, which allocates with it; the
icons and the working pictures are kept for the whole run) */
#undef malloc
#undef free
#undef realloc

unsigned long system_milliseconds(void);
/* the platform layer's (port_config.c) */
void config_folder(char *path, size_t size);
char *config_file_read(const char *path, size_t *size);
void platform_log(char const *format, ...);

/* ---------- the rank icons' pictures, read from a file

A rank's icon is its tour's shield with its rank's insignia over it: 11 shields
and 42 insignia, 128x128 pictures in rank_icons.bin
(port/assets/firefight/rank_icons.bin, packed by tools/rank_icon_assets.py),
beside config.toml, where every build puts it. Read the first time an icon is
drawn, kept on the heap and not in the game's image (Android's room for it is
full). Its layout, little-endian: "FFRI", then 32-bit version (2), shields,
insignia and a picture's width and height; then the pictures, shields first,
each pixel a 32-bit ARGB colour.
Without the file (logged once) no rank icon is drawn. */
#define FF_RANK_ICONS_FILE "rank_icons.bin"
#define FF_RANK_ICONS_VERSION 2
#define FF_RANK_ICONS_HEADER 20
#define FF_RANK_ICON_DIM 128
#define FF_RANK_ICON_BYTES (FF_RANK_ICON_DIM * FF_RANK_ICON_DIM * 4)

static unsigned long ff_read_u32(unsigned char const *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) |
		((unsigned long)bytes[2] << 16) | ((unsigned long)bytes[3] << 24);
}

/* the file's pictures, or NULL without them */
static unsigned char const *ff_rank_icons(void)
{
	static unsigned char *file;
	static boolean tried;
	char path[1024];
	size_t size = 0;

	if (tried)
		return file ? file + FF_RANK_ICONS_HEADER : NULL;
	tried = TRUE;
	config_folder(path, sizeof(path));
	snprintf(path + strlen(path), sizeof(path) - strlen(path), "%s", FF_RANK_ICONS_FILE);
	file = (unsigned char *)config_file_read(path, &size);
	if (!file || size != FF_RANK_ICONS_HEADER +
			(size_t)(FIREFIGHT_SERVICE_TOURS + FIREFIGHT_SERVICE_RANKS) * FF_RANK_ICON_BYTES ||
		memcmp(file, "FFRI", 4) || ff_read_u32(file + 4) != FF_RANK_ICONS_VERSION ||
		ff_read_u32(file + 8) != FIREFIGHT_SERVICE_TOURS || ff_read_u32(file + 12) != FIREFIGHT_SERVICE_RANKS ||
		ff_read_u32(file + 16) != FF_RANK_ICON_DIM)
	{
		platform_log("Firefight: the rank icons' file %s %s, so no rank icon is drawn", path,
			!file ? "cannot be read" : "is not one this game reads");
		free(file);
		file = NULL;
		return NULL;
	}
	return file + FF_RANK_ICONS_HEADER;
}
void draw_quad(rectangle2d const *bounds, pixel32 color);
struct bitmap_data *bitmap_group_try_and_get_bitmap(long bitmap_group_index, short bitmap_index);
short find_nav_point(char const *name);
short hud_get_nav_point_render_type(short local_player_index, real_point3d const *head, real_point3d const *position, long reference_object_index);
void custom_render_nav_point(short local_player_index, real_point3d const *position_pointer, short nav_index, short waypoint_type);
void render_interpolation_object_point(long object_index, real_point3d *point);

/* ---------- constants */

/* Stock Halo CE pause/objective box palette (matches pause_box_svg in tools/port_settings.py) */
#define FF_HUD_PANEL_FILL  0xBF0A2649
#define FF_HUD_PANEL_BOT   0xBF050D1C
#define FF_HUD_HEADER_FILL 0xBF000000
#define FF_HUD_PANEL_EDGE  0xFF2896FF
#define FF_HUD_WARN_FILL   0xBF481010
#define FF_HUD_WARN_BOT    0xBF220606
#define FF_HUD_WARN_EDGE   0xFFFF4444
#define FF_HUD_BAR_BG      0xBF050D1C
#define FF_HUD_SHIELD_BAR  0xFF2896FF
#define FF_HUD_HEALTH_BAR  0xFF40E070
#define FF_HUD_LOCK_BAR    0xFFFFCC33

enum
{
	_ff_text_left = 0,
	_ff_text_right = 1,
	_ff_text_center = 2
};

/* ---------- helpers */

static pixel32 ff_lerp_argb(pixel32 c0, pixel32 c1, real t)
{
	long a0 = (long)((c0 >> 24) & 0xFF);
	long r0 = (long)((c0 >> 16) & 0xFF);
	long g0 = (long)((c0 >> 8) & 0xFF);
	long b0 = (long)(c0 & 0xFF);
	long a1 = (long)((c1 >> 24) & 0xFF);
	long r1 = (long)((c1 >> 16) & 0xFF);
	long g1 = (long)((c1 >> 8) & 0xFF);
	long b1 = (long)(c1 & 0xFF);
	unsigned long a = (unsigned long)PIN((long)(a0 + (a1 - a0) * t + 0.5f), 0L, 255L);
	unsigned long r = (unsigned long)PIN((long)(r0 + (r1 - r0) * t + 0.5f), 0L, 255L);
	unsigned long g = (unsigned long)PIN((long)(g0 + (g1 - g0) * t + 0.5f), 0L, 255L);
	unsigned long b = (unsigned long)PIN((long)(b0 + (b1 - b0) * t + 0.5f), 0L, 255L);

	return (pixel32)((a << 24) | (r << 16) | (g << 8) | b);
}

static void ff_draw_bitmap_uv(
	struct bitmap_data *bitmap,
	short x0,
	short y0,
	short x1,
	short y1,
	real u0,
	real v0,
	real u1,
	real v1,
	pixel32 tint_argb)
{
	struct dynamic_screen_vertex vertices[4];
	struct rasterizer_dynamic_screen_geometry_parameters parameters;
	int v;

	if (!bitmap || !bitmap->hardware_format || x1 <= x0 || y1 <= y0 || ((tint_argb >> 24) & 0xFFUL) == 0)
		return;

	for (v = 0; v < 4; v++)
	{
		vertices[v].position.x = (v % 3) ? (real)x1 : (real)x0;
		vertices[v].position.y = (v > 1) ? (real)y1 : (real)y0;
		vertices[v].texture_coordinates.x = (v % 3) ? u1 : u0;
		vertices[v].texture_coordinates.y = (v > 1) ? v1 : v0;
		vertices[v].color = tint_argb;
	}

	csmemset(&parameters, 0, sizeof(parameters));
	parameters.meter_parameters = NULL;
	parameters.map_offset[0] = NULL;
	parameters.map_wrapped[0] = FALSE;
	parameters.map_wrapped[1] = FALSE;
	parameters.map_wrapped[2] = FALSE;
	parameters.map_scale[0].i = 1.0f;
	parameters.map_scale[0].j = 1.0f;
	parameters.map_texture_scale[0].i = 1.0f;
	parameters.map_texture_scale[0].j = 1.0f;
	parameters.point_sampled = FALSE;
	parameters.framebuffer_blend_function = 0;
	parameters.map[0] = bitmap;
	parameters.map[1] = NULL;
	parameters.map[2] = NULL;

	rasterizer_psuedo_dynamic_screen_quad_draw(&parameters, vertices);
}

static boolean ff_draw_ce_menu_bitmap_uv(
	char const *tag_name,
	short frame_idx,
	short x0,
	short y0,
	short x1,
	short y1,
	real u0,
	real v0,
	real u1,
	real v1,
	pixel32 tint_argb)
{
	long tag_idx;
	struct bitmap_data *bmp;

	if (!tag_name || !tag_name[0] || x1 <= x0 || y1 <= y0)
		return FALSE;

	tag_idx = tag_loaded(BITMAP_GROUP_TAG, tag_name);
	if (tag_idx == NONE)
		return FALSE;

	bmp = bitmap_group_get_bitmap_from_sequence(tag_idx, 0, frame_idx);
	if (!bmp && frame_idx != 0)
		bmp = bitmap_group_get_bitmap_from_sequence(tag_idx, 0, 0);
	if (!bmp || !bmp->hardware_format || bmp->width <= 0 || bmp->height <= 0)
		return FALSE;

	ff_draw_bitmap_uv(bmp, x0, y0, x1, y1, u0, v0, u1, v1, tint_argb);
	return TRUE;
}

/* Draws a 3-slice horizontally rounded Halo CE option/row pill bar using pc\bitmaps\option_bkds_sm
 * (exact same rounded bar style used in the Forge menu). */
boolean firefight_draw_ce_rounded_pill(
	short px0,
	short py0,
	short px1,
	short py1,
	short frame_idx,
	real scale,
	pixel32 tint_argb)
{
	short w = (short)(px1 - px0);
	short h = (short)(py1 - py0);
	short cap = (short)MIN(w / 2, MAX(4, (int)(12.0f * scale + 0.5f)));
	real v1 = 28.0f / 32.0f;
	real u_cap = 16.0f / 256.0f;

	if (w <= 2 || h <= 2 || ((tint_argb >> 24) & 0xFFUL) == 0)
		return FALSE;

	if (w <= cap * 2)
	{
		return ff_draw_ce_menu_bitmap_uv(
			"pc\\bitmaps\\option_bkds_sm", frame_idx,
			px0, py0, px1, py1,
			0.0f, 0.0f, 1.0f, v1, tint_argb);
	}

	if (ff_draw_ce_menu_bitmap_uv("pc\\bitmaps\\option_bkds_sm", frame_idx, px0, py0, (short)(px0 + cap), py1, 0.0f, 0.0f, u_cap, v1, tint_argb) &&
		ff_draw_ce_menu_bitmap_uv("pc\\bitmaps\\option_bkds_sm", frame_idx, (short)(px0 + cap), py0, (short)(px1 - cap), py1, u_cap, 0.0f, 1.0f - u_cap, v1, tint_argb) &&
		ff_draw_ce_menu_bitmap_uv("pc\\bitmaps\\option_bkds_sm", frame_idx, (short)(px1 - cap), py0, px1, py1, 1.0f - u_cap, 0.0f, 1.0f, v1, tint_argb))
	{
		return TRUE;
	}
	return FALSE;
}

/* Draws the authentic 9-slice Halo CE pause/objectives container box with rounded blue corners
 * (exact same rounded panel style used in the Forge menu). */
void firefight_draw_ce_rounded_box(
	short px0,
	short py0,
	short px1,
	short py1,
	short header_px,
	real scale,
	pixel32 tint_argb,
	pixel32 fallback_fill,
	pixel32 fallback_edge)
{
	short w = (short)(px1 - px0);
	short h = (short)(py1 - py0);
	boolean with_header = (header_px > 0 && header_px < h - 4);
	char const *top_left   = with_header ? "pc\\pause\\objectives_left"   : "pc\\pause\\pausebox_left";
	char const *top_center = with_header ? "pc\\pause\\objectives_center" : "pc\\pause\\pausebox_center";
	char const *top_right  = with_header ? "pc\\pause\\objectives_right"  : "pc\\pause\\pausebox_right";
	char const *obj_left   = "pc\\pause\\objectives_left";
	char const *obj_center = "pc\\pause\\objectives_center";
	char const *obj_right  = "pc\\pause\\objectives_right";
	short frame_idx = 1;
	short cap_w = (short)MIN(w / 3, MAX(6, (int)(14.0f * scale + 0.5f)));
	short top_h = with_header
		? (short)PIN(header_px, (short)8, (short)(h - 6))
		: (short)MIN(h / 2, MAX(4, (int)(11.0f * scale + 0.5f)));
	short bot_h = (short)MIN(h - top_h, MAX(4, (int)(11.0f * scale + 0.5f)));
	real v_top_end = with_header ? (30.0f / 256.0f) : (14.0f / 256.0f);
	real v_body_start = 32.0f / 256.0f;
	real v_body_end = 211.0f / 256.0f;
	real v_bot_end = 225.0f / 256.0f;

	if (w <= 6 || h <= 6 || ((tint_argb >> 24) & 0xFFUL) == 0)
		return;

	/* Compact single-row bars without headers use the 3-slice Halo CE rounded pill (option_bkds_sm) */
	if (!with_header && h <= (short)(20.0f * scale + 0.5f))
	{
		if (firefight_draw_ce_rounded_pill(px0, py0, px1, py1, 0, scale, tint_argb))
			return;
	}

	if (w > cap_w * 2 && top_h > 0 && bot_h > 0)
	{
		boolean ok =
			ff_draw_ce_menu_bitmap_uv(top_left, frame_idx, px0, py0, (short)(px0 + cap_w), (short)(py0 + top_h), 0.0f, 0.0f, 1.0f, v_top_end, tint_argb) &&
			ff_draw_ce_menu_bitmap_uv(top_center, frame_idx, (short)(px0 + cap_w), py0, (short)(px1 - cap_w), (short)(py0 + top_h), 0.0f, 0.0f, 1.0f, v_top_end, tint_argb) &&
			ff_draw_ce_menu_bitmap_uv(top_right, frame_idx, (short)(px1 - cap_w), py0, px1, (short)(py0 + top_h), 0.0f, 0.0f, 1.0f, v_top_end, tint_argb);

		if (ok && (py1 - bot_h) > (py0 + top_h))
		{
			ok =
				ff_draw_ce_menu_bitmap_uv(obj_left, frame_idx, px0, (short)(py0 + top_h), (short)(px0 + cap_w), (short)(py1 - bot_h), 0.0f, v_body_start, 1.0f, v_body_end, tint_argb) &&
				ff_draw_ce_menu_bitmap_uv(obj_center, frame_idx, (short)(px0 + cap_w), (short)(py0 + top_h), (short)(px1 - cap_w), (short)(py1 - bot_h), 0.0f, v_body_start, 1.0f, v_body_end, tint_argb) &&
				ff_draw_ce_menu_bitmap_uv(obj_right, frame_idx, (short)(px1 - cap_w), (short)(py0 + top_h), px1, (short)(py1 - bot_h), 0.0f, v_body_start, 1.0f, v_body_end, tint_argb);
		}

		if (ok)
		{
			ok =
				ff_draw_ce_menu_bitmap_uv(obj_left, frame_idx, px0, (short)(py1 - bot_h), (short)(px0 + cap_w), py1, 0.0f, v_body_end, 1.0f, v_bot_end, tint_argb) &&
				ff_draw_ce_menu_bitmap_uv(obj_center, frame_idx, (short)(px0 + cap_w), (short)(py1 - bot_h), (short)(px1 - cap_w), py1, 0.0f, v_body_end, 1.0f, v_bot_end, tint_argb) &&
				ff_draw_ce_menu_bitmap_uv(obj_right, frame_idx, (short)(px1 - cap_w), (short)(py1 - bot_h), px1, py1, 0.0f, v_body_end, 1.0f, v_bot_end, tint_argb);
		}

		if (ok)
			return;
	}

	/* Fallback if menu bitmap tags are not present */
	{
		short border_px = (short)MAX(2, (int)(2.0f * scale + 0.5f));
		short corner_in = (short)MAX(2, (int)(3.0f * scale + 0.5f));
		short header_end_y = with_header ? (short)(py0 + top_h) : py0;
		short divider_end_y = (header_end_y > py0) ? (short)MIN(py1 - border_px, header_end_y + border_px) : py0;
		short body_y0 = (divider_end_y > py0) ? divider_end_y : (short)(py0 + border_px);
		short body_y1 = (short)(py1 - border_px);
		short inner_x0 = (short)(px0 + border_px);
		short inner_x1 = (short)(px1 - border_px);
		pixel32 bot_fill = (fallback_fill == FF_HUD_WARN_FILL) ? FF_HUD_WARN_BOT : FF_HUD_PANEL_BOT;
		rectangle2d r;
		int band;

		if (inner_x1 <= inner_x0 || body_y1 <= py0 + border_px)
			return;

		r.x0 = (short)(px0 + corner_in);
		r.x1 = (short)(px1 - corner_in);
		r.y0 = py0;
		r.y1 = (short)(py0 + border_px);
		if (r.x1 > r.x0)
			draw_quad(&r, fallback_edge);

		r.y0 = (short)(py1 - border_px);
		r.y1 = py1;
		if (r.x1 > r.x0)
			draw_quad(&r, fallback_edge);

		r.x0 = px0;
		r.x1 = inner_x0;
		r.y0 = (short)(py0 + border_px);
		r.y1 = body_y1;
		draw_quad(&r, fallback_edge);

		r.x0 = inner_x1;
		r.x1 = px1;
		draw_quad(&r, fallback_edge);

		if (header_end_y > py0 + border_px)
		{
			r.x0 = inner_x0;
			r.x1 = inner_x1;
			r.y0 = (short)(py0 + border_px);
			r.y1 = header_end_y;
			draw_quad(&r, FF_HUD_HEADER_FILL);

			r.y0 = header_end_y;
			r.y1 = divider_end_y;
			draw_quad(&r, fallback_edge);
		}

		if (body_y1 > body_y0)
		{
			short body_h = (short)(body_y1 - body_y0);
			for (band = 0; band < 3; band++)
			{
				short by0 = (short)(body_y0 + (body_h * band) / 3);
				short by1 = (band == 2) ? body_y1 : (short)(body_y0 + (body_h * (band + 1)) / 3);
				if (by1 > by0)
				{
					pixel32 fill_color = ff_lerp_argb(fallback_fill, bot_fill, (real)band * 0.5f);
					r.x0 = inner_x0;
					r.x1 = inner_x1;
					r.y0 = by0;
					r.y1 = by1;
					draw_quad(&r, fill_color);
				}
			}
		}
	}
}

/* ---------- the HUD's boxes

drawn here, not from the pause menu's art, whose body has no side border where
it meets the header: a gap in the frame at the size of a banner. Nothing is
drawn twice, the fills being translucent */

static void ff_box_quad(
	short x0,
	short y0,
	short x1,
	short y1,
	pixel32 color)
{
	rectangle2d r;

	if (x1 <= x0 || y1 <= y0)
		return;
	r.x0 = x0;
	r.y0 = y0;
	r.x1 = x1;
	r.y1 = y1;
	draw_quad(&r, color);
}

/* how far in a rounded corner's edge is, `row` rows from its flat side */
static short ff_box_corner_inset(
	short row,
	short radius)
{
	real t = (real)radius - (real)row - 0.5f;

	if (row >= radius)
		return 0;
	return (short)((real)radius - (real)sqrt((double)((real)(radius * radius) - t * t)) + 0.5f);
}

/* a row of the box's inside: `side` of it at each end (the wells') in the
one colour, the middle in the other */
static void ff_box_row(
	short x0,
	short x1,
	short y0,
	short y1,
	short side_x0,
	short side_x1,
	pixel32 middle,
	pixel32 sides)
{
	short middle_x0 = MAX(x0, side_x0);
	short middle_x1 = MIN(x1, side_x1);

	ff_box_quad(x0, y0, middle_x0, y1, sides);
	ff_box_quad(middle_x0, y0, middle_x1, y1, middle);
	ff_box_quad(middle_x1, y0, x1, y1, sides);
}

/* side_px: wells at each end, for the skulls' icons, which the header and its
divider stop short of */
static void ff_draw_panel_box(
	short px0,
	short py0,
	short px1,
	short py1,
	short header_px,
	short side_px,
	real scale,
	pixel32 fill,
	pixel32 edge)
{
	short border = (short)MAX(2, (int)(1.5f * scale + 0.5f));
	short radius = (short)MAX(border + 2, (int)(6.0f * scale + 0.5f));
	pixel32 bottom = fill == FF_HUD_WARN_FILL ? FF_HUD_WARN_BOT : FF_HUD_PANEL_BOT;
	pixel32 top_middle = header_px > 0 ? FF_HUD_HEADER_FILL : fill;
	short well_x0 = (short)(px0 + side_px);
	short well_x1 = (short)(px1 - side_px);
	short body_y0 = (short)(py0 + radius);
	short body_y1 = (short)(py1 - radius);
	short row;
	short band;

	if (px1 - px0 <= radius * 2 || py1 - py0 <= radius * 2)
		return;

	/* the corners, a row at a time, top and bottom */
	for (row = 0; row < radius; row++)
	{
		short outer = ff_box_corner_inset(row, radius);
		short top = (short)(py0 + row);
		short low = (short)(py1 - row - 1);

		if (row < border)
		{
			ff_box_quad((short)(px0 + outer), top, (short)(px1 - outer), (short)(top + 1), edge);
			ff_box_quad((short)(px0 + outer), low, (short)(px1 - outer), (short)(low + 1), edge);
		}
		else
		{
			short inner = (short)(border + ff_box_corner_inset((short)(row - border), (short)(radius - border)));

			ff_box_quad((short)(px0 + outer), top, (short)(px0 + inner), (short)(top + 1), edge);
			ff_box_quad((short)(px1 - inner), top, (short)(px1 - outer), (short)(top + 1), edge);
			ff_box_quad((short)(px0 + outer), low, (short)(px0 + inner), (short)(low + 1), edge);
			ff_box_quad((short)(px1 - inner), low, (short)(px1 - outer), (short)(low + 1), edge);
			ff_box_row((short)(px0 + inner), (short)(px1 - inner), top, (short)(top + 1),
				well_x0, well_x1, top_middle, fill);
			ff_box_quad((short)(px0 + inner), low, (short)(px1 - inner), (short)(low + 1), bottom);
		}
	}

	/* the sides, unbroken */
	ff_box_quad(px0, body_y0, (short)(px0 + border), body_y1, edge);
	ff_box_quad((short)(px1 - border), body_y0, px1, body_y1, edge);

	/* the header and its divider */
	if (header_px > 0)
	{
		short header_y1 = (short)MIN(py0 + header_px, body_y1);
		short divider_y1 = (short)MIN(header_y1 + border, body_y1);

		ff_box_row((short)(px0 + border), (short)(px1 - border), body_y0, header_y1,
			well_x0, well_x1, FF_HUD_HEADER_FILL, fill);
		ff_box_row((short)(px0 + border), (short)(px1 - border), header_y1, divider_y1,
			well_x0, well_x1, edge, fill);
		body_y0 = divider_y1;
	}

	/* the body, shaded to the bottom's fill */
	for (band = 0; band < 3; band++)
	{
		short height = (short)(body_y1 - body_y0);

		ff_box_quad((short)(px0 + border), (short)(body_y0 + (height * band) / 3),
			(short)(px1 - border), band == 2 ? body_y1 : (short)(body_y0 + (height * (band + 1)) / 3),
			ff_lerp_argb(fill, bottom, (real)band * 0.5f));
	}
}

static void ff_draw_box_with_wells(
	real vx0,
	real vy0,
	real scale,
	real x0,
	real y0,
	real x1,
	real y1,
	real header_h,
	real well_w,
	pixel32 fill,
	pixel32 edge)
{
	short px0 = (short)(vx0 + x0 * scale + 0.5f);
	short py0 = (short)(vy0 + y0 * scale + 0.5f);
	short px1 = (short)MAX(vx0 + x1 * scale + 0.5f, px0 + 6);
	short py1 = (short)MAX(vy0 + y1 * scale + 0.5f, py0 + 6);
	short header_px = (header_h > 0.0f && y0 + header_h < y1) ? (short)(header_h * scale + 0.5f) : 0;

	ff_draw_panel_box(px0, py0, px1, py1, header_px, (short)(well_w * scale + 0.5f), scale, fill, edge);
}

static void ff_draw_box(
	real vx0,
	real vy0,
	real scale,
	real x0,
	real y0,
	real x1,
	real y1,
	real header_h,
	pixel32 fill,
	pixel32 edge)
{
	ff_draw_box_with_wells(vx0, vy0, scale, x0, y0, x1, y1, header_h, 0.0f, fill, edge);
}

static void ff_draw_quad(
	real vx0,
	real vy0,
	real scale,
	real x0,
	real y0,
	real x1,
	real y1,
	pixel32 col)
{
	rectangle2d r;
	r.x0 = (short)(vx0 + x0 * scale + 0.5f);
	r.y0 = (short)(vy0 + y0 * scale + 0.5f);
	r.x1 = (short)(vx0 + x1 * scale + 0.5f);
	r.y1 = (short)(vy0 + y1 * scale + 0.5f);
	if (r.x1 > r.x0 && r.y1 > r.y0)
		draw_quad(&r, col);
}

static void ff_draw_text(
	long font_index,
	short line_height,
	real vx0,
	real vy0,
	real scale,
	char const *ascii_text,
	real x0,
	real x1,
	real y,
	real text_scale,
	short justification,
	real_argb_color const *color)
{
	real avail_w = MAX(1.0f, x1 - x0);
	real eff_scale = text_scale;
	real draw_x, ox, oy, final_scale;
	wchar_t wide[128];
	rectangle2d measure_bounds, ink, cursor, bounds;
	short ink_w;
	long i;

	if (!ascii_text || !ascii_text[0] || font_index == NONE)
		return;

	/* (" & " spelled out: the HUD font's & is a stylized one, which the banners'
	scale makes a smudge of) */
	{
		long length = 0;

		for (i = 0; length < (long)NUMBEROF(wide) - 4 && ascii_text[i]; i++)
		{
			if (ascii_text[i] == '&' && i > 0 && ascii_text[i - 1] == ' ' && ascii_text[i + 1] == ' ')
			{
				wide[length++] = 'A';
				wide[length++] = 'N';
				wide[length++] = 'D';
			}
			else
			{
				wide[length++] = (wchar_t)(unsigned char)ascii_text[i];
			}
		}
		wide[length] = 0;
	}

	draw_string_set_draw_mode(font_index, NONE, _ff_text_left, 0, color);
	measure_bounds.x0 = 0;
	measure_bounds.y0 = 0;
	measure_bounds.x1 = 4096;
	measure_bounds.y1 = (short)(line_height * 2);
	draw_unicode_string_compute_bounds(&measure_bounds, wide, &ink, &cursor);

	ink_w = (ink.x1 > ink.x0) ? (short)(ink.x1 - ink.x0) : 1;
	if ((real)ink_w * eff_scale > avail_w)
		eff_scale = avail_w / (real)ink_w;

	if (justification == _ff_text_center)
		draw_x = x0 + (avail_w - (real)ink_w * eff_scale) * 0.5f;
	else if (justification == _ff_text_right)
		draw_x = x1 - (real)ink_w * eff_scale;
	else
		draw_x = x0;

	ox = vx0 + draw_x * scale;
	oy = vy0 + y * scale;
	final_scale = scale * eff_scale;

	bounds.x0 = (short)(ox - (ink.x0 > 0 ? ink.x0 : 0));
	bounds.y0 = (short)oy;
	bounds.x1 = (short)(bounds.x0 + ink_w + 64);
	bounds.y1 = (short)(oy + line_height * 2);

	rasterizer_text_set_scale(final_scale, ox, oy);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, wide);
	rasterizer_text_set_scale(1.0f, 0.0f, 0.0f);
}

static void format_elapsed_time(long elapsed_ticks, char *out_buf, size_t buf_size)
{
	long total_seconds = MAX(0L, elapsed_ticks / TICKS_PER_SECOND);
	long hours = total_seconds / 3600L;
	long minutes = (total_seconds / 60L) % 60L;
	long seconds = total_seconds % 60L;

	if (hours > 0)
		snprintf(out_buf, buf_size, "%ld:%02ld:%02ld", hours, minutes, seconds);
	else
		snprintf(out_buf, buf_size, "%02ld:%02ld", minutes, seconds);
}

/* ---------- 3D -> 2D Navpoints (Stragglers & Resupply Drops) */

static void draw_world_navpoint(
	long font_index,
	short line_height,
	real scale,
	real_point3d const *world_pos,
	char const *label,
	pixel32 box_color,
	real_argb_color const *text_color,
	boolean is_friendly_marker)
{
	real_point3d view_pt;
	real_point2d screen_pt;
	real dist;
	char full_label[48];
	rectangle2d marker;
	short pill_x0, pill_y0, pill_x1, pill_y1;
	boolean drew_friendly_bitmap = FALSE;

	matrix4x3_transform_point(&render.frustum.world_to_view, world_pos, &view_pt);
	if (!render_camera_view_to_screen(&render.camera, &render.frustum, &view_pt, &screen_pt))
	{
		/* When ordnance is off-screen, render a green off-screen directional arrow */
		if (is_friendly_marker && render.local_player_index != NONE && hud_globals && hud_globals->waypoint.arrows.count > 0)
		{
			short arrow_index = find_nav_point("default");
			if (arrow_index == NONE)
				arrow_index = 0;
			if (arrow_index >= 0 && arrow_index < hud_globals->waypoint.arrows.count)
			{
				struct ff_hud_waypoint_arrow
				{
					char name[0x20];
					long unused0[2];
					unsigned long color;
					real opacity;
					real fade;
					short sequence_indices[3];
					short pad;
					long unused1[4];
					unsigned long flags;
					long unused2[6];
				};
				struct ff_hud_waypoint_arrow *arrow = TAG_BLOCK_GET_ELEMENT(
					&hud_globals->waypoint.arrows,
					arrow_index,
					struct ff_hud_waypoint_arrow);
				unsigned long saved_color = arrow->color;
				arrow->color = 0x0040E070;
				custom_render_nav_point(
					render.local_player_index,
					world_pos,
					arrow_index,
					1 /* _waypoint_off_screen */);
				arrow->color = saved_color;
			}
		}
		return;
	}

	dist = distance3d(&render.camera.position, world_pos);
	snprintf(full_label, sizeof(full_label), "%s [%dm]", label, (int)(dist * 3.0f + 0.5f));

	pill_x0 = (short)(screen_pt.x - 48.0f * scale + 0.5f);
	pill_y0 = (short)(screen_pt.y - 21.0f * scale + 0.5f);
	pill_x1 = (short)(screen_pt.x + 48.0f * scale + 0.5f);
	pill_y1 = (short)(screen_pt.y - 8.0f * scale + 0.5f);
	firefight_draw_ce_rounded_pill(pill_x0, pill_y0, pill_x1, pill_y1, 0, scale, 0xD8FFFFFFUL);

	if (is_friendly_marker)
	{
		long mp_hud_tag_index = interface_get_tag_index(_interface_bitmap_multiplayer_hud);
		if (mp_hud_tag_index != NONE)
		{
			struct bitmap_data *bitmap = bitmap_group_get_bitmap_from_sequence(
				mp_hud_tag_index,
				0,
				0);
			if (bitmap && _texture_cache_bitmap_get_hardware_format(bitmap, FALSE, TRUE))
			{
				point2d position;
				real_rectangle2d clip;
				real depth_factor;
				real fade;

				position.x = (short)fast_ftol(screen_pt.x) - render.camera.viewport_bounds.x0;
				position.y = (short)fast_ftol(screen_pt.y) - render.camera.viewport_bounds.y0;
				depth_factor = 1.0f -
					((-view_pt.z - render.frustum.z_near) * 100.0f) /
					(render.frustum.z_far - render.frustum.z_near);
				fade = PIN(depth_factor, 0.25f, 1.0f);
				clip.x0 = 0.0f;
				clip.x1 = 1.0f;
				clip.y0 = (1.0f - fade) * 0.5f;
				clip.y1 = 1.0f;
				interface_draw_bitmap(
					bitmap,
					&position,
					&clip,
					1.0f,
					0.0f,
					1.0f);
				drew_friendly_bitmap = TRUE;
			}
		}
	}

	if (!drew_friendly_bitmap)
	{
		/* Draw a downward chevron/diamond marker instead of a flat square */
		short cx = (short)(screen_pt.x + 0.5f);
		short cy = (short)(screen_pt.y + 1.0f * scale + 0.5f);
		short row;
		for (row = 0; row < 5; row++)
		{
			short half_w = (short)((4 - row) * scale + 0.5f);
			marker.x0 = cx - half_w;
			marker.x1 = cx + half_w + 1;
			marker.y0 = cy + (short)(row * scale);
			marker.y1 = marker.y0 + (short)(scale + 0.5f);
			if (marker.y1 <= marker.y0)
				marker.y1 = marker.y0 + 1;
			draw_quad(&marker, box_color);
		}
	}

	ff_draw_text(
		font_index,
		line_height,
		screen_pt.x - 46.0f * scale,
		screen_pt.y - 19.5f * scale,
		scale,
		full_label,
		0.0f,
		92.0f,
		0.0f,
		0.48f,
		_ff_text_center,
		text_color);
}

/* ---------- Generator Defense HUD Bars */

static void draw_generator_bars(
	long font_index,
	short line_height,
	real vx0,
	real vy0,
	real scale)
{
	static char const *const gen_names[3] = { "ALPHA GEN", "BETA GEN", "GAMMA GEN" };
	real base_x = 14.0f;
	real base_y = 106.0f;
	real card_w = 136.0f;
	real card_h = 24.0f;
	short g;

	if (ff_state.generator_count <= 0)
		return;

	for (g = 0; g < ff_state.generator_count && g < FIREFIGHT_MAX_GENERATORS; g++)
	{
		struct firefight_generator_state const *gen = &ff_state.generators[g];
		real y = base_y + g * (card_h + 4.0f);
		real_argb_color white = { 1.0f, 0.95f, 0.95f, 0.95f };
		real_argb_color red = { 1.0f, 1.0f, 0.35f, 0.35f };
		real_argb_color gold = { 1.0f, 1.0f, 0.85f, 0.25f };
		boolean destroyed = !gen->alive;
		real health_frac = (gen->max_health > 0) ? ((real)gen->health / (real)gen->max_health) : 0.0f;
		real shield_frac = (gen->max_shield > 0) ? ((real)gen->shield / (real)gen->max_shield) : 0.0f;
		char status_str[32];

		ff_draw_box(
			vx0, vy0, scale,
			base_x, y, base_x + card_w, y + card_h,
			0.0f,
			destroyed ? FF_HUD_WARN_FILL : FF_HUD_PANEL_FILL,
			destroyed ? FF_HUD_WARN_EDGE : FF_HUD_PANEL_EDGE);

		ff_draw_text(
			font_index, line_height, vx0, vy0, scale,
			gen_names[g],
			base_x + 6.0f, base_x + 76.0f, y + 3.0f,
			0.51f, _ff_text_left, &white);

		if (destroyed)
		{
			ff_draw_text(
				font_index, line_height, vx0, vy0, scale,
				"DESTROYED",
				base_x + 78.0f, base_x + card_w - 6.0f, y + 3.0f,
				0.51f, _ff_text_right, &red);
		}
		else if (gen->lockdown_ticks > 0)
		{
			snprintf(status_str, sizeof(status_str), "LOCK %ds",
				(int)((gen->lockdown_ticks + TICKS_PER_SECOND - 1) / TICKS_PER_SECOND));
			ff_draw_text(
				font_index, line_height, vx0, vy0, scale,
				status_str,
				base_x + 78.0f, base_x + card_w - 6.0f, y + 3.0f,
				0.51f, _ff_text_right, &gold);
		}
		else
		{
			snprintf(status_str, sizeof(status_str), "%d%%",
				(int)(health_frac * 100.0f + 0.5f));
			ff_draw_text(
				font_index, line_height, vx0, vy0, scale,
				status_str,
				base_x + 78.0f, base_x + card_w - 6.0f, y + 3.0f,
				0.51f, _ff_text_right, &white);
		}

		/* Shield & Health bar strips */
		if (!destroyed)
		{
			real bar_x0 = base_x + 6.0f;
			real bar_w = card_w - 12.0f;
			rectangle2d bar_rect;

			/* Background */
			bar_rect.x0 = (short)(vx0 + bar_x0 * scale);
			bar_rect.y0 = (short)(vy0 + (y + 14.0f) * scale);
			bar_rect.x1 = (short)(vx0 + (bar_x0 + bar_w) * scale);
			bar_rect.y1 = (short)(vy0 + (y + 20.0f) * scale);
			draw_quad(&bar_rect, FF_HUD_BAR_BG);

			/* Health fill */
			bar_rect.x1 = (short)(vx0 + (bar_x0 + bar_w * PIN(health_frac, 0.0f, 1.0f)) * scale);
			bar_rect.y1 = (short)(vy0 + (y + 17.0f) * scale);
			draw_quad(&bar_rect, gen->lockdown_ticks > 0 ? FF_HUD_LOCK_BAR : FF_HUD_HEALTH_BAR);

			/* Shield fill */
			bar_rect.y0 = (short)(vy0 + (y + 17.0f) * scale);
			bar_rect.y1 = (short)(vy0 + (y + 20.0f) * scale);
			bar_rect.x1 = (short)(vx0 + (bar_x0 + bar_w * PIN(shield_frac, 0.0f, 1.0f)) * scale);
			draw_quad(&bar_rect, FF_HUD_SHIELD_BAR);

			/* 3D World-to-Screen Navpoint over active generator */
			{
				real_point3d gen_nav = gen->position;
				gen_nav.z += 1.25f;
				draw_world_navpoint(
					font_index,
					line_height,
					scale,
					&gen_nav,
					gen_names[g],
					gen->under_attack_ticks > 0 ? FF_HUD_WARN_EDGE : FF_HUD_PANEL_EDGE,
					gen->under_attack_ticks > 0 ? &red : (gen->lockdown_ticks > 0 ? &gold : &white),
					FALSE);
			}
		}
	}
}

struct ff_skull_hud_def
{
	char const *name;
	char const *code;
	char const *desc;
	pixel32 badge_color;
};

static struct ff_skull_hud_def const s_ff_skull_hud_defs[NUMBER_OF_FIREFIGHT_SKULLS] =
{
	{ "IRON",           "IRN", "TEAM WIPE ENDS THE SET",                 0xFFE83A3A },
	{ "BLACK EYE",      "BLK", "SHIELDS ONLY RECHARGE ON MELEE",         0xFFB668FF },
	{ "TOUGH LUCK",     "TLK", "ENEMIES DODGE GRENADES & ROCKETS",       0xFFF09828 },
	{ "CATCH",          "CAT", "ENEMIES THROW MORE GRENADES",            0xFF38C8F0 },
	{ "CLOUD",          "FOG", "MOTION TRACKER DISABLED",                0xFF90A8C0 },
	{ "FAMINE",         "FAM", "DROPPED WEAPONS HAVE LESS AMMO",         0xFFD4A838 },
	{ "TILT",           "TLT", "ENEMY SHIELD & ARMOR RESISTANCE UP",     0xFF4288F5 },
	{ "MYTHIC",         "MYT", "ENEMIES HAVE 2X HEALTH & SHIELDS",       0xFFFF5038 },
	{ "THUNDERSTORM",   "THN", "ENEMIES PROMOTED TO HIGHER RANKS",       0xFFFFD038 },
	{ "BLIND",          "BLD", "HUD & FIRST-PERSON WEAPON HIDDEN",       0xFF808898 },
	{ "BOOM",           "BOM", "2X EXPLOSION RADIUS & DAMAGE",           0xFFFF6E28 },
	{ "COWBELL",        "CWB", "ACCELERATED EXPLOSIVE KNOCKBACK",        0xFF50E088 },
	{ "GRUNT BIRTHDAY", "GBP", "HEADSHOTS DETONATE IN CONFETTI",         0xFFF570C0 }
};

/* the skulls of a mask of enum firefight_skull_bit, by their bits */
static short ff_skull_bits_of(
	unsigned long skulls,
	short *bits,
	short maximum)
{
	short count = 0;
	short bit;

	for (bit = 0; bit < NUMBER_OF_FIREFIGHT_SKULLS && count < maximum; bit++)
	{
		if ((skulls & (1UL << bit)) != 0)
			bits[count++] = bit;
	}
	return count;
}

enum
{
	FF_BANNER_SKULL_INSET = 8,
	FF_BANNER_SKULL_GAP = 3
};

/* how wide the wells for `count` skulls of `size` are */
static real ff_banner_well_width(
	short count,
	real size)
{
	return FF_BANNER_SKULL_INSET + ((count + 1) / 2) * (size + FF_BANNER_SKULL_GAP) + 3.0f;
}

/* the skulls' own icons in the banner's wells: one on each side for one
skull, or shared out between them */
static void ff_draw_banner_skulls(
	real vx0,
	real vy0,
	real scale,
	real x0,
	real x1,
	real y,
	real size,
	short const *bits,
	short count)
{
	short left_count = (short)((count + 1) / 2);
	short right_count = (short)(count == 1 ? 1 : count - left_count);
	real gap = FF_BANNER_SKULL_GAP;
	real inset = FF_BANNER_SKULL_INSET;
	short index;

	for (index = 0; index < left_count + right_count; index++)
	{
		short bit = bits[count == 1 ? 0 : index];
		short slot = (short)(index < left_count ? index : index - left_count);
		real ix = index < left_count ?
			x0 + inset + slot * (size + gap) :
			x1 - inset - size - (right_count - 1 - slot) * (size + gap);

		skulls_draw_icon(firefight_map_reach_skull_to_40_skull((enum firefight_skull_bit)bit),
			(short)(vx0 + ix * scale + 0.5f), (short)(vy0 + y * scale + 0.5f),
			(short)(vx0 + (ix + size) * scale + 0.5f), (short)(vy0 + (y + size) * scale + 0.5f), 1.0f);
	}
}

static void format_banner_strings(
	short banner_type,
	short banner_data,
	char *title_out,
	size_t title_size,
	char *sub_out,
	size_t sub_size)
{
	title_out[0] = '\0';
	sub_out[0] = '\0';

	switch (banner_type)
	{
	case _ff_banner_set_start:
		snprintf(title_out, title_size, "SET %d START", (int)banner_data);
		snprintf(sub_out, sub_size, "PREPARE FOR COMBAT");
		break;
	case _ff_banner_round_start:
		snprintf(title_out, title_size, "ROUND %d", (int)banner_data);
		snprintf(sub_out, sub_size, "HOLD YOUR GROUND");
		break;
	case _ff_banner_wave_start:
		snprintf(title_out, title_size, "WAVE %d", (int)banner_data);
		snprintf(sub_out, sub_size, "HOSTILES INBOUND");
		break;
	case _ff_banner_boss_wave:
		snprintf(title_out, title_size, "BOSS WAVE %d", (int)banner_data);
		snprintf(sub_out, sub_size, "ELIMINATE HIGH-VALUE TARGETS");
		break;
	case _ff_banner_reinforcements:
		snprintf(title_out, title_size, "REINFORCEMENTS");
		snprintf(sub_out, sub_size, "ENEMY SQUADS DEPLOYING");
		break;
	case _ff_banner_bonus_round:
		snprintf(title_out, title_size, "BONUS ROUND");
		snprintf(sub_out, sub_size, "60s GRUNT BIRTHDAY PARTY — EXTRA LIVES!");
		break;
	case _ff_banner_set_complete:
		snprintf(title_out, title_size, "SET %d COMPLETE", (int)banner_data);
		snprintf(sub_out, sub_size, "BONUS LIVES AWARDED");
		break;
	case _ff_banner_skull_activated:
		if (banner_data >= 0 && banner_data < NUMBER_OF_FIREFIGHT_SKULLS)
		{
			snprintf(title_out, title_size, "SKULL ACTIVE: %s", s_ff_skull_hud_defs[banner_data].name);
			snprintf(sub_out, sub_size, "%s", s_ff_skull_hud_defs[banner_data].desc);
		}
		else
		{
			snprintf(title_out, title_size, "SKULL ACTIVATED");
			snprintf(sub_out, sub_size, "ENEMY MODIFIER ENGAGED");
		}
		break;
	case _ff_banner_last_man_standing:
		snprintf(title_out, title_size, "LAST MAN STANDING");
		snprintf(sub_out, sub_size, "SURVIVE TO KEEP THE TEAM ALIVE");
		break;
	case _ff_banner_generator_under_attack:
		snprintf(title_out, title_size, "GENERATOR %c UNDER ATTACK", 'A' + PIN(banner_data, 0, 2));
		snprintf(sub_out, sub_size, "DEFEND THE GENERATOR");
		break;
	case _ff_banner_generator_locked_down:
		snprintf(title_out, title_size, "GENERATOR %c LOCKED DOWN", 'A' + PIN(banner_data, 0, 2));
		snprintf(sub_out, sub_size, "BLAST SHIELDS ACTIVE FOR 30s");
		break;
	case _ff_banner_generator_destroyed:
		snprintf(title_out, title_size, "GENERATOR %c DESTROYED", 'A' + PIN(banner_data, 0, 2));
		snprintf(sub_out, sub_size, "DEFENSIVE PERIMETER BREACHED");
		break;
	case _ff_banner_ordnance_drop:
		snprintf(title_out, title_size, "WEAPON & AMMO DROP INBOUND");
		snprintf(sub_out, sub_size, "RESUPPLY CACHE DEPLOYED AT BASE");
		break;
	case _ff_banner_heavy_ordnance_drop:
		snprintf(title_out, title_size, "HEAVY ANTI-ARMOR DROP");
		snprintf(sub_out, sub_size, "ROCKETS, SNIPER & OVERSHIELD DEPLOYED");
		break;
	case _ff_banner_dropship_inbound:
		snprintf(title_out, title_size, "DROPSHIP INBOUND");
		snprintf(sub_out, sub_size, "PHANTOM / PELICAN INSERTION DETECTED");
		break;
	case _ff_banner_vehicle_deployed:
		snprintf(title_out, title_size, "VEHICLE DEPLOYED");
		snprintf(sub_out, sub_size, "ARMORED SUPPORT AT PLAYER BASE");
		break;
	case _ff_banner_bonus_life:
		snprintf(title_out, title_size, "+%d BONUS LIFE", (int)MAX(1, banner_data));
		snprintf(sub_out, sub_size, "TEAM REINFORCEMENT EARNED");
		break;
	case _ff_banner_victory:
		snprintf(title_out, title_size, "FIREFIGHT VICTORY");
		snprintf(sub_out, sub_size, "ALL SETS CLEARED");
		break;
	case _ff_banner_defeat:
		snprintf(title_out, title_size, "FIREFIGHT OVER");
		snprintf(sub_out, sub_size, "TEAM ELIMINATED");
		break;
	default:
		break;
	}
}

/* ---------- Main Firefight HUD Draw */

void firefight_hud_draw(void)
{
	long font_index;
	struct font_header *font;
	short line_height;
	real vx0, vy0, vw, vh, scale;
	real_argb_color white = { 1.0f, 0.96f, 0.98f, 1.0f };
	real_argb_color cyan = { 1.0f, 0.42f, 0.78f, 1.0f };
	real_argb_color gold = { 1.0f, 0.96f, 0.98f, 1.0f };
	real_argb_color red = { 1.0f, 1.0f, 0.35f, 0.35f };
	real_argb_color green = { 1.0f, 0.35f, 1.0f, 0.45f };
	boolean prep_phase;

	if (!firefight_active())
		return;

	font_index = hud_get_font_index();
	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	if (!font)
		return;
	line_height = (short)(font->ascending_height + font->descending_height);
	if (line_height <= 0)
		return;

	vx0 = (real)render.camera.viewport_bounds.x0;
	vy0 = (real)render.camera.viewport_bounds.y0;
	vw = (real)(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0);
	vh = (real)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0);
	scale = vh / 480.0f;
	if (scale <= 0.0f)
		return;

	prep_phase = (ff_state.phase_state == _ff_phase_pre_set || ff_state.phase_state == _ff_phase_pre_round);

	/* (the set, the round, the wave, the lives, the hostiles and the time are
	the score panel's: campaign_score.c) */

	/* 2. Pre-Round Preparation Countdown Banner */
	if (prep_phase && ff_state.phase_timer_ticks > 0)
	{
		real bw = 280.0f;
		real bh = 38.0f;
		real bx = (vw / scale - bw) * 0.5f;
		real by = 52.0f;
		long secs = (ff_state.phase_timer_ticks + TICKS_PER_SECOND - 1) / TICKS_PER_SECOND;
		char prep_title[64];
		short round_bits[NUMBER_OF_FIREFIGHT_SKULLS];
		short round_count = ff_skull_bits_of(ff_state.round_skulls, round_bits, NUMBER_OF_FIREFIGHT_SKULLS);

		ff_draw_box(vx0, vy0, scale, bx, by, bx + bw, by + bh, 19.0f, FF_HUD_PANEL_FILL, FF_HUD_LOCK_BAR);
		snprintf(prep_title, sizeof(prep_title), "ROUND %d STARTS IN %lds",
			(int)ff_state.current_round, MAX(1L, secs));
		ff_draw_text(font_index, line_height, vx0, vy0, scale,
			prep_title, bx + 8.0f, bx + bw - 8.0f, by + 4.0f,
			0.68f, _ff_text_center, &gold);
		ff_draw_text(font_index, line_height, vx0, vy0, scale,
			"RESUPPLY & BOARD VEHICLES", bx + 8.0f, bx + bw - 8.0f, by + 23.0f,
			0.51f, _ff_text_center, &cyan);

		if (round_count > 0)
		{
			char sk_title[96];
			char const *sk_desc = round_count == 1 ? s_ff_skull_hud_defs[round_bits[0]].desc : "ENEMY MODIFIERS ENGAGED";
			real sby = by + bh + 4.0f;
			real inset = ff_banner_well_width(round_count, 18.0f);
			short name;

			ff_draw_box_with_wells(vx0, vy0, scale, bx, sby, bx + bw, sby + 34.0f, 17.0f, inset, FF_HUD_WARN_FILL,
				s_ff_skull_hud_defs[round_bits[0]].badge_color);
			ff_draw_banner_skulls(vx0, vy0, scale, bx, bx + bw, sby + 8.0f, 18.0f, round_bits, round_count);
			snprintf(sk_title, sizeof(sk_title), "%s: ", round_count == 1 ? "SKULL ACTIVE" : "SKULLS ACTIVE");
			for (name = 0; name < round_count; name++)
			{
				if (name > 0)
					strncat(sk_title, " + ", sizeof(sk_title) - strlen(sk_title) - 1);
				strncat(sk_title, s_ff_skull_hud_defs[round_bits[name]].name, sizeof(sk_title) - strlen(sk_title) - 1);
			}
			ff_draw_text(font_index, line_height, vx0, vy0, scale,
				sk_title, bx + inset, bx + bw - inset, sby + 2.5f,
				0.58f, _ff_text_center, &gold);
			ff_draw_text(font_index, line_height, vx0, vy0, scale,
				sk_desc, bx + inset, bx + bw - inset, sby + 20.0f,
				0.48f, _ff_text_center, &white);
		}
	}
	/* 3. Wave / Boss / Skull / Ordnance Announcement Banner */
	else if (ff_state.banner_timer_ticks > 0 && ff_state.banner_type != _ff_banner_none)
	{
		char banner_title[64];
		char banner_sub[64];
		real bw = 280.0f;
		real bh = 38.0f;
		real bx = (vw / scale - bw) * 0.5f;
		real by = 52.0f;

		format_banner_strings(
			ff_state.banner_type,
			ff_state.banner_data,
			banner_title, sizeof(banner_title),
			banner_sub, sizeof(banner_sub));

		if (banner_title[0] != '\0')
		{
			boolean is_skull_b = (ff_state.banner_type == _ff_banner_skull_activated);
			pixel32 edge_c = is_skull_b && ff_state.banner_data >= 0 && ff_state.banner_data < NUMBER_OF_FIREFIGHT_SKULLS
				? s_ff_skull_hud_defs[ff_state.banner_data].badge_color
				: FF_HUD_PANEL_EDGE;

			ff_draw_box_with_wells(vx0, vy0, scale, bx, by, bx + bw, by + bh, 19.0f,
				is_skull_b ? ff_banner_well_width(1, 18.0f) : 0.0f,
				is_skull_b ? FF_HUD_WARN_FILL : FF_HUD_PANEL_FILL, edge_c);
			if (is_skull_b && ff_state.banner_data >= 0 && ff_state.banner_data < NUMBER_OF_FIREFIGHT_SKULLS)
			{
				short banner_bit = ff_state.banner_data;

				ff_draw_banner_skulls(vx0, vy0, scale, bx, bx + bw, by + 10.0f, 18.0f, &banner_bit, 1);
			}
			ff_draw_text(font_index, line_height, vx0, vy0, scale,
				banner_title, bx + 8.0f, bx + bw - 8.0f, by + 4.0f,
				0.68f, _ff_text_center, &gold);
			if (banner_sub[0] != '\0')
			{
				ff_draw_text(font_index, line_height, vx0, vy0, scale,
					banner_sub, bx + 8.0f, bx + bw - 8.0f, by + 23.0f,
					0.51f, _ff_text_center, &cyan);
			}
		}
	}

	/* 4. Generator Defense Status Bars */
	draw_generator_bars(font_index, line_height, vx0, vy0, scale);

	/* 5. 3D->2D World-to-Screen Navpoints for Stragglers (<= 3 enemies left) & Resupply Drops */
	if (ff_state.straggler_navpoints_active && ff_state.enemies_alive > 0 && ff_state.enemies_alive <= FIREFIGHT_STRAGGLER_THRESHOLD)
	{
		short local_player_index = render.local_player_index;
		short red_arrow_index = find_nav_point("target_red");
		short i;

		if (red_arrow_index == NONE)
			red_arrow_index = find_nav_point("default_red");
		if (red_arrow_index == NONE && hud_globals && hud_globals->waypoint.arrows.count > 0)
			red_arrow_index = (hud_globals->waypoint.arrows.count > 1) ? 1 : 0;

		for (i = 0; i < FIREFIGHT_MAX_TRACKED_ENEMIES; i++)
		{
			long unit_index = ff_state.tracked_enemy_units[i];
			struct unit_datum *unit;
			real_point3d nav_pos;

			if (unit_index == NONE)
				continue;
			unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);
			if (!unit || unit->object.body_vitality <= 0.0f || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
				continue;

			nav_pos = unit->object.bounding_sphere_center;
			render_interpolation_object_point(unit_index, &nav_pos);
			nav_pos.z += unit->object.bounding_sphere_radius * 0.65f + 0.15f;

			if (local_player_index != NONE && red_arrow_index != NONE)
			{
				short waypoint_type = hud_get_nav_point_render_type(
					local_player_index,
					&render.camera.position,
					&nav_pos,
					unit_index);
				custom_render_nav_point(
					local_player_index,
					&nav_pos,
					red_arrow_index,
					waypoint_type);
			}
			else
			{
				draw_world_navpoint(
					font_index, line_height, scale,
					&nav_pos, "HOSTILE", FF_HUD_WARN_EDGE, &red, FALSE);
			}
		}
	}

	if (ff_state.ordnance_navpoint_ticks > 0)
	{
		real_point3d ord_pos = ff_state.last_ordnance_position;
		ord_pos.z += 0.4f;
		draw_world_navpoint(
			font_index, line_height, scale,
			&ord_pos,
			ff_state.last_ordnance_is_heavy ? "HEAVY ORDNANCE" : "ORDNANCE",
			0xE040E070, &green, TRUE);
	}
}

void firefight_hud_post_rasterize(void)
{
}

void firefight_format_scoreboard_title(
	wchar_t *title_string,
	long count,
	boolean postgame)
{
	char narrow[160];
	char time_str[24];
	long i;

	(void)postgame;
	if (!title_string || count <= 0)
		return;

	format_elapsed_time(ff_state.session_elapsed_ticks, time_str, sizeof(time_str));
	snprintf(
		narrow,
		sizeof(narrow),
		"FIREFIGHT | SET %d RND %d WAVE %d | LIVES: %d | TIME: %s | SCORE: %ld",
		(int)ff_state.current_set,
		(int)ff_state.current_round,
		(int)ff_state.current_wave,
		(int)ff_state.shared_lives_remaining,
		time_str,
		campaign_score_team_points());

	for (i = 0; i < count - 1 && narrow[i]; i++)
		title_string[i] = (wchar_t)(unsigned char)narrow[i];
	title_string[i] = 0;
}

boolean firefight_hud_format_scoreboard_title(
	wchar_t *title_string,
	long count)
{
	if (!firefight_active() || !title_string || count <= 0)
		return FALSE;

	firefight_format_scoreboard_title(title_string, count, FALSE);
	return TRUE;
}

/* ---------- MCC Rank Icons & Customizable Emblem Renderer ---------- */

static pixel32 ff_apply_alpha(pixel32 argb, real alpha)
{
	unsigned long a = (unsigned long)((argb >> 24) & 0xFF);
	a = (unsigned long)PIN((long)((real)a * PIN(alpha, 0.0f, 1.0f) + 0.5f), 0L, 255L);
	return (pixel32)((argb & 0x00FFFFFFUL) | (a << 24));
}

#define FF_ICON_HI_DIM 128
#define FF_ICON_HI_MIPS 4
#define FF_ICON_GRID_DIM 16
#define FF_ICON_MAX_RECTS 128

/* the icons' working pictures (a 128x128 icon and its smaller mips): one icon
is made at a time, so one set, on the heap when first needed rather than in the
game's image */
struct ff_icon_scratch
{
	pixel32 pixels128[FF_ICON_HI_DIM * FF_ICON_HI_DIM];
	pixel32 mip64[64 * 64];
	pixel32 mip32[32 * 32];
	pixel32 mip16[16 * 16];
	unsigned char emblem_mask[FIREFIGHT_EMBLEM_MASK_DIM * FIREFIGHT_EMBLEM_MASK_DIM];
};

static struct ff_icon_scratch *ff_icon_scratch(void)
{
	static struct ff_icon_scratch *scratch;

	if (!scratch)
		scratch = malloc(sizeof(*scratch));
	return scratch;
}

struct ff_hw_icon_entry
{
	boolean valid;
	unsigned long key;
	struct bitmap_data bitmap;
};

static struct ff_hw_icon_entry s_hw_icon_cache[64];
static int s_hw_icon_next = 0;

static void ff_downsample_2x(pixel32 const *src, int src_dim, pixel32 *dst)
{
	int dst_dim = src_dim >> 1;
	int y, x;

	for (y = 0; y < dst_dim; y++)
	{
		for (x = 0; x < dst_dim; x++)
		{
			pixel32 c00 = src[(y * 2) * src_dim + (x * 2)];
			pixel32 c01 = src[(y * 2) * src_dim + (x * 2 + 1)];
			pixel32 c10 = src[(y * 2 + 1) * src_dim + (x * 2)];
			pixel32 c11 = src[(y * 2 + 1) * src_dim + (x * 2 + 1)];
			unsigned long a00 = (c00 >> 24) & 0xFF;
			unsigned long a01 = (c01 >> 24) & 0xFF;
			unsigned long a10 = (c10 >> 24) & 0xFF;
			unsigned long a11 = (c11 >> 24) & 0xFF;
			unsigned long a_sum = a00 + a01 + a10 + a11;

			if (a_sum == 0)
			{
				dst[y * dst_dim + x] = 0;
			}
			else
			{
				unsigned long r = (((c00 >> 16) & 0xFF) * a00 + ((c01 >> 16) & 0xFF) * a01 +
					((c10 >> 16) & 0xFF) * a10 + ((c11 >> 16) & 0xFF) * a11 + (a_sum >> 1)) / a_sum;
				unsigned long g = (((c00 >> 8) & 0xFF) * a00 + ((c01 >> 8) & 0xFF) * a01 +
					((c10 >> 8) & 0xFF) * a10 + ((c11 >> 8) & 0xFF) * a11 + (a_sum >> 1)) / a_sum;
				unsigned long b = ((c00 & 0xFF) * a00 + (c01 & 0xFF) * a01 +
					(c10 & 0xFF) * a10 + (c11 & 0xFF) * a11 + (a_sum >> 1)) / a_sum;
				unsigned long a = (a_sum + 2) >> 2;
				if (r > 255) r = 255;
				if (g > 255) g = 255;
				if (b > 255) b = 255;
				if (a > 255) a = 255;
				dst[y * dst_dim + x] = (pixel32)((a << 24) | (r << 16) | (g << 8) | b);
			}
		}
	}
}

static boolean ff_draw_hw_icon_pixels(
	unsigned long key,
	pixel32 const *pixels_128x128,
	short x0,
	short y0,
	short size,
	real alpha)
{
	struct ff_hw_icon_entry *entry = NULL;
	int i;

	if (!global_d3d_device)
		return FALSE;

	for (i = 0; i < 64; i++)
	{
		if (s_hw_icon_cache[i].valid &&
			s_hw_icon_cache[i].key == key &&
			s_hw_icon_cache[i].bitmap.hardware_format != NULL)
		{
			entry = &s_hw_icon_cache[i];
			break;
		}
	}

	if (!entry && pixels_128x128)
	{
		struct ff_icon_scratch *scratch = ff_icon_scratch();
		pixel32 *s_mip64 = scratch->mip64;
		pixel32 *s_mip32 = scratch->mip32;
		pixel32 *s_mip16 = scratch->mip16;
		pixel32 const *mip_ptrs[FF_ICON_HI_MIPS];
		short mip_dims[FF_ICON_HI_MIPS] = { 128, 64, 32, 16 };
		short mip;

		entry = &s_hw_icon_cache[s_hw_icon_next];
		s_hw_icon_next = (s_hw_icon_next + 1) & 63;

		if (!entry->bitmap.hardware_format)
		{
			csmemset(&entry->bitmap, 0, sizeof(entry->bitmap));
			entry->bitmap.signature = 'bitm';
			entry->bitmap.width = FF_ICON_HI_DIM;
			entry->bitmap.height = FF_ICON_HI_DIM;
			entry->bitmap.depth = 1;
			entry->bitmap.type = 0; /* _bitmap_type_2d */
			entry->bitmap.format = 11; /* _bitmap_format_a8r8g8b8 */
			entry->bitmap.flags = 1; /* _bitmap_has_power_of_two_dimensions_bit */
			entry->bitmap.mipmap_count = FF_ICON_HI_MIPS - 1;
			entry->bitmap.pixels_offset = 0;
			entry->bitmap.pixels_size = FF_ICON_HI_DIM * FF_ICON_HI_DIM * 4;
			entry->bitmap.tag_index = NONE;
			entry->bitmap.cache_block_index = NONE;
			if (IDirect3DDevice8_CreateTexture(
					global_d3d_device,
					FF_ICON_HI_DIM,
					FF_ICON_HI_DIM,
					FF_ICON_HI_MIPS,
					0,
					D3DFMT_A8R8G8B8,
					D3DPOOL_MANAGED,
					(IDirect3DTexture8 **)&entry->bitmap.hardware_format) < 0)
			{
				entry->bitmap.hardware_format = NULL;
				return FALSE;
			}
		}

		ff_downsample_2x(pixels_128x128, 128, s_mip64);
		ff_downsample_2x(s_mip64, 64, s_mip32);
		ff_downsample_2x(s_mip32, 32, s_mip16);
		mip_ptrs[0] = pixels_128x128;
		mip_ptrs[1] = s_mip64;
		mip_ptrs[2] = s_mip32;
		mip_ptrs[3] = s_mip16;

		for (mip = 0; mip < FF_ICON_HI_MIPS; mip++)
		{
			D3DLOCKED_RECT locked_rect;
			csmemset(&locked_rect, 0, sizeof(locked_rect));
			if (IDirect3DTexture8_LockRect(
					(IDirect3DTexture8 *)entry->bitmap.hardware_format,
					(UINT)mip,
					&locked_rect,
					NULL,
					D3DLOCK_NOOVERWRITE) >= 0 && locked_rect.pBits)
			{
				rasterizer_xbox_bitmap_swizzle2d_long(
					locked_rect.pBits,
					mip_ptrs[mip],
					mip_dims[mip],
					mip_dims[mip]);
				IDirect3DTexture8_UnlockRect(
					(IDirect3DTexture8 *)entry->bitmap.hardware_format,
					(UINT)mip);
			}
			else
			{
				return FALSE;
			}
		}

		entry->valid = TRUE;
		entry->key = key;
	}

	if (entry && entry->bitmap.hardware_format)
	{
		rectangle2d rect;
		short dim = MAX(8, size);
		pixel32 argb = ff_apply_alpha(0xFFFFFFFFUL, PIN(alpha, 0.0f, 1.0f));
		rect.x0 = x0;
		rect.y0 = y0;
		rect.x1 = (short)(x0 + dim);
		rect.y1 = (short)(y0 + dim);
		draw_bitmap_in_rect(&entry->bitmap, &rect, NULL, NULL, argb, NULL, TRUE);
		return TRUE;
	}

	return FALSE;
}

struct ff_cached_icon_rect
{
	unsigned char x0, y0, x1, y1;
	pixel32 col;
};

struct ff_cached_icon_mesh
{
	boolean valid;
	unsigned long key;
	short rect_count;
	struct ff_cached_icon_rect rects[FF_ICON_MAX_RECTS];
};

static struct ff_cached_icon_mesh s_rank_mesh_cache[8];
static int s_rank_mesh_next = 0;

static struct ff_cached_icon_mesh s_emblem_mesh_cache[8];
static int s_emblem_mesh_next = 0;

static void ff_build_merged_icon_mesh(
	pixel32 grid[FF_ICON_GRID_DIM][FF_ICON_GRID_DIM],
	struct ff_cached_icon_mesh *out_mesh,
	unsigned long key)
{
	short gy, gx;
	out_mesh->valid = TRUE;
	out_mesh->key = key;
	out_mesh->rect_count = 0;

	for (gy = 0; gy < FF_ICON_GRID_DIM; gy++)
	{
		for (gx = 0; gx < FF_ICON_GRID_DIM; gx++)
		{
			pixel32 c = grid[gy][gx];
			short gx1, gy1, tx, cy;
			if (c == 0)
				continue;

			gx1 = (short)(gx + 1);
			while (gx1 < FF_ICON_GRID_DIM && grid[gy][gx1] == c)
				gx1++;

			gy1 = (short)(gy + 1);
			while (gy1 < FF_ICON_GRID_DIM)
			{
				boolean row_matches = TRUE;
				for (tx = gx; tx < gx1; tx++)
				{
					if (grid[gy1][tx] != c)
					{
						row_matches = FALSE;
						break;
					}
				}
				if (!row_matches)
					break;
				gy1++;
			}

			for (cy = gy; cy < gy1; cy++)
			{
				for (tx = gx; tx < gx1; tx++)
					grid[cy][tx] = 0;
			}

			if (out_mesh->rect_count < FF_ICON_MAX_RECTS)
			{
				struct ff_cached_icon_rect *r = &out_mesh->rects[out_mesh->rect_count++];
				r->x0 = (unsigned char)gx;
				r->y0 = (unsigned char)gy;
				r->x1 = (unsigned char)gx1;
				r->y1 = (unsigned char)gy1;
				r->col = c;
			}
		}
	}
}

static void ff_draw_cached_icon_mesh(
	struct ff_cached_icon_mesh const *mesh,
	short x0,
	short y0,
	short size,
	real alpha)
{
	short w = MAX(8, size);
	short h = MAX(8, size);
	short i;

	for (i = 0; i < mesh->rect_count; i++)
	{
		struct ff_cached_icon_rect const *r = &mesh->rects[i];
		rectangle2d q;
		q.x0 = (short)(x0 + ((int)r->x0 * (int)w + (FF_ICON_GRID_DIM / 2)) / FF_ICON_GRID_DIM);
		q.x1 = (short)(x0 + ((int)r->x1 * (int)w + (FF_ICON_GRID_DIM / 2)) / FF_ICON_GRID_DIM);
		q.y0 = (short)(y0 + ((int)r->y0 * (int)h + (FF_ICON_GRID_DIM / 2)) / FF_ICON_GRID_DIM);
		q.y1 = (short)(y0 + ((int)r->y1 * (int)h + (FF_ICON_GRID_DIM / 2)) / FF_ICON_GRID_DIM);
		if (q.x1 > q.x0 && q.y1 > q.y0)
		{
			pixel32 col = (alpha < 0.999f) ? ff_apply_alpha(r->col, alpha) : r->col;
			draw_quad(&q, col);
		}
	}
}

/* over, its alpha not multiplied in, laid over under */
static pixel32 ff_icon_over(pixel32 under, pixel32 over)
{
	unsigned long src_a = (over >> 24) & 0xFF;
	unsigned long under_weight = ((under >> 24) & 0xFF) * (255 - src_a);
	unsigned long total = src_a * 255 + under_weight;

	if (!src_a)
		return under;
	return (pixel32)(
		(((total + 127) / 255) << 24) |
		(((((over >> 16) & 0xFF) * src_a * 255 + ((under >> 16) & 0xFF) * under_weight + total / 2) / total) << 16) |
		(((((over >> 8) & 0xFF) * src_a * 255 + ((under >> 8) & 0xFF) * under_weight + total / 2) / total) << 8) |
		(((over & 0xFF) * src_a * 255 + (under & 0xFF) * under_weight + total / 2) / total));
}

/* the scratch's pixels128 as a grid of solid and empty cells, for the icons'
quads where no texture can be made */
static void ff_icon_grid(struct ff_icon_scratch const *scratch, pixel32 grid[FF_ICON_GRID_DIM][FF_ICON_GRID_DIM])
{
	int step = FF_ICON_HI_DIM / FF_ICON_GRID_DIM;
	short py, px;

	for (py = 0; py < FF_ICON_GRID_DIM; py++)
	{
		for (px = 0; px < FF_ICON_GRID_DIM; px++)
		{
			pixel32 pixel = scratch->pixels128[(py * step + step / 2) * FF_ICON_HI_DIM + px * step + step / 2];
			grid[py][px] = ((pixel >> 24) & 0xFF) >= 128 ? (pixel | 0xFF000000) : 0;
		}
	}
}

/* an insignia over a tour's shield (NONE: over nothing) in the scratch's pixels128 */
static void ff_rank_compose(
	struct ff_icon_scratch *scratch,
	unsigned char const *pictures,
	short shield,
	short insignia)
{
	unsigned char const *over = pictures + (FIREFIGHT_SERVICE_TOURS + insignia) * (size_t)FF_RANK_ICON_BYTES;
	unsigned char const *under = shield != NONE ? pictures + shield * (size_t)FF_RANK_ICON_BYTES : NULL;
	int i;

	for (i = 0; i < FF_ICON_HI_DIM * FF_ICON_HI_DIM; i++)
		scratch->pixels128[i] = ff_icon_over(under ? (pixel32)ff_read_u32(under + i * 4) : 0,
			(pixel32)ff_read_u32(over + i * 4));
}

static void ff_draw_rank_picture(
	unsigned long key,
	short shield,
	short insignia,
	short x0,
	short y0,
	short size,
	real alpha)
{
	unsigned char const *pictures = ff_rank_icons();
	struct ff_icon_scratch *scratch = ff_icon_scratch();
	struct ff_cached_icon_mesh *mesh = NULL;
	int i;

	if (!pictures)
		return;
	if (global_d3d_device)
	{
		if (ff_draw_hw_icon_pixels(key, NULL, x0, y0, size, alpha))
			return;
		if (scratch)
		{
			ff_rank_compose(scratch, pictures, shield, insignia);
			if (ff_draw_hw_icon_pixels(key, scratch->pixels128, x0, y0, size, alpha))
				return;
		}
	}

	for (i = 0; i < 8; i++)
	{
		if (s_rank_mesh_cache[i].valid && s_rank_mesh_cache[i].key == key)
		{
			mesh = &s_rank_mesh_cache[i];
			break;
		}
	}

	if (!mesh)
	{
		pixel32 grid[FF_ICON_GRID_DIM][FF_ICON_GRID_DIM];

		if (!scratch)
			return;
		ff_rank_compose(scratch, pictures, shield, insignia);
		ff_icon_grid(scratch, grid);
		mesh = &s_rank_mesh_cache[s_rank_mesh_next];
		s_rank_mesh_next = (s_rank_mesh_next + 1) & 7;
		ff_build_merged_icon_mesh(grid, mesh, key);
	}

	ff_draw_cached_icon_mesh(mesh, x0, y0, size, alpha);
}

void firefight_draw_mcc_rank_icon(
	short x0,
	short y0,
	short size,
	short tour_number,
	short rank_in_tour,
	real alpha)
{
	short t_idx = (short)PIN(tour_number - 1, 0, FIREFIGHT_SERVICE_TOURS - 1);
	short r_idx = (short)PIN(rank_in_tour - 1, 0, FIREFIGHT_SERVICE_RANKS - 1);

	/* (a player without one, as social_extensions_get_person_rank_and_emblem says) */
	if (tour_number <= 0)
		return;

	ff_draw_rank_picture(((unsigned long)t_idx << 8) | (unsigned long)r_idx | 0x80000000UL, t_idx, r_idx,
		x0, y0, size, alpha);
}

void firefight_draw_h3_ranked_icon(
	short x0,
	short y0,
	short size,
	short ranked_index,
	real alpha)
{
	short idx = (short)PIN(ranked_index, 0, FIREFIGHT_SERVICE_RANKS - 1);

	ff_draw_rank_picture(0x20000000UL | (unsigned long)idx, NONE, idx, x0, y0, size, alpha);
}

/* the emblems' masks and palette (port/assets/emblems, tools/emblem_assets.py):
a word for each mask, the background's and then each foreground's two, giving
its offset (0: none), then the masks. EMBLEM_PRIMARY_ON_TOP is set in a
foreground's first when that layer goes over its second. */
extern const unsigned int firefight_emblem_masks[];
extern const unsigned int firefight_emblem_palette[];

#define EMBLEM_PRIMARY_ON_TOP 0x80000000u

#if FIREFIGHT_EMBLEM_MASK_DIM != FF_ICON_HI_DIM
#error the emblems' masks are drawn pixel for pixel into the icons' pictures
#endif

static unsigned int ff_emblem_foreground_word(short foreground, short layer)
{
	return firefight_emblem_masks[FIREFIGHT_EMBLEM_BACKGROUND_COUNT + foreground * 2 + layer];
}

/* a mask's runs into out: a control byte below 128 is that many plus one bytes
as they are, others the next byte repeated that many less 126 times */
static void ff_emblem_unpack_mask(unsigned int offset, unsigned char *out)
{
	unsigned char const *in = (unsigned char const *)firefight_emblem_masks + offset;
	int size = FIREFIGHT_EMBLEM_MASK_DIM * FIREFIGHT_EMBLEM_MASK_DIM;
	int done = 0;

	while (done < size)
	{
		int control = *in++;
		if (control < 128)
		{
			int count = MIN(control + 1, size - done);
			memcpy(out + done, in, (size_t)count);
			in += control + 1;
			done += count;
		}
		else
		{
			int count = MIN(control - 126, size - done);
			memset(out + done, *in++, (size_t)count);
			done += count;
		}
	}
}

/* lays a mask, in color, over the picture so far */
static void ff_emblem_add_layer(struct ff_icon_scratch *scratch, unsigned int word, pixel32 color)
{
	unsigned int offset = word & ~EMBLEM_PRIMARY_ON_TOP;
	int i;

	if (!offset)
		return;
	ff_emblem_unpack_mask(offset, scratch->emblem_mask);
	for (i = 0; i < FF_ICON_HI_DIM * FF_ICON_HI_DIM; i++)
	{
		pixel32 over = (color & 0x00FFFFFF) | ((pixel32)scratch->emblem_mask[i] << 24);

		scratch->pixels128[i] = ff_icon_over(scratch->pixels128[i], over);
	}
}

/* the emblem in the scratch's pixels128: its background, then its foreground's
layers in their order, each mask tinted with its colour; or one of those alone
(part: _firefight_emblem_part_...) */
static void ff_emblem_compose(struct ff_icon_scratch *scratch, struct firefight_emblem_info const *e, short part)
{
	short foreground = (short)PIN(e->foreground, 0, FIREFIGHT_EMBLEM_FOREGROUND_COUNT - 1);
	short background = (short)PIN(e->background, 0, FIREFIGHT_EMBLEM_BACKGROUND_COUNT - 1);
	pixel32 primary = firefight_emblem_palette[PIN(e->primary_color, 0, FIREFIGHT_EMBLEM_COLOR_COUNT - 1)];
	pixel32 secondary = firefight_emblem_palette[PIN(e->secondary_color, 0, FIREFIGHT_EMBLEM_COLOR_COUNT - 1)];
	pixel32 back = firefight_emblem_palette[PIN(e->background_color, 0, FIREFIGHT_EMBLEM_COLOR_COUNT - 1)];
	unsigned int first = ff_emblem_foreground_word(foreground, 0);
	unsigned int second = ff_emblem_foreground_word(foreground, 1);
	int i;

	/* (the empty pixels have the colour under them, for no dark edge where the texture is filtered) */
	for (i = 0; i < FF_ICON_HI_DIM * FF_ICON_HI_DIM; i++)
		scratch->pixels128[i] = (part == _firefight_emblem_part_foreground ? primary : back) & 0x00FFFFFF;

	if (part != _firefight_emblem_part_foreground)
		ff_emblem_add_layer(scratch, firefight_emblem_masks[background], back);
	if (part == _firefight_emblem_part_background)
		return;
	if (e->secondary_layer && (first & EMBLEM_PRIMARY_ON_TOP))
		ff_emblem_add_layer(scratch, second, secondary);
	ff_emblem_add_layer(scratch, first, primary);
	if (e->secondary_layer && !(first & EMBLEM_PRIMARY_ON_TOP))
		ff_emblem_add_layer(scratch, second, secondary);
}

void firefight_draw_player_emblem_part(
	short x0,
	short y0,
	short size,
	struct firefight_emblem_info const *emblem,
	real alpha,
	short part)
{
	struct firefight_emblem_info def_emb = FIREFIGHT_EMBLEM_DEFAULT;
	struct firefight_emblem_info const *e = emblem ? emblem : &def_emb;
	unsigned long fg = (unsigned long)PIN(e->foreground, 0, FIREFIGHT_EMBLEM_FOREGROUND_COUNT - 1);
	unsigned long bg = (unsigned long)PIN(e->background, 0, FIREFIGHT_EMBLEM_BACKGROUND_COUNT - 1);
	unsigned long sec = e->secondary_layer ? 1UL : 0UL;
	unsigned long c_pri = (unsigned long)PIN(e->primary_color, 0, FIREFIGHT_EMBLEM_COLOR_COUNT - 1);
	unsigned long c_sec = (unsigned long)PIN(e->secondary_color, 0, FIREFIGHT_EMBLEM_COLOR_COUNT - 1);
	unsigned long c_bg = (unsigned long)PIN(e->background_color, 0, FIREFIGHT_EMBLEM_COLOR_COUNT - 1);
	/* (7 bits of foreground, 6 of background, 1 of layer, 5 of each colour) */
	unsigned long key;
	struct ff_icon_scratch *scratch = ff_icon_scratch();
	struct ff_cached_icon_mesh *mesh = NULL;
	int i;

	/* (a player without one, as social_extensions_get_person_rank_and_emblem says) */
	if (e->foreground < 0)
		return;
	/* (a part's picture depends on its own settings alone; bits 31 and 29 tell
	it from the whole emblem's, each picture its own in the cache) */
	if (part == _firefight_emblem_part_foreground)
	{
		bg = 0;
		c_bg = 0;
	}
	else if (part == _firefight_emblem_part_background)
	{
		fg = sec = c_pri = c_sec = 0;
	}
	key = 0x40000000UL | (fg << 22) | (bg << 16) | (sec << 15) | (c_pri << 10) | (c_sec << 5) | c_bg |
		(part == _firefight_emblem_part_foreground ? 0x80000000UL :
			part == _firefight_emblem_part_background ? 0x20000000UL : 0);
	if (global_d3d_device)
	{
		if (ff_draw_hw_icon_pixels(key, NULL, x0, y0, size, alpha))
			return;
		if (scratch)
		{
			ff_emblem_compose(scratch, e, part);
			if (ff_draw_hw_icon_pixels(key, scratch->pixels128, x0, y0, size, alpha))
				return;
		}
	}

	for (i = 0; i < 8; i++)
	{
		if (s_emblem_mesh_cache[i].valid && s_emblem_mesh_cache[i].key == key)
		{
			mesh = &s_emblem_mesh_cache[i];
			break;
		}
	}

	if (!mesh)
	{
		pixel32 grid[FF_ICON_GRID_DIM][FF_ICON_GRID_DIM];

		if (!scratch)
			return;
		ff_emblem_compose(scratch, e, part);
		ff_icon_grid(scratch, grid);
		mesh = &s_emblem_mesh_cache[s_emblem_mesh_next];
		s_emblem_mesh_next = (s_emblem_mesh_next + 1) & 7;
		ff_build_merged_icon_mesh(grid, mesh, key);
	}

	ff_draw_cached_icon_mesh(mesh, x0, y0, size, alpha);
}

void firefight_draw_player_emblem(
	short x0,
	short y0,
	short size,
	struct firefight_emblem_info const *emblem,
	real alpha)
{
	firefight_draw_player_emblem_part(x0, y0, size, emblem, alpha, _firefight_emblem_part_whole);
}

static void draw_screen_quad(short x0, short y0, short x1, short y1, pixel32 col)
{
	rectangle2d r;
	if (x1 <= x0 || y1 <= y0)
		return;
	r.x0 = x0;
	r.y0 = y0;
	r.x1 = x1;
	r.y1 = y1;
	draw_quad(&r, col);
}

struct mcc_nameplate_theme
{
	pixel32 left_bg;
	pixel32 right_bg;
	pixel32 border;
	pixel32 stripe_a;
	pixel32 stripe_b;
};

static struct mcc_nameplate_theme const s_mcc_nameplate_themes[FIREFIGHT_NAMEPLATE_COUNT] =
{
	/* 0: UNSC Standard (Navy & Tactical Cyan) */
	{ 0xE60B1E38, 0xE6050D1C, 0xFF2896FF, 0x702896FF, 0x4070C8FF },
	/* 1: Halo Ring (Deep Space & Forerunner Teal Arc) */
	{ 0xE6082832, 0xE604101A, 0xFF3CE8D8, 0x883CE8D8, 0x5098FFF5 },
	/* 2: Legendary Shield (Crimson & Gold Trim) */
	{ 0xE6360D12, 0xE6180508, 0xFFF2C038, 0x88F2C038, 0x58FF5048 },
	/* 3: ODST Drop Pod (Orbital Charcoal & Hazard Amber) */
	{ 0xE61A1E26, 0xE60B0D12, 0xFFF09824, 0x90F09824, 0x50FFD060 },
	/* 4: Covenant Empire (Royal Violet & Plasma Magenta) */
	{ 0xE6240F3D, 0xE60F061C, 0xFFC058FF, 0x88C058FF, 0x5050D8FF },
	/* 5: Forerunner Glyph (Obsidian & Hardlight Gold-Cyan) */
	{ 0xE6141C24, 0xE6080B10, 0xFFFFD868, 0x8848E8FF, 0x60FFD868 },
	/* 6: Firefight Veteran (Scorched Crimson & Ember Orange) */
	{ 0xE63B140C, 0xE61A0704, 0xFFFF682C, 0x90FF682C, 0x58FFCC40 },
	/* 7: Golden Warthog (UNSC Olive Drab & Gold Brass) */
	{ 0xE61B2916, 0xE60B1209, 0xFFE8C242, 0x88E8C242, 0x5078C860 },
	/* 8: Pillar of Autumn (Steel Cruiser & Reactor Blue) */
	{ 0xE61C2634, 0xE60C1118, 0xFF68B8FF, 0x8868B8FF, 0x50FFA038 },
	/* 9: Reclaimer (Mythic Teal & Solar Gold) */
	{ 0xE60D2B3E, 0xE61E0C2E, 0xFFFFDF60, 0x903CF0E0, 0x68FFDF60 }
};

void firefight_draw_mcc_nameplate(
	short x0,
	short y0,
	short x1,
	short y1,
	short nameplate_id,
	pixel32 armor_accent,
	real alpha)
{
	short id = (short)PIN(nameplate_id, 0, FIREFIGHT_NAMEPLATE_COUNT - 1);
	struct mcc_nameplate_theme const *th = &s_mcc_nameplate_themes[id];
	short w = (short)(x1 - x0);
	short h = (short)(y1 - y0);
	short b;
	pixel32 border_c = ff_apply_alpha((id == 0 && (armor_accent & 0x00FFFFFF) != 0) ? armor_accent : th->border, alpha);
	pixel32 sa = ff_apply_alpha(th->stripe_a, alpha);
	pixel32 sb = ff_apply_alpha(th->stripe_b, alpha);

	if (w <= 8 || h <= 6)
		return;

	/* 4-band horizontal gradient background */
	for (b = 0; b < 4; b++)
	{
		short bx0 = (short)(x0 + (w * b) / 4);
		short bx1 = (b == 3) ? x1 : (short)(x0 + (w * (b + 1)) / 4);
		pixel32 bg_c = ff_apply_alpha(ff_lerp_argb(th->left_bg, th->right_bg, (real)b / 3.0f), alpha);
		draw_screen_quad(bx0, y0, bx1, y1, bg_c);
	}

	/* Distinctive geometric motif per MCC Nameplate theme (fast quad primitives) */
	switch (id)
	{
	case 0: /* UNSC Standard: dual tactical horizon bars */
		draw_screen_quad((short)(x0 + 4), (short)(y1 - 5), (short)(x1 - 4), (short)(y1 - 3), sa);
		draw_screen_quad((short)(x1 - w / 3), (short)(y0 + 3), (short)(x1 - 4), (short)(y0 + 5), sb);
		break;
	case 1: /* Halo Ring: segmented diagonal installation arc */
		for (b = 0; b < 4; b++)
		{
			short rx = (short)(x1 - 54 + b * 10);
			short ry = (short)(y0 + 3 + b * (h - 8) / 4);
			draw_screen_quad(rx, ry, (short)(rx + 12), (short)(ry + 3), (b & 1) ? sb : sa);
		}
		break;
	case 2: /* Legendary Shield: twin crossed combat blades & gold crest */
		draw_screen_quad((short)(x0 + 4), (short)(y0 + h / 2 - 1), (short)(x1 - 4), (short)(y0 + h / 2 + 1), sa);
		draw_screen_quad((short)(x1 - 42), (short)(y0 + 4), (short)(x1 - 14), (short)(y0 + 7), sb);
		draw_screen_quad((short)(x1 - 42), (short)(y1 - 7), (short)(x1 - 14), (short)(y1 - 4), sb);
		break;
	case 3: /* ODST Drop Pod: hazard chevron stripes */
		for (b = 0; b < 4; b++)
		{
			short hx = (short)(x1 - 56 + b * 12);
			draw_screen_quad(hx, (short)(y0 + 3), (short)(hx + 6), (short)(y0 + h / 2), sa);
			draw_screen_quad((short)(hx + 2), (short)(y0 + h / 2), (short)(hx + 8), (short)(y1 - 3), sb);
		}
		break;
	case 4: /* Covenant Empire: energy lattice diamonds */
		draw_screen_quad((short)(x1 - 52), (short)(y0 + h / 2 - 2), (short)(x1 - 8), (short)(y0 + h / 2 + 2), sa);
		draw_screen_quad((short)(x1 - 36), (short)(y0 + 4), (short)(x1 - 24), (short)(y1 - 4), sb);
		break;
	case 5: /* Forerunner Glyph: Didact geometric pylon & rings */
		draw_screen_quad((short)(x1 - 46), (short)(y0 + 4), (short)(x1 - 12), (short)(y0 + 6), sa);
		draw_screen_quad((short)(x1 - 46), (short)(y1 - 6), (short)(x1 - 12), (short)(y1 - 4), sa);
		draw_screen_quad((short)(x1 - 32), (short)(y0 + 6), (short)(x1 - 26), (short)(y1 - 6), sb);
		break;
	case 6: /* Firefight Veteran: triple wave-tally bars & ember trim */
		draw_screen_quad((short)(x0 + 4), (short)(y1 - 5), (short)(x1 - 4), (short)(y1 - 3), sa);
		for (b = 0; b < 3; b++)
		{
			short vx = (short)(x1 - 38 + b * 9);
			draw_screen_quad(vx, (short)(y0 + 5), (short)(vx + 5), (short)(y1 - 6), sb);
		}
		break;
	case 7: /* Golden Warthog: twin racing stripes */
		draw_screen_quad((short)(x0 + 4), (short)(y1 - 8), (short)(x1 - 4), (short)(y1 - 6), sa);
		draw_screen_quad((short)(x0 + 4), (short)(y1 - 5), (short)(x1 - 4), (short)(y1 - 3), sb);
		break;
	case 8: /* Pillar of Autumn: MAC barrel rails & reactor core */
		draw_screen_quad((short)(x0 + w / 3), (short)(y0 + 4), (short)(x1 - 6), (short)(y0 + 6), sa);
		draw_screen_quad((short)(x0 + w / 2), (short)(y1 - 6), (short)(x1 - 6), (short)(y1 - 4), sa);
		draw_screen_quad((short)(x1 - 20), (short)(y0 + h / 2 - 3), (short)(x1 - 10), (short)(y0 + h / 2 + 3), sb);
		break;
	case 9: /* Reclaimer: full mythic double-border & twin energy conduits */
	default:
		draw_screen_quad((short)(x0 + 4), (short)(y0 + 3), (short)(x1 - 4), (short)(y0 + 5), sa);
		draw_screen_quad((short)(x0 + 4), (short)(y1 - 5), (short)(x1 - 4), (short)(y1 - 3), sb);
		draw_screen_quad((short)(x1 - 34), (short)(y0 + h / 2 - 2), (short)(x1 - 8), (short)(y0 + h / 2 + 2), sa);
		break;
	}

	/* Left accent bar & outer frame */
	draw_screen_quad(x0, y0, (short)(x0 + 4), y1, border_c);
	draw_screen_quad(x0, y0, x1, (short)(y0 + 1), border_c);
	draw_screen_quad(x0, (short)(y1 - 1), x1, y1, border_c);
	draw_screen_quad((short)(x1 - 1), y0, x1, y1, border_c);
}


/* the match's report's extras, a multiplayer game's (Firefight's has its own
card: campaign_score.c): the best player's row marked, and the local player's
best medals. Rank and XP are the screen after it */
void firefight_hud_draw_pgcr_extras(
	long mvp_player_index,
	short mvp_row)
{
	long font_index = hud_get_font_index();
	struct font_header *font;
	short line_height;
	real vx0 = (real)(-render.camera.viewport_bounds.x0);
	real vy0 = (real)(-render.camera.viewport_bounds.y0);
	real scale = 1.0f;
	long local_p = local_player_get_player_index(0);
	short top_medal_ids[3] = { NONE, NONE, NONE };
	short top_medal_counts[3] = { 0, 0, 0 };
	short top_medal_num = 0;
	struct firefight_postgame_xp_summary xp_sum;
#if OPENCE_ENABLE_NAMEPLATES
	struct firefight_emblem_info emb;
#endif
	real_argb_color gold = { 1.0f, 1.0f, 0.85f, 0.28f };
	real_argb_color cyan = { 1.0f, 0.62f, 0.85f, 1.0f };
	real_argb_color white = { 1.0f, 0.95f, 0.95f, 0.95f };

	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	if (!font)
		return;
	line_height = (short)(font->ascending_height + font->descending_height);
	if (line_height <= 0)
		return;

	/* (the match's XP is recorded as its report is first shown) */
	firefight_stats_get_postgame_xp_summary(&xp_sum);

	/* 1. Highlight Top Performer Row with Golden [MVP] Badge */
	if (mvp_player_index != NONE && mvp_row >= 8)
	{
		real ry = (real)(mvp_row * 18);
		ff_draw_quad(vx0, vy0, scale, 42.0f, ry - 1.0f, 594.0f, ry + 17.0f, 0x28FFD040);
		ff_draw_quad(vx0, vy0, scale, 6.0f, ry + 1.0f, 40.0f, ry + 15.0f, 0xFFE8B828);
		ff_draw_quad(vx0, vy0, scale, 7.0f, ry + 2.0f, 39.0f, ry + 14.0f, 0xE6382406);
		ff_draw_text(font_index, line_height, vx0, vy0, scale,
			"MVP", 7.0f, 39.0f, ry + 1.5f, 0.50f, _ff_text_center, &gold);
	}

	/* 2. Local Player Nameplate + Top 3 Medals Earned */
	{
		real lx0 = 45.0f, ly0 = 324.0f, lx1 = 595.0f, ly1 = 404.0f;
		long target_p = (local_p != NONE) ? local_p : mvp_player_index;
		long medals_bmp = campaign_medals_bitmap();
		short m;

		ff_draw_box(vx0, vy0, scale, lx0, ly0, lx1, ly1, 18.0f, FF_HUD_PANEL_FILL, FF_HUD_PANEL_EDGE);
#if OPENCE_ENABLE_NAMEPLATES
		firefight_stats_get_emblem(NONE, &emb);
		firefight_draw_mcc_nameplate(
			(short)(vx0 + lx0 + 2.0f),
			(short)(vy0 + ly0 + 2.0f),
			(short)(vx0 + lx1 - 2.0f),
			(short)(vy0 + ly0 + 19.0f),
			emb.nameplate,
			0,
			0.85f);
#endif
		ff_draw_text(font_index, line_height, vx0, vy0, scale,
			"TOP MEDALS & MVP SHOWCASE", lx0 + 8.0f, lx1 - 8.0f, ly0 + 3.0f,
			0.52f, _ff_text_left, &gold);

		top_medal_num = firefight_stats_get_player_top_medals(target_p, top_medal_ids, top_medal_counts, 3);
		if (top_medal_num <= 0)
		{
			ff_draw_text(font_index, line_height, vx0, vy0, scale,
				"NO MEDALS EARNED THIS MATCH", lx0 + 10.0f, lx1 - 10.0f, ly0 + 42.0f,
				0.54f, _ff_text_center, &cyan);
		}
		else
		{
			real slot_w = (lx1 - lx0 - 16.0f) / 3.0f;
			for (m = 0; m < top_medal_num; m++)
			{
				short mid = top_medal_ids[m];
				real sx0 = lx0 + 8.0f + (real)m * slot_w;
				real scx = sx0 + slot_w * 0.5f;
				struct bitmap_data *bmp = (medals_bmp != NONE && mid >= 0 && mid < NUMBER_OF_CAMPAIGN_MEDALS)
					? bitmap_group_try_and_get_bitmap(medals_bmp, mid)
					: NULL;
				char cnt_str[24];
				char lbl_str[32];

				ff_draw_quad(vx0, vy0, scale, sx0 + 2.0f, ly0 + 22.0f, sx0 + slot_w - 2.0f, ly1 - 4.0f, 0x6008182E);
				if (bmp)
				{
					rectangle2d mb;
					mb.x0 = (short)(vx0 + scx - 15.0f);
					mb.x1 = (short)(vx0 + scx + 15.0f);
					mb.y0 = (short)(vy0 + ly0 + 24.0f);
					mb.y1 = (short)(vy0 + ly0 + 54.0f);
					draw_bitmap_in_rect(bmp, &mb, NULL, NULL, 0xFFFFFFFF, NULL, TRUE);
				}
				else
				{
					ff_draw_quad(vx0, vy0, scale, scx - 10.0f, ly0 + 27.0f, scx + 10.0f, ly0 + 47.0f, 0xFFD4A838);
				}

				snprintf(cnt_str, sizeof(cnt_str), "x%d", (int)top_medal_counts[m]);
				ff_draw_text(font_index, line_height, vx0, vy0, scale,
					cnt_str, sx0 + 4.0f, sx0 + slot_w - 4.0f, ly0 + 24.0f,
					0.52f, _ff_text_right, &gold);

				if (mid >= 0 && mid < NUMBER_OF_CAMPAIGN_MEDALS && campaign_medal_descriptions[mid].label)
				{
					snprintf(lbl_str, sizeof(lbl_str), "%.13s", campaign_medal_descriptions[mid].label);
					ff_draw_text(font_index, line_height, vx0, vy0, scale,
						lbl_str, sx0 + 2.0f, sx0 + slot_w - 2.0f, ly0 + 58.0f,
						0.45f, _ff_text_center, &white);
				}
			}
		}
	}
}

/* ---------- the postgame's XP screen

after the report, a card as the campaign's level summary is: the player's rank
as the match began, a bar that fills through what the match gave, a line at a
time, each saying what gave it, the rank changing as the bar passes one */

enum
{
	FF_XP_CARD_WIDTH = 440,
	FF_XP_CARD_PADDING = 14,
	FF_XP_CARD_GAP = 8,
	FF_XP_ICON_SIZE = 40,
	FF_XP_BAR_HEIGHT = 14,
	/* (the card's room above the prompt, at 410 of the 480) */
	FF_XP_CARD_BOTTOM = 402,
	/* (before the first line, each line, and in milliseconds) */
	FF_XP_START_HOLD_MS = 900,
	FF_XP_LINE_MS = 650
};

static struct firefight_postgame_xp_summary ff_xp_summary;
static unsigned long ff_xp_start_ms;
/* the postgame's XP screen was opened in this game, so leaving it needs no card */
static boolean ff_xp_screen_seen;
/* the card over the menus for a game left without that screen: waiting to be
closed, when it was last drawn, and when it was closed (the press that closed
it stays the card's a moment, held or let go) */
static struct
{
	boolean pending;
	unsigned long drawn_at;
	unsigned long closed_at;
} ff_xp_exit;

boolean firefight_hud_xp_screen_wanted(void)
{
#if OPENCE_ENABLE_SERVICE_RANK
	struct firefight_postgame_xp_summary summary;

	firefight_stats_get_postgame_xp_summary(&summary);
	return summary.total_gained_xp > 0;
#else
	return FALSE;
#endif
}

void firefight_hud_xp_screen_begin(void)
{
	firefight_stats_get_postgame_xp_summary(&ff_xp_summary);
	ff_xp_start_ms = system_milliseconds();
	ff_xp_screen_seen = TRUE;
}

/* how long the lines take to count up */
static unsigned long ff_xp_animation_ms(void)
{
	return FF_XP_START_HOLD_MS + (unsigned long)ff_xp_summary.line_count * FF_XP_LINE_MS;
}

boolean firefight_hud_xp_screen_finish(void)
{
	unsigned long now = system_milliseconds();

	if (now - ff_xp_start_ms >= ff_xp_animation_ms())
		return FALSE;
	ff_xp_start_ms = now - ff_xp_animation_ms();
	return TRUE;
}

/* the XP counted up after `elapsed` milliseconds: the lines before the one
being counted, and that one's share, easing in and out; the line's index */
static long ff_xp_counted(
	unsigned long elapsed,
	short *line_counting,
	real *line_fraction)
{
	long counted = 0;
	short index;

	*line_counting = -1;
	*line_fraction = 0.0f;
	if (elapsed < FF_XP_START_HOLD_MS)
		return 0;
	elapsed -= FF_XP_START_HOLD_MS;
	for (index = 0; index < ff_xp_summary.line_count; index++)
	{
		if (elapsed < FF_XP_LINE_MS)
		{
			real t = (real)elapsed / (real)FF_XP_LINE_MS;

			*line_counting = index;
			*line_fraction = t * t * (3.0f - 2.0f * t);
			return counted + (long)(ff_xp_summary.lines[index].xp * *line_fraction);
		}
		elapsed -= FF_XP_LINE_MS;
		counted += ff_xp_summary.lines[index].xp;
	}
	*line_counting = ff_xp_summary.line_count;
	return counted;
}

static void ff_xp_color(
	real_argb_color *color,
	real red,
	real green,
	real blue)
{
	color->alpha = 1.0f;
	color->red = red;
	color->green = green;
	color->blue = blue;
}

/* the card's text and bars, from its top: its height, drawn or not */
static real ff_xp_card_layout(
	struct campaign_score_panel const *panel,
	unsigned long elapsed,
	boolean draw)
{
	real const left = FF_XP_CARD_PADDING;
	real const right = FF_XP_CARD_WIDTH - FF_XP_CARD_PADDING;
	real const line = panel->line_height;
	real const small = panel->line_height * CAMPAIGN_SCORE_SUBLINE_SIZE;
	struct firefight_mcc_rank_info rank;
	real_argb_color color;
	short line_counting;
	real line_fraction;
	long counted = ff_xp_counted(elapsed, &line_counting, &line_fraction);
	boolean ranked_up;
	boolean finished = line_counting >= ff_xp_summary.line_count;
	char text[64];
	real y = FF_XP_CARD_PADDING;
	short index;

	service_rank_compute_from_xp(ff_xp_summary.start_xp + counted, &rank);
	ranked_up = rank.tour_number > ff_xp_summary.before_rank.tour_number ||
		rank.rank_in_tour > ff_xp_summary.before_rank.rank_in_tour;

	if (draw)
	{
		ff_xp_color(&color, 0.95f, 0.95f, 0.95f);
		campaign_score_panel_text(panel, "RANK PROGRESS", NULL, left, right, y, 1.0f, _campaign_score_text_center,
			&color);
	}
	y += line + 4.0f;
	if (draw)
		campaign_score_panel_quad(panel, left, y, right, y + 1.0f, CAMPAIGN_SCORE_CARD_RULE);
	y += FF_XP_CARD_GAP;

	/* the rank, its icon and the player's emblem, what is held in all */
	if (draw)
	{
		struct firefight_emblem_info emblem;
		rectangle2d icons;

		firefight_stats_get_emblem(NONE, &emblem);
		icons.x0 = (short)(panel->left + left * panel->scale);
		icons.y0 = (short)(panel->top + y * panel->scale);
		firefight_draw_mcc_rank_icon(icons.x0, icons.y0, (short)(FF_XP_ICON_SIZE * panel->scale), rank.tour_number,
			rank.rank_in_tour, 1.0f);
		firefight_draw_player_emblem((short)(icons.x0 + (FF_XP_ICON_SIZE + 4.0f) * panel->scale), icons.y0,
			(short)(FF_XP_ICON_SIZE * panel->scale), &emblem, 1.0f);
		if (ranked_up)
			ff_xp_color(&color, 1.0f, 0.84f, 0.35f);
		else
			ff_xp_color(&color, 0.95f, 0.95f, 0.95f);
		campaign_score_panel_text(panel, rank.rank_name, NULL, left + FF_XP_ICON_SIZE * 2.0f + 12.0f, right, y, 1.0f,
			_campaign_score_text_left, &color);
		snprintf(text, sizeof(text), "%s - RANK %d", rank.tour_name, (int)rank.rank_in_tour);
		ff_xp_color(&color, 0.55f, 0.72f, 1.0f);
		campaign_score_panel_text(panel, text, NULL, left + FF_XP_ICON_SIZE * 2.0f + 12.0f, right, y + line,
			CAMPAIGN_SCORE_SUBLINE_SIZE, _campaign_score_text_left, &color);
		if (ranked_up)
		{
			ff_xp_color(&color, 1.0f, 0.84f, 0.35f);
			campaign_score_panel_text(panel, "RANK UP!", NULL, left, right, y, 1.0f, _campaign_score_text_right,
				&color);
		}
	}
	y += MAX((real)FF_XP_ICON_SIZE, line + small) + FF_XP_CARD_GAP;

	/* the bar through this rank */
	if (draw)
	{
		long span = MAX(1L, rank.xp_for_next_rank);
		real fraction = PIN((real)rank.xp_in_rank / (real)span, 0.0f, 1.0f);
		real fill = (right - left - 2.0f) * fraction;

		campaign_score_panel_quad(panel, left, y, right, y + FF_XP_BAR_HEIGHT, 0xFF0C2A4C);
		if (fill >= 1.0f)
		{
			campaign_score_panel_quad(panel, left + 1.0f, y + 1.0f, left + 1.0f + fill, y + FF_XP_BAR_HEIGHT - 1.0f,
				ranked_up ? 0xFFE8B828 : 0xFF2896FF);
		}
		campaign_score_panel_frame(panel, left, y, right, y + FF_XP_BAR_HEIGHT, CAMPAIGN_SCORE_PANEL_EDGE);
	}
	y += FF_XP_BAR_HEIGHT + 3.0f;
	if (draw)
	{
		snprintf(text, sizeof(text), "%ld / %ld XP", rank.xp_in_rank, MAX(1L, rank.xp_for_next_rank));
		ff_xp_color(&color, 0.6f, 0.66f, 0.75f);
		campaign_score_panel_text(panel, text, NULL, left, right, y, CAMPAIGN_SCORE_SUBLINE_SIZE,
			_campaign_score_text_right, &color);
		snprintf(text, sizeof(text), "STARTED AT %ld XP", ff_xp_summary.start_xp);
		campaign_score_panel_text(panel, text, NULL, left, right, y, CAMPAIGN_SCORE_SUBLINE_SIZE,
			_campaign_score_text_left, &color);
	}
	y += small + FF_XP_CARD_GAP;
	if (draw)
		campaign_score_panel_quad(panel, left, y, right, y + 1.0f, CAMPAIGN_SCORE_CARD_RULE);
	y += FF_XP_CARD_GAP;

	/* what gave it, a line at a time */
	for (index = 0; index < ff_xp_summary.line_count; index++)
	{
		struct firefight_postgame_xp_line const *entry = &ff_xp_summary.lines[index];

		if (draw && index <= line_counting)
		{
			long shown = index == line_counting ? (long)(entry->xp * line_fraction) : entry->xp;

			if (entry->count > 1)
				snprintf(text, sizeof(text), "%s x%ld", entry->label, entry->count);
			else
				snprintf(text, sizeof(text), "%s", entry->label);
			ff_xp_color(&color, 0.55f, 0.72f, 1.0f);
			campaign_score_panel_text(panel, text, NULL, left, right, y, CAMPAIGN_SCORE_SUBLINE_SIZE,
				_campaign_score_text_left, &color);
			snprintf(text, sizeof(text), "+%ld", shown);
			ff_xp_color(&color, 0.95f, 0.95f, 0.95f);
			campaign_score_panel_text(panel, text, NULL, left, right, y, CAMPAIGN_SCORE_SUBLINE_SIZE,
				_campaign_score_text_right, &color);
		}
		y += small + 4.0f;
	}
	y += FF_XP_CARD_GAP - 4.0f;
	if (draw)
		campaign_score_panel_quad(panel, left, y, right, y + 1.0f, CAMPAIGN_SCORE_CARD_RULE);
	y += FF_XP_CARD_GAP;

	/* the total */
	if (draw)
	{
		snprintf(text, sizeof(text), "+%ld", finished ? ff_xp_summary.total_gained_xp : counted);
		ff_xp_color(&color, 0.95f, 0.95f, 0.95f);
		campaign_score_panel_text(panel, "XP EARNED", NULL, left, right, y, 1.0f, _campaign_score_text_left, &color);
		ff_xp_color(&color, 1.0f, 0.84f, 0.35f);
		campaign_score_panel_text(panel, text, NULL, left, right, y, 1.0f, _campaign_score_text_right, &color);
	}
	y += line;

	return y + FF_XP_CARD_PADDING - 4.0f;
}

/* the card in the 640 by 480 from (left, top), `scale` pixels to its unit,
above FF_XP_CARD_BOTTOM */
static void ff_xp_card_draw(
	struct campaign_score_panel *panel,
	real left,
	real top,
	real scale,
	unsigned long elapsed)
{
	real height = ff_xp_card_layout(panel, elapsed, FALSE);

	panel->scale = scale;
	panel->width = FF_XP_CARD_WIDTH;
	panel->left = left + (640.0f - FF_XP_CARD_WIDTH) / 2.0f * scale;
	panel->top = top + MAX(6.0f, (FF_XP_CARD_BOTTOM - height) / 2.0f) * scale;
	campaign_score_panel_quad(panel, 0.0f, 0.0f, FF_XP_CARD_WIDTH, height, CAMPAIGN_SCORE_CARD_FILL);
	campaign_score_panel_frame(panel, 0.0f, 0.0f, FF_XP_CARD_WIDTH, height, CAMPAIGN_SCORE_PANEL_EDGE);
	ff_xp_card_layout(panel, elapsed, TRUE);
}

void firefight_hud_xp_screen_draw(void)
{
	struct campaign_score_panel panel;
	struct font_header *font;
	real vx0 = (real)(-render.camera.viewport_bounds.x0);
	real vy0 = (real)(-render.camera.viewport_bounds.y0);

	panel.font_index = hud_get_font_index();
	if (panel.font_index == NONE)
		return;
	font = font_definition_get(panel.font_index);
	if (!font)
		return;
	panel.line_height = (short)(font->ascending_height + font->descending_height);
	if (panel.line_height <= 0)
		return;
	panel.scale = 1.0f;
	panel.width = FF_XP_CARD_WIDTH;
	panel.left = vx0;
	panel.top = vy0;
	campaign_score_panel_quad(&panel, 0.0f, 0.0f, 640.0f, 480.0f, 0xFF000000);
	ff_xp_card_draw(&panel, vx0, vy0, 1.0f, system_milliseconds() - ff_xp_start_ms);
}

/* ---------- the same card after a game left without that screen (quit from
the pause menu, B on the report, the host going on first, a campaign left):
over the main menu or the lobby, until A, B or a click closes it */

enum
{
	/* (no draw this long: not up, so the menus keep their input) */
	FF_XP_EXIT_SHOWN_MS = 500,
	FF_XP_EXIT_CLOSING_MS = 250,
	FF_XP_EXIT_PROMPT_TOP = 424
};

void firefight_hud_xp_exit_note(void)
{
#if OPENCE_ENABLE_SERVICE_RANK
	boolean seen = ff_xp_screen_seen;

	ff_xp_screen_seen = FALSE;
	ff_xp_exit.pending = FALSE;
	ff_xp_exit.drawn_at = 0;
	if (seen)
		return;
	firefight_stats_get_postgame_xp_summary(&ff_xp_summary);
	ff_xp_exit.pending = ff_xp_summary.total_gained_xp > 0;
#endif
}

void firefight_hud_xp_card_show(struct firefight_postgame_xp_summary const *summary)
{
	ff_xp_summary = *summary;
	ff_xp_exit.pending = TRUE;
	ff_xp_exit.drawn_at = 0;
}

boolean firefight_hud_xp_exit_shown(void)
{
	unsigned long now = system_milliseconds();

	/* (signed: each stamp, made odd, may be a millisecond ahead of now) */
	if (ff_xp_exit.closed_at && (long)(now - ff_xp_exit.closed_at) < FF_XP_EXIT_CLOSING_MS)
		return TRUE;
	ff_xp_exit.closed_at = 0;
	return ff_xp_exit.pending && ff_xp_exit.drawn_at && (long)(now - ff_xp_exit.drawn_at) < FF_XP_EXIT_SHOWN_MS;
}

void firefight_hud_xp_exit_press(boolean close)
{
	if (!ff_xp_exit.pending || (!close && firefight_hud_xp_screen_finish()))
		return;
	ff_xp_exit.pending = FALSE;
	ff_xp_exit.closed_at = system_milliseconds() | 1;
}

void firefight_hud_xp_exit_draw(short screen_width, short screen_height)
{
	struct campaign_score_panel panel;
	struct font_header *font;
	struct scenario *scenario;
	real_argb_color color;
	real scale;
	real left;

	if (!ff_xp_exit.pending || screen_width <= 0 || screen_height <= 0)
		return;
	scenario = global_scenario_index != NONE ? global_scenario_try_and_get() : NULL;
	if (!scenario)
		return;
	/* (another game started instead of the menus: that one has its own) */
	if (scenario->type != _scenario_type_main_menu)
	{
		ff_xp_exit.pending = FALSE;
		return;
	}
	/* (the HUD's font is not on the menus' map) */
	panel.font_index = tag_loaded('font', "ui\\small_ui");
	if (panel.font_index == NONE)
		panel.font_index = interface_get_tag_index(_interface_font_terminal);
	if (panel.font_index == NONE)
		return;
	font = font_definition_get(panel.font_index);
	if (!font)
		return;
	panel.line_height = (short)(font->ascending_height + font->descending_height);
	if (panel.line_height <= 0)
		return;
	/* (the count starts as it is first seen, not behind the map's loading) */
	if (!ff_xp_exit.drawn_at)
		ff_xp_start_ms = system_milliseconds();
	ff_xp_exit.drawn_at = system_milliseconds() | 1;
	/* (the menus dimmed behind it) */
	panel.scale = 1.0f;
	panel.width = screen_width;
	panel.left = panel.top = 0.0f;
	campaign_score_panel_quad(&panel, 0.0f, 0.0f, screen_width, screen_height, 0xB0000000);
	scale = (real)screen_height / 480.0f;
	left = (screen_width - 640.0f * scale) / 2.0f;
	ff_xp_card_draw(&panel, left, 0.0f, scale, system_milliseconds() - ff_xp_start_ms);
	panel.left = left;
	panel.top = 0.0f;
	panel.width = 640.0f;
	ff_xp_color(&color, 0.55f, 0.72f, 1.0f);
	campaign_score_panel_text(&panel, "A or click: continue", NULL, 0.0f, 640.0f, FF_XP_EXIT_PROMPT_TOP, 1.0f,
		_campaign_score_text_center, &color);
}
