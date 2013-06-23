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

#ifndef MAIN_H
#define MAIN_H


// frameskip type
#define AUTO_FRAMESKIP   0
#define MANUAL_FRAMESKIP 1
#define NO_FRAMESKIP     2

extern u32 option_frameskip_type;
extern u32 option_frameskip_value;
extern u32 option_random_skip;
extern u32 option_clock_speed;
extern u32 option_update_backup;

extern u32 psp_model;
extern u32 firmware_version;

// extern u32 cycle_dma16_words;
// extern u32 cycle_dma32_words;
extern u32 cycle_count_dma;

extern char main_path[MAX_PATH];

extern u8 quit_flag;
extern u8 sleep_flag;
extern u8 synchronize_flag;
extern u8 psp_fps_debug;

extern u32 real_frame_count;
extern u32 virtual_frame_count;

void direct_sound_timer_select(u32 value);

void timer_control_low(u8 timer_number, u32 value);
void timer_control_high(u8 timer_number, u32 value);

u32 update_gba(void);
void reset_gba(void);
void quit(void);

void get_ticks_us(u64 *tick_return);
void game_name_ext(u8 *src, u8 *buffer, u8 *extension);
void error_msg(char *text);
void change_ext(char *src, char *buffer, char *extension);
u32 file_length(char *filename);

void set_cpu_clock(int psp_clock);

void main_write_mem_savestate(FILE_TAG_TYPE savestate_file);
void main_read_savestate(FILE_TAG_TYPE savestate_file);


#endif /* MAIN_H */
