/* unofficial gameplaySP kai
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 * Copyright (C) 2007 takka <takka@tfact.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licens e as
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

#include "common.h"


#define GPSP_CONFIG_FILENAME "gpsp_mod.cfg"

void _flush_cache(void);

static s8 save_game_config_file(void);

static int sort_function(const void *dest_str_ptr, const void *src_str_ptr);

u32 time_status_pos_x;
u32 batt_status_pos_x;
u32 current_dir_name_length;
static u16 update_status_str(char *time_str, char *batt_str);

int date_format;
static void get_timestamp_string(char *buffer, u16 msg_id, pspTime msg_time,
 u16 dweek);

u32 savestate_slot = 0;
static void get_savestate_info(char *savestate_filename, u16 *savestate_snapshot,
 char *savestate_timestamp);
static void get_savestate_filename(u32 slot, char *name_buffer);

static s8 parse_msg_line(char *current_line, char *current_str);
static void replace_msg_line(char *msg_str);

static void save_ss_bmp(u16 *image);

u32 ALIGN_DATA gamepad_config_line_to_button[] = 
{
  8, 6, 7, 9, 1, 2, 3, 0, 4, 5, 11, 10
};

u32 clock_speed_number;

char dir_roms[MAX_PATH];
char dir_save[MAX_PATH];
char dir_cfg[MAX_PATH];
char dir_snap[MAX_PATH];
char dir_cheat[MAX_PATH];


// Blatantly stolen and trimmed from MZX (megazeux.sourceforge.net)

#define STATUS_ROWS        0
#define CURRENT_DIR_ROWS   0

#define FILE_LIST_ROWS     24
#define FILE_LIST_POS_X    15
#define FILE_LIST_POS_Y    5

#define DIR_LIST_POSITION  360

#define PAGE_SCROLL_NUM    5

#define MENU_LIST_POS_X    15


#define COLOR16(red, green, blue)                                             \
 (((blue) << 10) | ((green) << 5) | (red))                                    \

#define COLOR_BG            COLOR16( 2,  4, 10)
#define COLOR_ROM_INFO      COLOR16(22, 18, 26)
#define COLOR_ACTIVE_ITEM   COLOR16(31, 31, 31)
#define COLOR_INACTIVE_ITEM COLOR16(13, 20, 18)
#define COLOR_FRAMESKIP_BAR COLOR16(15, 15, 31)
#define COLOR_HELP_TEXT     COLOR16(16, 20, 24)
#define COLOR_INACTIVE_DIR  COLOR16(13, 22, 22)
#define COLOR_SCROLL_BAR    COLOR16( 4,  8, 12)
#define COLOR_BATT_EMPTY1   COLOR16(31,  5, 5)
#define COLOR_BATT_EMPTY2   COLOR16(31, 31, 5)

// scroll bar
#define SBAR_X1  1
#define SBAR_X2  10
#define SBAR_Y1  15
#define SBAR_Y2  255

#define SBAR_T   (SBAR_Y1 + 2)
#define SBAR_B   (SBAR_Y2 - 2)
#define SBAR_H   (SBAR_B - SBAR_T)
#define SBAR_X1I (SBAR_X1 + 2)
#define SBAR_X2I (SBAR_X2 - 2)
#define SBAR_Y1I                                                              \
 (SBAR_H * current_file_scroll_value / num_files + SBAR_T)
#define SBAR_Y2I                                                              \
 (SBAR_H * (current_file_scroll_value + FILE_LIST_ROWS) / num_files + SBAR_T)


void _flush_cache(void)
{
  invalidate_all_cache();
}


static int sort_function(const void *dest_str_ptr, const void *src_str_ptr)
{
  char *dest_str = *((char **)dest_str_ptr);
  char *src_str  = *((char **)src_str_ptr);

  if(src_str[0] == '.')
    return 1;

  if(dest_str[0] == '.')
    return -1;

  if(isalpha(src_str[0]) || isalpha(dest_str[0]))
    return strcasecmp(dest_str, src_str);
  else
    return strcmp(dest_str, src_str);
}

// 汎用ファイル読込み
s8 load_file(char **wildcards, char *result, char *default_dir_name)
{
  DIR *current_dir;
  struct dirent *current_file;
  struct stat file_info;
  char current_dir_name[MAX_PATH];
  char current_dir_short[81];
  u32 current_dir_length;
  u32 total_filenames_allocated;
  u32 total_dirnames_allocated;
  char **file_list;
  char **dir_list;
  u32 num_files;
  u32 num_dirs;
  char *file_name;
  u32 file_name_length;
  s32 ext_pos = -1;
  u32 chosen_file, chosen_dir;
//  u32 dialog_result = 1;
  s32 return_value = 1;
  u32 current_file_selection;
  u32 current_file_scroll_value;
  u32 current_dir_selection;
  u32 current_dir_scroll_value;
  u32 current_file_in_scroll;
  u32 current_dir_in_scroll;
  u32 current_file_number, current_dir_number;
  u32 current_column = 0;
  u32 repeat;
  u32 i;
  GUI_ACTION_TYPE gui_action;

  char time_str[80];
  char batt_str[80];
  u16 color_batt_life = COLOR_HELP_TEXT;
  u32 counter = 0;

  void filelist_term(void)
  {
    for(i = 0; i < num_files; i++)
    {
      free(file_list[i]);
    }
    free(file_list);

    for(i = 0; i < num_dirs; i++)
    {
      free(dir_list[i]);
    }
    free(dir_list);
  }


  if(default_dir_name != NULL)
  {
    chdir(default_dir_name);
  }

  while(return_value == 1)
  {
    current_file_selection    = 0;
    current_file_scroll_value = 0;
    current_dir_selection     = 0;
    current_dir_scroll_value  = 0;
    current_file_in_scroll    = 0;
    current_dir_in_scroll     = 0;

    total_filenames_allocated = 32;
    total_dirnames_allocated  = 32;
    file_list = (char **)malloc(sizeof(char *) * 32);
    dir_list  = (char **)malloc(sizeof(char *) * 32);
    memset(file_list, 0, sizeof(char *) * 32);
    memset(dir_list,  0, sizeof(char *) * 32);

    num_files   = 0;
    num_dirs    = 0;
    chosen_file = 0;
    chosen_dir  = 0;

    getcwd(current_dir_name, MAX_PATH);
    current_dir = opendir(current_dir_name);

    do
    {
      if(current_dir)
        current_file = readdir(current_dir);
      else
        current_file = NULL;

      if(current_file)
      {
        file_name = current_file->d_name;
        file_name_length = strlen(file_name);

        if((stat(file_name, &file_info) >= 0) &&
           ((file_name[0] != '.') || (file_name[1] == '.')))
        {
          if(S_ISDIR(file_info.st_mode))
          {
            dir_list[num_dirs] = (char *)malloc(file_name_length + 1);
            sprintf(dir_list[num_dirs], "%s", file_name);
            num_dirs++;
          }
          else
          {
            // Must match one of the wildcards, also ignore the .
            if(file_name_length >= 4)
            {
              if(file_name[file_name_length - 4] == '.')
                ext_pos = file_name_length - 4;
              else
              if(file_name[file_name_length - 3] == '.')
                ext_pos = file_name_length - 3;
              else
                ext_pos = 0;

              for(i = 0; wildcards[i] != NULL; i++)
              {
                if(!strcasecmp((file_name + ext_pos), wildcards[i]))
                {
                  file_list[num_files] = (char *)malloc(file_name_length + 1);
                  sprintf(file_list[num_files], "%s", file_name);
                  num_files++;
                  break;
                }
              }
            }
          }
        }

        if(num_files == total_filenames_allocated)
        {
          file_list = (char **)realloc(file_list,
           sizeof(char *) * total_filenames_allocated * 2);
          memset(file_list + total_filenames_allocated, 0,
           sizeof(u8 *) * total_filenames_allocated);
          total_filenames_allocated *= 2;
        }

        if(num_dirs == total_dirnames_allocated)
        {
          dir_list = (char **)realloc(dir_list,
           sizeof(char *) * total_dirnames_allocated * 2);
          memset(dir_list + total_dirnames_allocated, 0,
           sizeof(char *) * total_dirnames_allocated);
          total_dirnames_allocated *= 2;
        }
      }
    } while(current_file);

    qsort((void *)file_list, num_files, sizeof(u8 *), sort_function);
    qsort((void *)dir_list,  num_dirs,  sizeof(u8 *), sort_function);

    closedir(current_dir);

    current_dir_length = strlen(current_dir_name);

    if(current_dir_length > current_dir_name_length)
    {
      memcpy(current_dir_short, "...", 3);
      memcpy(current_dir_short + 3,
       current_dir_name + current_dir_length - (current_dir_name_length - 3),
       (current_dir_name_length - 3));
      current_dir_short[current_dir_name_length] = 0;
    }
    else
    {
      memcpy(current_dir_short, current_dir_name, current_dir_length + 1);
    }

    repeat = 1;

    if(num_files == 0)
      current_column = 1;

    while(repeat)
    {
      clear_screen(COLOR_BG);

      if((counter % 30) == 0)
        color_batt_life = update_status_str(time_str, batt_str);
      counter++;
      print_string(time_str, COLOR_HELP_TEXT, COLOR_BG, time_status_pos_x, 2);
      print_string(batt_str, color_batt_life, COLOR_BG, batt_status_pos_x, 2);

      print_string(current_dir_short, COLOR_ACTIVE_ITEM, COLOR_BG, 2, 2);
      print_string(msg[MSG_RETURN_MENU], COLOR_HELP_TEXT, COLOR_BG, 20, 260);

      if(num_files > FILE_LIST_ROWS)
      {
        // draw scroll bar
        draw_box_line(SBAR_X1,  SBAR_Y1,  SBAR_X2,  SBAR_Y2,  COLOR_SCROLL_BAR);
        draw_box_fill(SBAR_X1I, SBAR_Y1I, SBAR_X2I, SBAR_Y2I, COLOR_SCROLL_BAR);
      }

      for(i = 0, current_file_number = i + current_file_scroll_value;
       i < (FILE_LIST_ROWS - CURRENT_DIR_ROWS); i++, current_file_number++)
      {
        if(current_file_number < num_files)
        {
          if((current_file_number == current_file_selection) &&
             (current_column == 0))
          {
            print_string(file_list[current_file_number], COLOR_ACTIVE_ITEM,
             COLOR_BG, FILE_LIST_POS_X, ((i + CURRENT_DIR_ROWS + 1) * 10)
             + FILE_LIST_POS_Y);
          }
          else
          {
            print_string(file_list[current_file_number], COLOR_INACTIVE_ITEM,
             COLOR_BG, FILE_LIST_POS_X, ((i + CURRENT_DIR_ROWS + 1) * 10)
             + FILE_LIST_POS_Y);
          }
        }
      }

      for(i = 0, current_dir_number = i + current_dir_scroll_value;
       i < (FILE_LIST_ROWS - CURRENT_DIR_ROWS); i++, current_dir_number++)
      {
        if(current_dir_number < num_dirs)
        {
          if((current_dir_number == current_dir_selection) &&
             (current_column == 1))
          {
            print_string(dir_list[current_dir_number], COLOR_ACTIVE_ITEM,
             COLOR_BG, DIR_LIST_POSITION, ((i + CURRENT_DIR_ROWS + 1) * 10)
             + FILE_LIST_POS_Y);
          }
          else
          {
            print_string(dir_list[current_dir_number], COLOR_INACTIVE_DIR,
             COLOR_BG, DIR_LIST_POSITION, ((i + CURRENT_DIR_ROWS + 1) * 10)
             + FILE_LIST_POS_Y);
          }
        }
      }

      flip_screen(1);

      gui_action = get_gui_input();

      switch(gui_action)
      {
        case CURSOR_DOWN:
          if(current_column == 0)
          {
            if(current_file_selection < (num_files - 1))
            {
              current_file_selection++;
              if(current_file_in_scroll ==
                 (FILE_LIST_ROWS - CURRENT_DIR_ROWS - 1))
              {
                current_file_scroll_value++;
              }
              else
              {
                current_file_in_scroll++;
              }
            }
          }
          else
          {
            if(current_dir_selection < (num_dirs - 1))
            {
              current_dir_selection++;
              if(current_dir_in_scroll ==
                 (FILE_LIST_ROWS - CURRENT_DIR_ROWS - 1))
              {
                current_dir_scroll_value++;
              }
              else
              {
                current_dir_in_scroll++;
              }
            }
          }
          break;

        case CURSOR_RTRIGGER:
          if(current_column == 0)
          {
            if(num_files > PAGE_SCROLL_NUM)
            {
              if(current_file_selection < (num_files - PAGE_SCROLL_NUM))
              {
                current_file_selection += PAGE_SCROLL_NUM;
                if(current_file_in_scroll >=
                   (FILE_LIST_ROWS - CURRENT_DIR_ROWS - PAGE_SCROLL_NUM))
                {
                  current_file_scroll_value += PAGE_SCROLL_NUM;

                  if(current_file_scroll_value > (num_files - FILE_LIST_ROWS))
                  {
                    current_file_scroll_value = num_files - FILE_LIST_ROWS;
                    current_file_in_scroll = 
                     current_file_selection - current_file_scroll_value;
                  }
                }
                else
                {
                  current_file_in_scroll += PAGE_SCROLL_NUM;
                }
              }
            }
          }
          else
          {
            if(num_dirs > PAGE_SCROLL_NUM)
            {
              if(current_dir_selection < (num_dirs - PAGE_SCROLL_NUM))
              {
                current_dir_selection += PAGE_SCROLL_NUM;
                if(current_dir_in_scroll >=
                   (FILE_LIST_ROWS - CURRENT_DIR_ROWS - PAGE_SCROLL_NUM))
                {
                  current_dir_scroll_value += PAGE_SCROLL_NUM;

                  if(current_dir_scroll_value > (num_files - FILE_LIST_ROWS))
                  {
                    current_dir_scroll_value = num_files - FILE_LIST_ROWS;
                    current_dir_in_scroll = 
                     current_dir_selection - current_dir_scroll_value;
                  }
                }
                else
                {
                  current_dir_in_scroll += PAGE_SCROLL_NUM;
                }
              }
            }
          }
          break;

        case CURSOR_UP:
          if(current_column == 0)
          {
            if(current_file_selection)
            {
              current_file_selection--;

              if(current_file_in_scroll == 0)
                current_file_scroll_value--;
              else
                current_file_in_scroll--;
            }
          }
          else
          {
            if(current_dir_selection)
            {
              current_dir_selection--;

              if(current_dir_in_scroll == 0)
                current_dir_scroll_value--;
              else
                current_dir_in_scroll--;
            }
          }
          break;

        case CURSOR_LTRIGGER:
          if(current_column == 0)
          {
            if(current_file_selection >= PAGE_SCROLL_NUM)
            {
              current_file_selection -= PAGE_SCROLL_NUM;
              if(current_file_in_scroll < PAGE_SCROLL_NUM)
              {
                if(current_file_scroll_value >= PAGE_SCROLL_NUM)
                {
                  current_file_scroll_value -= PAGE_SCROLL_NUM;
                }
                else
                {
                  current_file_scroll_value = 0;
                  current_file_in_scroll = current_file_selection;
                }
              }
              else
              {
                current_file_in_scroll -= PAGE_SCROLL_NUM;
              }
            }
          }
          else
          {
            if(current_dir_selection >= PAGE_SCROLL_NUM)
            {
              current_dir_selection -= PAGE_SCROLL_NUM;
              if(current_dir_in_scroll < PAGE_SCROLL_NUM)
              {
                if(current_dir_scroll_value >= PAGE_SCROLL_NUM)
                {
                  current_dir_scroll_value -= PAGE_SCROLL_NUM;
                }
                else
                {
                  current_dir_scroll_value = 0;
                  current_dir_in_scroll = current_dir_selection;
                }
              }
              else
              {
                current_dir_in_scroll -= PAGE_SCROLL_NUM;
              }
            }
          }
          break;

        case CURSOR_RIGHT:
          if(current_column == 0)
          {
            if(num_dirs != 0)
              current_column = 1;
          }
          break;

        case CURSOR_LEFT:
          if(current_column == 1)
          {
            if(num_files != 0)
              current_column = 0;
          }
          break;

        case CURSOR_SELECT:
          if(current_column == 1)
          {
            repeat = 0;
            chdir(dir_list[current_dir_selection]);
          }
          else
          {
            if(num_files != 0)
            {
              repeat = 0;
              return_value = 0;
              strcpy(result, file_list[current_file_selection]);
            }
          }
          break;

        case CURSOR_BACK:
          if(!strcmp(current_dir_name, "ms0:/PSP"))
            break;

          repeat = 0;
          chdir("..");
          break;

        case CURSOR_EXIT:
          return_value = -1;
          repeat = 0;
          break;
      }

      if(quit_flag == 1)
      {
        filelist_term();
        quit();
      }
    } /* end while(repeat) */

    filelist_term();
  } /* end while(return_value == 1) */

  return return_value;
}

