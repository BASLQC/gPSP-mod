/* unofficial gameplaySP kai
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 * Copyright (C) 2007 takka <takka@tfact.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef VIDEO_H
#define VIDEO_H


// video scale type
#define UNSCALED      0
#define SCALED_X15_GU 1
#define SCALED_X15_SW 2
#define SCALED_USER   3

// video filter type
#define FILTER_NEAREST  0
#define FILTER_BILINEAR 1

extern u32 option_screen_scale;
extern u32 option_screen_filter;
extern u32 option_screen_mag;

void update_scanline(void);
void (*update_screen)(void);

void flip_screen(u8 vsync);

void video_resolution_large(void);
void video_resolution_small(void);

void init_video(void);
void video_term(void);

void print_string(char *str, u32 fg_color, u32 bg_color, u32 x, u32 y);
void print_string_pad(char *str, u32 fg_color, u32 bg_color, u32 x, u32 y,
 u32 pad);
void print_string_ext(char *str, u32 fg_color, u32 bg_color, u32 x, u32 y,
 void *_dest_ptr, u32 pitch, u32 pad);

void clear_screen(u16 color);
void blit_to_screen(u16 *src, u32 w, u32 h, u32 dest_x, u32 dest_y);
u16 *copy_screen(void);

extern s32 affine_reference_x[2];
extern s32 affine_reference_y[2];

typedef void (* tile_render_function)(u32 layer_number, u32 start, u32 end,
 void *dest_ptr);
typedef void (* bitmap_render_function)(u32 start, u32 end, void *dest_ptr);

typedef struct
{
  tile_render_function normal_render_base;
  tile_render_function normal_render_transparent;
  tile_render_function alpha_render_base;
  tile_render_function alpha_render_transparent;
  tile_render_function color16_render_base;
  tile_render_function color16_render_transparent;
  tile_render_function color32_render_base;
  tile_render_function color32_render_transparent;
} TILE_LAYER_RENDER_STRUCT;

typedef struct
{
  bitmap_render_function normal_render;
} BITMAP_LAYER_RENDER_STRUCT;


void draw_box_line(u32 x1, u32 y1, u32 x2, u32 y2, u16 color);
void draw_box_fill(u32 x1, u32 y1, u32 x2, u32 y2, u16 color);

int draw_volume_status(int draw);

void video_write_mem_savestate(FILE_TAG_TYPE savestate_file);
void video_read_savestate(FILE_TAG_TYPE savestate_file);

#endif /* VIDEO_H */
