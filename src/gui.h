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

#ifndef GUI_H
#define GUI_H


s8 load_file(char **wildcards, char *result, char *default_dir_name);
s8 load_game_config_file(void);
s8 load_config_file(void);
s8 save_config_file(void);

s8 load_dir_cfg(char *file_name);
s8 load_msg_cfg(char *file_name);
s8 load_font_cfg(char *file_name, char *s_font, char *d_font);

void setup_status_str(s8 font_width);

u8 menu(void);

void action_loadstate(void);
void action_savestate(void);

extern char dir_roms[MAX_PATH];
extern char dir_save[MAX_PATH];
extern char dir_cfg[MAX_PATH];
extern char dir_snap[MAX_PATH];
extern char dir_cheat[MAX_PATH];

#endif /* GUI_H */