typedef enum
{
  NUMBER_SELECTION_OPTION = 0x01,
  STRING_SELECTION_OPTION = 0x02,
  SUBMENU_OPTION          = 0x04,
  ACTION_OPTION           = 0x08
} MENU_OPTION_TYPE_ENUM;

struct _menu_type
{
  void (* init_function)(void);
  void (* passive_function)(void);
  struct _menu_option_type *options;
  u32 num_options;
};

struct _menu_option_type
{
  void (* action_function)(void);
  void (* passive_function)(void);
  struct _menu_type *sub_menu;
  char *display_string;
  void *options;
  u32 *current_option;
  u32 num_options;
  char *help_string;
  u32 line_number;
  MENU_OPTION_TYPE_ENUM option_type;
};

typedef struct _menu_option_type menu_option_type;
typedef struct _menu_type menu_type;

#define MAKE_MENU(name, init_function, passive_function)                      \
  menu_type name##_menu =                                                     \
  {                                                                           \
    init_function,                                                            \
    passive_function,                                                         \
    name##_options,                                                           \
    sizeof(name##_options) / sizeof(menu_option_type)                         \
  }                                                                           \

#define GAMEPAD_CONFIG_OPTION(display_string, number)                         \
{                                                                             \
  NULL,                                                                       \
  menu_fix_gamepad_help,                                                      \
  NULL,                                                                       \
  display_string,                                                             \
  gamepad_config_buttons,                                                     \
  gamepad_config_map + gamepad_config_line_to_button[number],                 \
  sizeof(gamepad_config_buttons) / sizeof(gamepad_config_buttons[0]),         \
  gamepad_help[gamepad_config_map[                                            \
   gamepad_config_line_to_button[number]]],                                   \
  number + 1,                                                                 \
  STRING_SELECTION_OPTION                                                     \
}                                                                             \

#define ANALOG_CONFIG_OPTION(display_string, number)                          \
{                                                                             \
  NULL,                                                                       \
  menu_fix_gamepad_help,                                                      \
  NULL,                                                                       \
  display_string,                                                             \
  gamepad_config_buttons,                                                     \
  gamepad_config_map + number + 12,                                           \
  sizeof(gamepad_config_buttons) / sizeof(gamepad_config_buttons[0]),         \
  gamepad_help[gamepad_config_map[number + 12]],                              \
  number + 1,                                                                 \
  STRING_SELECTION_OPTION                                                     \
}                                                                             \

#define CHEAT_OPTION(number)                                                  \
{                                                                             \
  NULL,                                                                       \
  NULL,                                                                       \
  NULL,                                                                       \
  cheat_format_str[number],                                                   \
  enable_disable_options,                                                     \
  &(cheats[number].cheat_active),                                             \
  2,                                                                          \
  msg[MSG_CHEAT_MENU_HELP_0],                                                 \
  number + 1,                                                                 \
  STRING_SELECTION_OPTION                                                     \
}                                                                             \

#define ACTION_OPTION(action_function, passive_function, display_string,      \
 help_string, line_number)                                                    \
{                                                                             \
  action_function,                                                            \
  passive_function,                                                           \
  NULL,                                                                       \
  display_string,                                                             \
  NULL,                                                                       \
  NULL,                                                                       \
  0,                                                                          \
  help_string,                                                                \
  line_number,                                                                \
  ACTION_OPTION                                                               \
}                                                                             \

#define SUBMENU_OPTION(sub_menu, display_string, help_string, line_number)    \
{                                                                             \
  NULL,                                                                       \
  NULL,                                                                       \
  sub_menu,                                                                   \
  display_string,                                                             \
  NULL,                                                                       \
  NULL,                                                                       \
  sizeof(sub_menu) / sizeof(menu_option_type),                                \
  help_string,                                                                \
  line_number,                                                                \
  SUBMENU_OPTION                                                              \
}                                                                             \

#define SELECTION_OPTION(passive_function, display_string, options,           \
 option_ptr, num_options, help_string, line_number, type)                     \
{                                                                             \
  NULL,                                                                       \
  passive_function,                                                           \
  NULL,                                                                       \
  display_string,                                                             \
  options,                                                                    \
  option_ptr,                                                                 \
  num_options,                                                                \
  help_string,                                                                \
  line_number,                                                                \
  type                                                                        \
}                                                                             \

#define ACTION_SELECTION_OPTION(action_function, passive_function,            \
 display_string, options, option_ptr, num_options, help_string, line_number,  \
 type)                                                                        \
{                                                                             \
  action_function,                                                            \
  passive_function,                                                           \
  NULL,                                                                       \
  display_string,                                                             \
  options,                                                                    \
  option_ptr,                                                                 \
  num_options,                                                                \
  help_string,                                                                \
  line_number,                                                                \
  type | ACTION_OPTION                                                        \
}                                                                             \


#define STRING_SELECTION_OPTION(passive_function, display_string, options,    \
 option_ptr, num_options, help_string, line_number)                           \
  SELECTION_OPTION(passive_function, display_string, options,                 \
   option_ptr, num_options, help_string, line_number, STRING_SELECTION_OPTION)\

#define NUMERIC_SELECTION_OPTION(passive_function, display_string,            \
 option_ptr, num_options, help_string, line_number)                           \
  SELECTION_OPTION(passive_function, display_string, NULL, option_ptr,        \
   num_options, help_string, line_number, NUMBER_SELECTION_OPTION)            \

#define STRING_SELECTION_ACTION_OPTION(action_function, passive_function,     \
 display_string, options, option_ptr, num_options, help_string, line_number)  \
  ACTION_SELECTION_OPTION(action_function, passive_function,                  \
   display_string,  options, option_ptr, num_options, help_string,            \
   line_number, STRING_SELECTION_OPTION)                                      \

#define NUMERIC_SELECTION_ACTION_OPTION(action_function, passive_function,    \
 display_string, option_ptr, num_options, help_string, line_number)           \
  ACTION_SELECTION_OPTION(action_function, passive_function,                  \
   display_string,  NULL, option_ptr, num_options, help_string,               \
   line_number, NUMBER_SELECTION_OPTION)                                      \

#define NUMERIC_SELECTION_ACTION_HIDE_OPTION(action_function,                 \
 passive_function, display_string, option_ptr, num_options, help_string,      \
 line_number)                                                                 \
  ACTION_SELECTION_OPTION(action_function, passive_function,                  \
   display_string, NULL, option_ptr, num_options, help_string,                \
   line_number, NUMBER_SELECTION_OPTION)                                      \


s8 load_game_config_file(void)
{
  FILE_TAG_TYPE game_config_file;
  char game_config_filename[MAX_FILE];
  char game_config_path[MAX_PATH];
  u32 i;

  change_ext(gamepak_filename, game_config_filename, ".cfg");
  sprintf(game_config_path, "%s/%s", dir_cfg, game_config_filename);

  FILE_OPEN(game_config_file, game_config_path, READ);

  if(FILE_CHECK_VALID(game_config_file))
  {
    u32 file_size = file_length(game_config_path);

    // Sanity check: File size must be the right size
    if(file_size == 56)
    {
      u32 file_options[file_size / 4];

      FILE_READ_ARRAY(game_config_file, file_options);

      option_frameskip_type  = file_options[0] % 3;
      option_frameskip_value = file_options[1];
      option_random_skip = file_options[2] % 2;
      option_clock_speed = file_options[3];

      if(option_clock_speed > 333)
        option_clock_speed = 333;

      if(option_clock_speed < 33)
        option_clock_speed = 33;

      clock_speed_number = (option_clock_speed / 33) - 1;

      if(option_frameskip_value > 99)
        option_frameskip_value = 99;

      for(i = 0; i < 10; i++)
      {
        cheats[i].cheat_active = file_options[3 + i] % 2;
        cheats[i].cheat_name[0] = 0;
      }

      FILE_CLOSE(game_config_file);

      return 0;
    }
  }

  option_frameskip_type = AUTO_FRAMESKIP;
  option_frameskip_value = 9;
  option_random_skip = 0;
  option_clock_speed = 333;
  clock_speed_number = 9;

  for(i = 0; i < 10; i++)
  {
    cheats[i].cheat_active = 0;
    cheats[i].cheat_name[0] = 0;
  }

  return -1;
}

s8 load_config_file(void)
{
  FILE_TAG_TYPE config_file;
  char config_path[MAX_PATH];

  sprintf(config_path, "%s/%s", main_path, GPSP_CONFIG_FILENAME);

  FILE_OPEN(config_file, config_path, READ);

  if(FILE_CHECK_VALID(config_file))
  {
    u32 file_size = file_length(config_path);

    // Sanity check: File size must be the right size
    if(file_size == (23 * 4))
    {
      u32 file_options[file_size / 4];
      u32 i;
      FILE_READ_ARRAY(config_file, file_options);

      option_screen_scale  = file_options[0] % 4;
      option_screen_mag    = file_options[1] % 201;
      option_screen_filter = file_options[2] % 2;
      option_enable_audio  = file_options[3] % 2;
      option_update_backup = file_options[4] % 2;
      option_enable_analog = file_options[5] % 2;
      option_analog_sensitivity = file_options[6] % 10;

      for(i = 0; i < 16; i++)
        gamepad_config_map[i] = file_options[7 + i] % (BUTTON_ID_NONE + 1);

      FILE_CLOSE(config_file);
    }

    return 0;
  }

  return -1;
}

static s8 save_game_config_file(void)
{
  FILE_TAG_TYPE game_config_file;
  char game_config_filename[MAX_FILE];
  char game_config_path[MAX_PATH];
  u32 i;

  if(gamepak_filename[0] == 0)
  {
    return -1;
  }

  change_ext(gamepak_filename, game_config_filename, ".cfg");
  sprintf(game_config_path, "%s/%s", dir_cfg, game_config_filename);

  FILE_OPEN(game_config_file, game_config_path, WRITE);

  if(FILE_CHECK_VALID(game_config_file))
  {
    u32 file_options[14];

    file_options[0] = option_frameskip_type;
    file_options[1] = option_frameskip_value;
    file_options[2] = option_random_skip;
    file_options[3] = option_clock_speed;

    for(i = 0; i < 10; i++)
    {
      file_options[4 + i] = cheats[i].cheat_active;
    }

    FILE_WRITE_ARRAY(game_config_file, file_options);
    FILE_CLOSE(game_config_file);

    return 0;
  }

  return -1;
}

s8 save_config_file(void)
{
  FILE_TAG_TYPE config_file;
  char config_path[MAX_PATH];

  sprintf(config_path, "%s/%s", main_path, GPSP_CONFIG_FILENAME);

  FILE_OPEN(config_file, config_path, WRITE);

  save_game_config_file();

  if(FILE_CHECK_VALID(config_file))
  {
    u32 file_options[23];
    u32 i;

    file_options[0] = option_screen_scale;
    file_options[1] = option_screen_mag;
    file_options[2] = option_screen_filter;
    file_options[3] = option_enable_audio;
    file_options[4] = option_update_backup;
    file_options[5] = option_enable_analog;
    file_options[6] = option_analog_sensitivity;

    for(i = 0; i < 16; i++)
    {
      file_options[7 + i] = gamepad_config_map[i];
    }

    FILE_WRITE_ARRAY(config_file, file_options);
    FILE_CLOSE(config_file);

    return 0;
  }

  return -1;
}


static void get_savestate_info(char *savestate_filename, u16 *savestate_snapshot,
 char *savestate_timestamp)
{
  FILE_TAG_TYPE savestate_file;
  char savestate_path[MAX_PATH];

  sprintf(savestate_path, "%s/%s", dir_save, savestate_filename);

  FILE_OPEN(savestate_file, savestate_path, READ);

  if(FILE_CHECK_VALID(savestate_file))
  {
    u64 savestate_tick_utc;
    u64 savestate_tick_local;

    pspTime savestate_time;
    u16 savestate_dweek;

    FILE_READ(savestate_file, savestate_snapshot, (240 * 160 * 2));
    FILE_READ_VARIABLE(savestate_file, savestate_tick_utc);

    FILE_CLOSE(savestate_file);

    sceRtcConvertUtcToLocalTime(&savestate_tick_utc, &savestate_tick_local);
    sceRtcSetTick(&savestate_time, &savestate_tick_local);

    savestate_dweek = sceRtcGetDayOfWeek(savestate_time.year,
     savestate_time.month, savestate_time.day);

    get_timestamp_string(savestate_timestamp, MSG_STATE_MENU_DATE_FMT_0,
     savestate_time, savestate_dweek);

    savestate_timestamp[40] = 0;
  }
  else
  {
    memset(savestate_snapshot, 0, (240 * 160 * 2));
    print_string_ext(msg[MSG_STATE_MENU_STATE_NONE], 0xFFFF, 0x0000, 15, 75,
     savestate_snapshot, 240, 0);

    pspTime savestate_time_null = { 0 };

    get_timestamp_string(savestate_timestamp, MSG_STATE_MENU_DATE_NONE_0,
     savestate_time_null, 7);
  }
}

static void get_savestate_filename(u32 slot, char *name_buffer)
{
  char savestate_ext[16];

  sprintf(savestate_ext, "_%d.svs", (int)slot);
  change_ext(gamepak_filename, name_buffer, savestate_ext);
}


void action_loadstate(void)
{
  char current_savestate_filename[MAX_FILE];

  get_savestate_filename(savestate_slot, current_savestate_filename);
  load_state(current_savestate_filename);
}

void action_savestate(void)
{
  char current_savestate_filename[MAX_FILE];
  u16 *current_screen = copy_screen();

  get_savestate_filename(savestate_slot, current_savestate_filename);
  save_state(current_savestate_filename, current_screen);

  free(current_screen);
}


u8 menu(void)
{
  u32 i;

  u32 repeat = 1;
  u32 return_value = 0;
  u32 first_load = 0;

  GUI_ACTION_TYPE gui_action;
  SceCtrlData ctrl_data;

  u16 *screen_image_ptr;
  u16 *current_screen = copy_screen();
  u16 *savestate_screen = malloc(240 * 160 * 2);

  u32 current_savestate_slot = 0xFF;
  char current_savestate_timestamp[80];
  char current_savestate_filename[MAX_FILE];

  char game_title[MAX_FILE];

  char time_str[80];
  char batt_str[80];
  u16 color_batt_life = COLOR_HELP_TEXT;
  u32 counter = 0;

  char line_buffer[80];
  char cheat_format_str[10][41];

  menu_type *current_menu;
  menu_option_type *current_option;
  menu_option_type *display_option;
  u32 current_option_num;

  auto void choose_menu(menu_type *new_menu);
  auto void menu_term(void);
  auto void menu_exit(void);
  auto void menu_quit(void);
  auto void menu_load(void);
  auto void menu_restart(void);
  auto void menu_save_ss(void);
  auto void menu_change_state(void);
  auto void menu_save_state(void);
  auto void menu_load_state(void);
  auto void menu_load_state_file(void);
  auto void menu_load_cheat_file(void);
  auto void menu_fix_gamepad_help(void);
  auto void submenu_emulator(void);
  auto void submenu_cheats_misc(void);
  auto void submenu_gamepad(void);
  auto void submenu_analog(void);
  auto void submenu_savestate(void);
  auto void submenu_main(void);

  char *gamepad_help[] =
  {
    msg[MSG_PAD_MENU_CFG_HELP_0],
    msg[MSG_PAD_MENU_CFG_HELP_1],
    msg[MSG_PAD_MENU_CFG_HELP_2],
    msg[MSG_PAD_MENU_CFG_HELP_3],
    msg[MSG_PAD_MENU_CFG_HELP_4],
    msg[MSG_PAD_MENU_CFG_HELP_5],
    msg[MSG_PAD_MENU_CFG_HELP_6],
    msg[MSG_PAD_MENU_CFG_HELP_7],
    msg[MSG_PAD_MENU_CFG_HELP_8],
    msg[MSG_PAD_MENU_CFG_HELP_9],
    msg[MSG_PAD_MENU_CFG_HELP_10],
    msg[MSG_PAD_MENU_CFG_HELP_11],
    msg[MSG_PAD_MENU_CFG_HELP_12],
    msg[MSG_PAD_MENU_CFG_HELP_13],
    msg[MSG_PAD_MENU_CFG_HELP_14],
    msg[MSG_PAD_MENU_CFG_HELP_15],
    msg[MSG_PAD_MENU_CFG_HELP_16],
    msg[MSG_PAD_MENU_CFG_HELP_17],
    msg[MSG_PAD_MENU_CFG_HELP_18],
    msg[MSG_PAD_MENU_CFG_HELP_19]
  };

  void menu_term(void)
  {
    screen_image_ptr = NULL;

    if(savestate_screen != NULL)
    {
      free(savestate_screen);
      savestate_screen = NULL;
    }

    if(current_screen != NULL)
    {
      free(current_screen);
      current_screen = NULL;
    }
  }

  void menu_exit(void)
  {
    if(!first_load)
      repeat = 0;
  }

  void menu_quit(void)
  {
    menu_term();
    quit();
  }

  void menu_load(void)
  {
    char *file_ext[] = { ".gba", ".bin", ".agb", ".zip", ".gbz", NULL };
    char load_filename[MAX_FILE];

    save_game_config_file();
    update_backup_force();

    if(load_file(file_ext, load_filename, dir_roms) != -1)
    {
      if(load_gamepak(load_filename) == -1)
      {
        menu_quit();
      }
      reset_gba();
      reg[CHANGED_PC_STATUS] = 1;

      return_value = 1;
      repeat = 0;
    }
    else
    {
      choose_menu(current_menu);
    }
  }

  void menu_restart(void)
  {
    if(!first_load)
    {
      reset_gba();
      reg[CHANGED_PC_STATUS] = 1;

      return_value = 1;
      repeat = 0;
    }
  }

  void menu_save_ss(void)
  {
    if(!first_load)
      save_ss_bmp(current_screen);
  }

  void menu_change_state(void)
  {
    if(savestate_slot != current_savestate_slot)
    {
      get_savestate_filename(savestate_slot, current_savestate_filename);
      get_savestate_info(current_savestate_filename, savestate_screen,
       current_savestate_timestamp);
      current_savestate_slot = savestate_slot;
    }
    screen_image_ptr = savestate_screen;
    print_string(current_savestate_timestamp, COLOR_HELP_TEXT, COLOR_BG,
     MENU_LIST_POS_X, 50);
  }

  void menu_save_state(void)
  {
    menu_change_state();

    if(!first_load)
    {
      get_savestate_filename(savestate_slot, current_savestate_filename);
      save_state(current_savestate_filename, current_screen);
      current_savestate_slot = 0xFF;
    }
  }

  void menu_load_state(void)
  {
    menu_change_state();

    if(!first_load)
    {
      load_state(current_savestate_filename);
      return_value = 1;
      repeat = 0;
    }
  }

  void menu_load_state_file(void)
  {
    char *file_ext[] = { ".svs", NULL };
    char load_filename[MAX_FILE];

    if(load_file(file_ext, load_filename, dir_save) != -1)
    {
      load_state(load_filename);
      return_value = 1;
      repeat = 0;
    }
    else
    {
      choose_menu(current_menu);
    }
  }

  void menu_load_cheat_file(void)
  {
    char *file_ext[] = { ".cht", NULL };
    char load_filename[MAX_FILE];

    if(load_file(file_ext, load_filename, dir_cheat) != -1)
    {
      add_cheats(load_filename);
      for(i = 0; i < MAX_CHEATS; i++)
      {
        if(i >= num_cheats)
        {
          sprintf(cheat_format_str[i], msg[MSG_CHEAT_MENU_NON_LOAD], i);
        }
        else
        {
          sprintf(cheat_format_str[i], msg[MSG_CHEAT_MENU_0], i,
           cheats[i].cheat_name);
        }
      }
      choose_menu(current_menu);
    }
    else
    {
      choose_menu(current_menu);
    }
  }

  void menu_fix_gamepad_help(void)
  {
    current_option->help_string =
     gamepad_help[gamepad_config_map[
     gamepad_config_line_to_button[current_option_num]]];
  }

  #define DRAW_TITLE(title)                                                   \
   print_string(msg[title], COLOR_HELP_TEXT, COLOR_BG, MENU_LIST_POS_X, 20)

  void submenu_emulator(void)
  {
    DRAW_TITLE(MSG_OPTION_MENU_TITLE);
  }

  void submenu_cheats_misc(void)
  {
    DRAW_TITLE(MSG_CHEAT_MENU_TITLE);
  }

  void submenu_gamepad(void)
  {
    DRAW_TITLE(MSG_PAD_MENU_TITLE);
  }

  void submenu_analog(void)
  {
    DRAW_TITLE(MSG_A_PAD_MENU_TITLE);
  }

  void submenu_savestate(void)
  {
    DRAW_TITLE(MSG_STATE_MENU_TITLE);
    menu_change_state();
  }

  void submenu_main(void)
  {
    DRAW_TITLE(MSG_MAIN_MENU_TITLE);
    get_savestate_filename(savestate_slot, current_savestate_filename);
    screen_image_ptr = current_screen;
  }

  char *yes_no_options[] =
  {
    msg[MSG_NO],
    msg[MSG_YES]
  };

  char *enable_disable_options[] =
  {
    msg[MSG_DISABLED],
    msg[MSG_ENABLED]
  };

  char *scale_options[] =
  {
    msg[MSG_SCN_UNSCALED],
    msg[MSG_SCN_SCALED_X15_GU],
    msg[MSG_SCN_SCALED_X15_SW],
    msg[MSG_SCN_SCALED_USER]
  };

  char *frameskip_options[] =
  {
    msg[MSG_FS_AUTO],
    msg[MSG_FS_MANUAL],
    msg[MSG_FS_OFF]
  };

  char *frameskip_variation_options[] =
  {
    msg[MSG_FS_UNIFORM],
    msg[MSG_FS_RANDOM]
  };

  char *update_backup_options[] =
  {
    msg[MSG_BK_EXITONLY],
    msg[MSG_BK_AUTO]
  };

  char *clock_speed_options[] =
  {
    "33MHz",  "66MHz",  "100MHz", "133MHz", "166MHz",
    "200MHz", "233MHz", "266MHz", "300MHz", "333MHz"
  };

  char *gamepad_config_buttons[] =
  {
    msg[MSG_PAD_MENU_CFG_0],
    msg[MSG_PAD_MENU_CFG_1],
    msg[MSG_PAD_MENU_CFG_2],
    msg[MSG_PAD_MENU_CFG_3],
    msg[MSG_PAD_MENU_CFG_4],
    msg[MSG_PAD_MENU_CFG_5],
    msg[MSG_PAD_MENU_CFG_6],
    msg[MSG_PAD_MENU_CFG_7],
    msg[MSG_PAD_MENU_CFG_8],
    msg[MSG_PAD_MENU_CFG_9],
    msg[MSG_PAD_MENU_CFG_10],
    msg[MSG_PAD_MENU_CFG_11],
    msg[MSG_PAD_MENU_CFG_12],
    msg[MSG_PAD_MENU_CFG_13],
    msg[MSG_PAD_MENU_CFG_14],
    msg[MSG_PAD_MENU_CFG_15],
    msg[MSG_PAD_MENU_CFG_16],
    msg[MSG_PAD_MENU_CFG_17],
    msg[MSG_PAD_MENU_CFG_18],
    msg[MSG_PAD_MENU_CFG_19]
  };

  // Marker for help information, don't go past this mark (except \n)------*
  menu_option_type emulator_options[] =
  {
    STRING_SELECTION_OPTION(NULL, msg[MSG_OPTION_MENU_0], scale_options, &option_screen_scale, 4, msg[MSG_OPTION_MENU_HELP_0], 0),

    NUMERIC_SELECTION_OPTION(NULL, msg[MSG_OPTION_MENU_1], &option_screen_mag, 201, msg[MSG_OPTION_MENU_HELP_1], 1),

    STRING_SELECTION_OPTION(NULL, msg[MSG_OPTION_MENU_2], yes_no_options, &option_screen_filter, 2, msg[MSG_OPTION_MENU_HELP_2], 2),

    STRING_SELECTION_OPTION(NULL, msg[MSG_OPTION_MENU_3], frameskip_options, &option_frameskip_type, 3, msg[MSG_OPTION_MENU_HELP_3], 4),

    NUMERIC_SELECTION_OPTION(NULL, msg[MSG_OPTION_MENU_4], &option_frameskip_value, 100, msg[MSG_OPTION_MENU_HELP_4], 5),

    STRING_SELECTION_OPTION(NULL, msg[MSG_OPTION_MENU_5], frameskip_variation_options, &option_random_skip, 2, msg[MSG_OPTION_MENU_HELP_5], 6),

    STRING_SELECTION_OPTION(NULL, msg[MSG_OPTION_MENU_6], yes_no_options, &option_enable_audio, 2, msg[MSG_OPTION_MENU_HELP_6], 8),

    STRING_SELECTION_OPTION(NULL, msg[MSG_OPTION_MENU_7], clock_speed_options, &clock_speed_number, 10, msg[MSG_OPTION_MENU_HELP_7], 10), 

    STRING_SELECTION_OPTION(NULL, msg[MSG_OPTION_MENU_8], update_backup_options, &option_update_backup, 2, msg[MSG_OPTION_MENU_HELP_8], 12), 

    SUBMENU_OPTION(NULL, msg[MSG_OPTION_MENU_9], msg[MSG_OPTION_MENU_HELP_9], 14)
  };

  MAKE_MENU(emulator, submenu_emulator, NULL);

  menu_option_type cheats_misc_options[] =
  {
    CHEAT_OPTION(0),
    CHEAT_OPTION(1),
    CHEAT_OPTION(2),
    CHEAT_OPTION(3),
    CHEAT_OPTION(4),
    CHEAT_OPTION(5),
    CHEAT_OPTION(6),
    CHEAT_OPTION(7),
    CHEAT_OPTION(8),
    CHEAT_OPTION(9),

    ACTION_OPTION(menu_load_cheat_file, NULL, msg[MSG_CHEAT_MENU_1], msg[MSG_CHEAT_MENU_HELP_1], 12), 

    SUBMENU_OPTION(NULL, msg[MSG_CHEAT_MENU_2], msg[MSG_CHEAT_MENU_HELP_2], 14)
  };

  MAKE_MENU(cheats_misc, submenu_cheats_misc, NULL);

  menu_option_type savestate_options[] =
  {
    NUMERIC_SELECTION_ACTION_HIDE_OPTION(menu_load_state, menu_change_state, msg[MSG_STATE_MENU_0], &savestate_slot, 10, msg[MSG_STATE_MENU_HELP_0], 4),

    NUMERIC_SELECTION_ACTION_HIDE_OPTION(menu_save_state, menu_change_state, msg[MSG_STATE_MENU_1], &savestate_slot, 10, msg[MSG_STATE_MENU_HELP_1], 5),

    NUMERIC_SELECTION_ACTION_HIDE_OPTION(menu_load_state_file, menu_change_state, msg[MSG_STATE_MENU_2], &savestate_slot, 10, msg[MSG_STATE_MENU_HELP_2], 7),

    NUMERIC_SELECTION_OPTION(menu_change_state, msg[MSG_STATE_MENU_3], &savestate_slot, 10, msg[MSG_STATE_MENU_HELP_3], 9),

    SUBMENU_OPTION(NULL, msg[MSG_STATE_MENU_4], msg[MSG_STATE_MENU_HELP_4], 11)
  };

  MAKE_MENU(savestate, submenu_savestate, NULL);

  menu_option_type gamepad_config_options[] =
  {
    GAMEPAD_CONFIG_OPTION(msg[MSG_PAD_MENU_0], 0),
    GAMEPAD_CONFIG_OPTION(msg[MSG_PAD_MENU_1], 1),
    GAMEPAD_CONFIG_OPTION(msg[MSG_PAD_MENU_2], 2),
    GAMEPAD_CONFIG_OPTION(msg[MSG_PAD_MENU_3], 3),
    GAMEPAD_CONFIG_OPTION(msg[MSG_PAD_MENU_4], 4),
    GAMEPAD_CONFIG_OPTION(msg[MSG_PAD_MENU_5], 5),
    GAMEPAD_CONFIG_OPTION(msg[MSG_PAD_MENU_6], 6),
    GAMEPAD_CONFIG_OPTION(msg[MSG_PAD_MENU_7], 7),
    GAMEPAD_CONFIG_OPTION(msg[MSG_PAD_MENU_8], 8),
    GAMEPAD_CONFIG_OPTION(msg[MSG_PAD_MENU_9], 9),
    GAMEPAD_CONFIG_OPTION(msg[MSG_PAD_MENU_10], 10),
    GAMEPAD_CONFIG_OPTION(msg[MSG_PAD_MENU_11], 11),

    SUBMENU_OPTION(NULL, msg[MSG_PAD_MENU_12], msg[MSG_PAD_MENU_HELP_0], 14)
  };

  MAKE_MENU(gamepad_config, submenu_gamepad, NULL);

  menu_option_type analog_config_options[] =
  {
    ANALOG_CONFIG_OPTION(msg[MSG_A_PAD_MENU_0], 0),
    ANALOG_CONFIG_OPTION(msg[MSG_A_PAD_MENU_1], 1),
    ANALOG_CONFIG_OPTION(msg[MSG_A_PAD_MENU_2], 2),
    ANALOG_CONFIG_OPTION(msg[MSG_A_PAD_MENU_3], 3),

    STRING_SELECTION_OPTION(NULL, msg[MSG_A_PAD_MENU_4], yes_no_options, &option_enable_analog, 2, msg[MSG_A_PAD_MENU_HELP_0], 6),
    NUMERIC_SELECTION_OPTION(NULL, msg[MSG_A_PAD_MENU_5], &option_analog_sensitivity, 10, msg[MSG_A_PAD_MENU_HELP_1], 7),
    SUBMENU_OPTION(NULL, msg[MSG_A_PAD_MENU_6], msg[MSG_A_PAD_MENU_HELP_2], 9)
  };

  MAKE_MENU(analog_config, submenu_analog, NULL);

  menu_option_type main_options[] =
  {
    NUMERIC_SELECTION_ACTION_OPTION(menu_load_state, NULL, msg[MSG_MAIN_MENU_0], &savestate_slot, 10, msg[MSG_MAIN_MENU_HELP_0], 0),

    NUMERIC_SELECTION_ACTION_OPTION(menu_save_state, NULL, msg[MSG_MAIN_MENU_1], &savestate_slot, 10, msg[MSG_MAIN_MENU_HELP_1], 1),

    SUBMENU_OPTION(&savestate_menu, msg[MSG_MAIN_MENU_2], msg[MSG_MAIN_MENU_HELP_2], 2),

    ACTION_OPTION(menu_save_ss, NULL, msg[MSG_MAIN_MENU_3], msg[MSG_MAIN_MENU_HELP_3], 4),

    SUBMENU_OPTION(&emulator_menu, msg[MSG_MAIN_MENU_4], msg[MSG_MAIN_MENU_HELP_4], 6), 

    SUBMENU_OPTION(&gamepad_config_menu, msg[MSG_MAIN_MENU_5], msg[MSG_MAIN_MENU_HELP_5], 7),

    SUBMENU_OPTION(&analog_config_menu, msg[MSG_MAIN_MENU_6], msg[MSG_MAIN_MENU_HELP_6], 8),

    SUBMENU_OPTION(&cheats_misc_menu, msg[MSG_MAIN_MENU_7], msg[MSG_MAIN_MENU_HELP_7], 9),

    ACTION_OPTION(menu_load, NULL, msg[MSG_MAIN_MENU_8], msg[MSG_MAIN_MENU_HELP_8], 11),

    ACTION_OPTION(menu_restart, NULL, msg[MSG_MAIN_MENU_9], msg[MSG_MAIN_MENU_HELP_9], 12),

    ACTION_OPTION(menu_exit, NULL, msg[MSG_MAIN_MENU_10], msg[MSG_MAIN_MENU_HELP_10], 13),

    ACTION_OPTION(menu_quit, NULL, msg[MSG_MAIN_MENU_11], msg[MSG_MAIN_MENU_HELP_11], 15)
  };

  MAKE_MENU(main, submenu_main, NULL);

  void choose_menu(menu_type *new_menu)
  {
    if(new_menu == NULL)
    {
      new_menu = &main_menu;
    }
    current_menu = new_menu;
    current_option = new_menu->options;
    current_option_num = 0;
  }


  video_resolution_large();

  clock_speed_number = (option_clock_speed / 33) - 1;
  set_cpu_clock(222);

  game_title[0] = 0;

  if(gamepak_filename[0] == 0)
  {
    first_load = 1;
    memset(current_screen, 0x00, (240 * 160 * 2));
    print_string_ext(msg[MSG_NON_LOAD_GAME], 0xFFFF, 0x0000, 60, 75,
     current_screen, 240, 0);
  }
  else
  {
    change_ext(gamepak_filename, game_title, "");
  }

  if(FILE_CHECK_VALID(gamepak_file_large))
  {
    FILE_CLOSE(gamepak_file_large);
    gamepak_file_large = -2;
  }

  for(i = 0; i < MAX_CHEATS; i++)
  {
    if(i >= num_cheats)
    {
      sprintf(cheat_format_str[i], msg[MSG_CHEAT_MENU_NON_LOAD], i);
    }
    else
    {
      sprintf(cheat_format_str[i], msg[MSG_CHEAT_MENU_0], i,
       cheats[i].cheat_name);
    }
  }

  choose_menu(&main_menu);

  while(repeat)
  {
    clear_screen(COLOR_BG);

    if((counter % 30) == 0)
      color_batt_life = update_status_str(time_str, batt_str);
    counter++;
    print_string(time_str, COLOR_HELP_TEXT, COLOR_BG, time_status_pos_x, 2);
    print_string(batt_str, color_batt_life, COLOR_BG, batt_status_pos_x, 2);

    if(current_menu->init_function)
    {
      current_menu->init_function();
    }

    display_option = current_menu->options;

    for(i = 0; i < current_menu->num_options; i++, display_option++)
    {
      if(display_option->option_type & NUMBER_SELECTION_OPTION)
      {
        sprintf(line_buffer, display_option->display_string,
         *(display_option->current_option));
      }
      else
      {
        if(display_option->option_type & STRING_SELECTION_OPTION)
        {
          sprintf(line_buffer, display_option->display_string,
           ((u32 *)display_option->options)[*(display_option->current_option)]);
        }
        else
        {
          strcpy(line_buffer, display_option->display_string);
        }
      }

      if(display_option == current_option)
      {
        print_string(line_buffer, COLOR_ACTIVE_ITEM, COLOR_BG,
         MENU_LIST_POS_X, (display_option->line_number * 10) + 40);
      }
      else
      {
        print_string(line_buffer, COLOR_INACTIVE_ITEM, COLOR_BG,
         MENU_LIST_POS_X, (display_option->line_number * 10) + 40);
      }
    }

    print_string(game_title, COLOR_HELP_TEXT, COLOR_BG, 230, 20);
    blit_to_screen(screen_image_ptr, 240, 160, 230, 40);

    print_string(current_option->help_string, COLOR_HELP_TEXT, COLOR_BG, 30, 210);

    flip_screen(1);

    gui_action = get_gui_input();

    switch(gui_action)
    {
      case CURSOR_DOWN:
        current_option_num
         = (current_option_num + 1) % current_menu->num_options;

        current_option = current_menu->options + current_option_num;
        break;

      case CURSOR_UP:
        if(current_option_num)
          current_option_num--;
        else
          current_option_num = current_menu->num_options - 1;

        current_option = current_menu->options + current_option_num;
        break;

      case CURSOR_RIGHT:
        if(current_option->option_type &
           (NUMBER_SELECTION_OPTION | STRING_SELECTION_OPTION))
        {
          *(current_option->current_option) =
           (*current_option->current_option + 1) % current_option->num_options;

          if(current_option->passive_function)
            current_option->passive_function();
        }
        break;

      case CURSOR_LEFT:
        if(current_option->option_type &
           (NUMBER_SELECTION_OPTION | STRING_SELECTION_OPTION))
        {
          u32 current_option_val = *(current_option->current_option);

          if(current_option_val)
            current_option_val--;
          else
            current_option_val = current_option->num_options - 1;

          *(current_option->current_option) = current_option_val;

          if(current_option->passive_function)
            current_option->passive_function();
        }
        break;

      case CURSOR_RTRIGGER:
        if(current_menu == &main_menu)
          choose_menu(&savestate_menu);
        break;

      case CURSOR_LTRIGGER:
        if(current_menu == &main_menu)
          menu_load();
        break;

      case CURSOR_EXIT:
        if(current_menu == &main_menu)
          menu_exit();
        else
          choose_menu(&main_menu);
        break;

      case CURSOR_SELECT:
        if(current_option->option_type & ACTION_OPTION)
          current_option->action_function();
        else
        if(current_option->option_type & SUBMENU_OPTION)
          choose_menu(current_option->sub_menu);
        break;
    }

    if(quit_flag == 1)
    {
      menu_quit();
    }
  } /* end while */

// menu終了時の処理

  menu_term();

  if(gamepak_file_large == -2)
  {
    i = 5;

    while(i--)
    {
      FILE_OPEN(gamepak_file_large, gamepak_filename_raw, READ);

      if(gamepak_file_large < 0)
        sceKernelDelayThread(500000);
      else
        goto success_open_gamepak;
    }

    clear_screen(0x0000);
    error_msg("Could not open gamepak.\n\nPress any button to exit.");
    quit();

    success_open_gamepak:;
  }

  while(sceCtrlPeekBufferPositive(&ctrl_data, 1), ctrl_data.Buttons != 0);

  option_clock_speed = ((clock_speed_number + 1) * 333) / 10;
  set_cpu_clock(option_clock_speed);

  video_resolution_small();

  return return_value;
}


#define ADD_LAUNCH_DIRECTORY()                                                \
  if(strchr(current_value, ':') == NULL)                                      \
  {                                                                           \
    char sbuf[MAX_FILE];                                                      \
                                                                              \
    strcpy(sbuf, current_value);                                              \
    sprintf(current_value, "%s/%s", main_path, sbuf);                         \
  }                                                                           \

#define SET_DIRECTORY(name)                                                   \
  if((dir_ptr = opendir(current_value)) != NULL)                              \
  {                                                                           \
    strcpy(dir_##name, current_value);                                        \
    closedir(dir_ptr);                                                        \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    sprintf(error_str, "Could not open [%s] : %s", current_variable,          \
     current_value);                                                          \
    print_string(error_str, 0xFFFF, 0x0000, 5, error_str_line);               \
    error_str_line += 10;                                                     \
  }                                                                           \

s8 load_dir_cfg(char *file_name)
{
  char current_line[256];
  char current_variable[256];
  char current_value[256];

  FILE *dir_cfg_ptr;
  DIR  *dir_ptr;

  char error_str[256];
  u16 error_str_line = 5;

  // set launch directory
  strcpy(dir_roms,  main_path);
  strcpy(dir_save,  main_path);
  strcpy(dir_cfg,   main_path);
  strcpy(dir_snap,  main_path);
  strcpy(dir_cheat, main_path);

  dir_cfg_ptr = fopen(file_name, "r");

  if(dir_cfg_ptr)
  {
    while(fgets(current_line, 256, dir_cfg_ptr))
    {
      if(parse_config_line(current_line, current_variable, current_value) != -1)
      {
        ADD_LAUNCH_DIRECTORY();

        if(!strcasecmp(current_variable, "rom_directory"))
        {
          SET_DIRECTORY(roms);
        }

        if(!strcasecmp(current_variable, "save_directory"))
        {
          SET_DIRECTORY(save);
        }

        if(!strcasecmp(current_variable, "game_config_directory"))
        {
          SET_DIRECTORY(cfg);
        }

        if(!strcasecmp(current_variable, "snapshot_directory"))
        {
          SET_DIRECTORY(snap);
        }

        if(!strcasecmp(current_variable, "cheat_directory"))
        {
          SET_DIRECTORY(cheat);
        }
      }
    }
    fclose(dir_cfg_ptr);

    if(error_str_line > 5)
    {
      sprintf(error_str, "set directory : %s\n\nPress any button to continue.\n",
       main_path);
      print_string(error_str, 0xFFFF, 0x0000, 5, error_str_line + 10);
      error_msg("");
    }

    return 0;
  }

  return -1;
}

s8 load_font_cfg(char *file_name, char *s_font, char *d_font)
{
  char current_line[256];
  char current_variable[256];
  char current_value[256];

  FILE *font_cfg_ptr;

  font_cfg_ptr = fopen(file_name, "r");

  if(font_cfg_ptr)
  {
    while(fgets(current_line, 256, font_cfg_ptr))
    {
      if(parse_config_line(current_line, current_variable, current_value) != -1)
      {
        ADD_LAUNCH_DIRECTORY();

        if(!strcasecmp(current_variable, "single_byte_font"))
        {
          strcpy(s_font, current_value);
        }

        if(!strcasecmp(current_variable, "double_byte_font"))
        {
          strcpy(d_font, current_value);
        }
      }
    }
    fclose(font_cfg_ptr);

    return 0;
  }

  return -1;
}

s8 load_msg_cfg(char *file_name)
{
  char current_line[256];
  char current_str[256];

  u32 msg_id = 0;
  FILE *msg_cfg_ptr;

  sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_DATE_FORMAT, &date_format);

  msg_cfg_ptr = fopen(file_name, "r");

  if(msg_cfg_ptr)
  {
    while(fgets(current_line, 256, msg_cfg_ptr))
    {
      if(parse_msg_line(current_line, current_str) != -1)
      {
        replace_msg_line(current_str);
        strcpy(msg[msg_id], current_str);

        while(fgets(current_line, 256, msg_cfg_ptr))
        {
          if(parse_msg_line(current_line, current_str) == -1)
            break;

          replace_msg_line(current_str);
          strcat(msg[msg_id], current_str);
        }
        msg_id++;
      }

      if(msg_id == MSG_END)
      {
        fclose(msg_cfg_ptr);
        return 0;
      }
    }
    fclose(msg_cfg_ptr);

    return -1;
  }

  return -2;
}

static s8 parse_msg_line(char *current_line, char *current_str)
{
  char *line_ptr;

  line_ptr = current_line;

  if(current_line[0] != '!')
  {
    return -1;
  }

  line_ptr++;
  strcpy(current_str, line_ptr);

  line_ptr = current_str + strlen(current_str) - 1;

  if(*line_ptr == '\n')
  {
    line_ptr--;
    *line_ptr = 0;
  }

  if(*line_ptr == '\r')
  {
    *line_ptr = 0;
  }

  return 0;
}

// "\n" to '\n'
static void replace_msg_line(char *msg_str)
{
  char *msg_ptr;

  while((msg_ptr = strstr(msg_str, "\\n")) != NULL)
  {
    memmove((msg_ptr + 1), (msg_ptr + 2), (strlen(msg_ptr + 2) + 1));
    *msg_ptr = '\n';
  }
}


void setup_status_str(s8 font_width)
{
  char time_str[80];
  char batt_str[80];

  update_status_str(time_str, batt_str);

  batt_status_pos_x = 480 - fbm_getwidth(batt_str);
  time_status_pos_x = batt_status_pos_x - (fbm_getwidth(time_str) + font_width);

  current_dir_name_length = (time_status_pos_x / font_width) - 1;
}

static u16 update_status_str(char *time_str, char *batt_str)
{
  pspTime current_time;
  u16 current_dweek;

  char batt_life_str[10];
  u16 color_batt_life = COLOR_HELP_TEXT;

  sceRtcGetCurrentClockLocalTime(&current_time);
  current_dweek = sceRtcGetDayOfWeek(current_time.year, current_time.month,
   current_time.day);

  get_timestamp_string(time_str, MSG_MENU_DATE_FMT_0, current_time,
   current_dweek);

  int batt_life_per = scePowerGetBatteryLifePercent();

  if(batt_life_per < 0)
  {
    strcpy(batt_str, "BATT. --%");
  }
  else
  {
    sprintf(batt_str, "BATT.%3d%%", batt_life_per);

    if(batt_life_per < 10)
      color_batt_life = COLOR_BATT_EMPTY1;
    else
    if(batt_life_per < 30)
      color_batt_life = COLOR_BATT_EMPTY2;
  }

  if(scePowerIsPowerOnline())
  {
    strcpy(batt_life_str, "[DC IN]");
  }
  else
  {
    int batt_life_time = scePowerGetBatteryLifeTime();
    int batt_life_hour = (batt_life_time / 60) % 100;
    int batt_life_min = batt_life_time % 60;

    if(batt_life_time < 0)
    {
      strcpy(batt_life_str, "[--:--]");
    }
    else
    {
      sprintf(batt_life_str, "[%2d:%02d]", batt_life_hour, batt_life_min);
    }
  }
  strcat(batt_str, batt_life_str);

  return color_batt_life;
}


static void get_timestamp_string(char *buffer, u16 msg_id, pspTime msg_time,
 u16 dweek)
{
  char *week_str[] =
  {
    msg[MSG_DAYW_0], msg[MSG_DAYW_1], msg[MSG_DAYW_2], msg[MSG_DAYW_3],
    msg[MSG_DAYW_4], msg[MSG_DAYW_5], msg[MSG_DAYW_6], ""
  };

  switch(date_format)
  {
    case PSP_SYSTEMPARAM_DATE_FORMAT_YYYYMMDD:
      sprintf(buffer, msg[msg_id    ], msg_time.year, msg_time.month,
       msg_time.day, week_str[dweek], msg_time.hour, msg_time.minutes,
       msg_time.seconds, (msg_time.microseconds / 1000));
      break;

    case PSP_SYSTEMPARAM_DATE_FORMAT_MMDDYYYY:
      sprintf(buffer, msg[msg_id + 1], week_str[dweek], msg_time.month,
       msg_time.day, msg_time.year, msg_time.hour, msg_time.minutes,
       msg_time.seconds, (msg_time.microseconds / 1000));
      break;

    case PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY:
      sprintf(buffer, msg[msg_id + 2], week_str[dweek], msg_time.day,
       msg_time.month, msg_time.year, msg_time.hour, msg_time.minutes,
       msg_time.seconds, (msg_time.microseconds / 1000));
      break;
  }
}


static void save_ss_bmp(u16 *image)
{
  static const unsigned char ALIGN_DATA header[] =
  {  'B',  'M', 0x36, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
     240, 0x00, 0x00, 0x00,  160, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC2,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };

  char ss_filename[MAX_FILE];
  char ss_timestamp[MAX_FILE];
  char ss_save_path[MAX_PATH];

  pspTime current_time;

  u8 ALIGN_DATA rgb_data[160][240][3];
  u8 x,y;
  u16 col;
  u8 r,g,b;

  change_ext(gamepak_filename, ss_filename, "_");

  sceRtcGetCurrentClockLocalTime(&current_time);
  get_timestamp_string(ss_timestamp, MSG_SS_FMT_0, current_time, 7);

  sprintf(ss_save_path, "%s/%s%s.bmp", dir_snap, ss_filename, ss_timestamp);

  for(y = 0; y < 160; y++)
  {
    for(x = 0; x < 240; x++)
    {
      col = image[x + y * 240];
      r = (col >> 10) & 0x1F;
      g = (col >> 5) & 0x1F;
      b = (col) & 0x1F;

      rgb_data[159-y][x][2] = b * 255 / 31;
      rgb_data[159-y][x][1] = g * 255 / 31;
      rgb_data[159-y][x][0] = r * 255 / 31;
    }
  }

  FILE *ss = fopen( ss_save_path, "wb" );
  if( ss == NULL ) return;

  fwrite( header, sizeof(header), 1, ss );
  fwrite( rgb_data, 240*160*3, 1, ss);
  fclose( ss );
}

