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

#include "common.h"

static void trigger_key(u32 key);

#define PSP_ALL_BUTTON_MASK 0xFFFF

#define BUTTON_REPEAT_START    200000
#define BUTTON_REPEAT_CONTINUE 50000

#define PSP_CTRL_ANALOG_UP    (1 << 28)
#define PSP_CTRL_ANALOG_DOWN  (1 << 29)
#define PSP_CTRL_ANALOG_LEFT  (1 << 30)
#define PSP_CTRL_ANALOG_RIGHT (1 << 31)

u32 option_enable_analog = 1;
u32 option_analog_sensitivity = 4;

u32 key = 0;

u32 ALIGN_DATA gamepad_config_map[16] =
{
  BUTTON_ID_MENU,
  BUTTON_ID_A,
  BUTTON_ID_B,
  BUTTON_ID_START,
  BUTTON_ID_L,
  BUTTON_ID_R,
  BUTTON_ID_DOWN,
  BUTTON_ID_LEFT,
  BUTTON_ID_UP,
  BUTTON_ID_RIGHT,
  BUTTON_ID_SELECT,
  BUTTON_ID_START,
  BUTTON_ID_UP,
  BUTTON_ID_DOWN,
  BUTTON_ID_LEFT,
  BUTTON_ID_RIGHT
};

u32 ALIGN_DATA button_psp_mask_to_config[] =
{
  PSP_CTRL_TRIANGLE,
  PSP_CTRL_CIRCLE,
  PSP_CTRL_CROSS,
  PSP_CTRL_SQUARE,
  PSP_CTRL_LTRIGGER,
  PSP_CTRL_RTRIGGER,
  PSP_CTRL_DOWN,
  PSP_CTRL_LEFT,
  PSP_CTRL_UP,
  PSP_CTRL_RIGHT,
  PSP_CTRL_SELECT,
  PSP_CTRL_START,
  PSP_CTRL_ANALOG_UP,
  PSP_CTRL_ANALOG_DOWN,
  PSP_CTRL_ANALOG_LEFT,
  PSP_CTRL_ANALOG_RIGHT
};

u32 ALIGN_DATA button_id_to_gba_mask[] =
{
  BUTTON_UP,
  BUTTON_DOWN,
  BUTTON_LEFT,
  BUTTON_RIGHT,
  BUTTON_A,
  BUTTON_B,
  BUTTON_L,
  BUTTON_R,
  BUTTON_START,
  BUTTON_SELECT,
  BUTTON_NONE,
  BUTTON_NONE,
  BUTTON_NONE,
  BUTTON_NONE
};

typedef enum
{
  BUTTON_NOT_HELD,
  BUTTON_HELD_INITIAL,
  BUTTON_HELD_REPEAT
} BUTTON_REPEAT_STATE_TYPE;

BUTTON_REPEAT_STATE_TYPE button_repeat_state = BUTTON_NOT_HELD;
u32 button_repeat = 0;
GUI_ACTION_TYPE cursor_repeat = CURSOR_NONE;

u32 last_buttons = 0;
u64 button_repeat_timestamp;

u32 rapidfire_flag = 1;

u8 enable_tilt_sensor = 0;
u16 tilt_sensorX;
u16 tilt_sensorY;


// Special thanks to psp298 for the analog->dpad code!

static void trigger_key(u32 key)
{
  u16 p1_cnt = io_registers[REG_P1CNT];

  if((p1_cnt >> 14) & 0x01)
  {
    u16 key_intersection = (p1_cnt & key) & 0x3FF;

    if(p1_cnt >> 15)
    {
      if(key_intersection == (p1_cnt & 0x3FF))
        raise_interrupt(IRQ_KEYPAD);
    }
    else
    {
      if(key_intersection)
        raise_interrupt(IRQ_KEYPAD);
    }
  }
}


GUI_ACTION_TYPE get_gui_input(void)
{
  SceCtrlData ctrl_data;
  GUI_ACTION_TYPE new_button = CURSOR_NONE;
  u32 new_buttons;
  u32 analog_sensitivity = 3 + (option_analog_sensitivity * 8);
  u32 inv_analog_sensitivity = 255 - analog_sensitivity;

  sceKernelDelayThread(25000);

  sceCtrlPeekBufferPositive(&ctrl_data, 1);

  if(option_enable_analog && !(ctrl_data.Buttons & PSP_CTRL_HOLD))
  {
    if(ctrl_data.Lx < analog_sensitivity)
      ctrl_data.Buttons = PSP_CTRL_LEFT;

    if(ctrl_data.Lx > inv_analog_sensitivity)
      ctrl_data.Buttons = PSP_CTRL_RIGHT;

    if(ctrl_data.Ly < analog_sensitivity)
      ctrl_data.Buttons = PSP_CTRL_UP;

    if(ctrl_data.Ly > inv_analog_sensitivity)
      ctrl_data.Buttons = PSP_CTRL_DOWN;
  }

  ctrl_data.Buttons &= PSP_ALL_BUTTON_MASK;

  new_buttons = (last_buttons ^ ctrl_data.Buttons) & ctrl_data.Buttons;
  last_buttons = ctrl_data.Buttons;

  if(new_buttons & PSP_CTRL_LEFT)
    new_button = CURSOR_LEFT;

  if(new_buttons & PSP_CTRL_RIGHT)
    new_button = CURSOR_RIGHT;

  if(new_buttons & PSP_CTRL_UP)
    new_button = CURSOR_UP;

  if(new_buttons & PSP_CTRL_DOWN)
    new_button = CURSOR_DOWN;

  if(new_buttons & PSP_CTRL_START)
    new_button = CURSOR_SELECT;

  if(new_buttons & PSP_CTRL_CIRCLE)
    new_button = CURSOR_SELECT;

  if(new_buttons & PSP_CTRL_CROSS)
    new_button = CURSOR_EXIT;

  if(new_buttons & PSP_CTRL_SQUARE)
    new_button = CURSOR_BACK;

  if(new_buttons & PSP_CTRL_RTRIGGER)
    new_button = CURSOR_RTRIGGER;

  if(new_buttons & PSP_CTRL_LTRIGGER)
    new_button = CURSOR_LTRIGGER;

  if(new_button != CURSOR_NONE)
  {
    get_ticks_us(&button_repeat_timestamp);
    button_repeat_state = BUTTON_HELD_INITIAL;
    button_repeat = new_buttons;
    cursor_repeat = new_button;
  }
  else
  {
    if(ctrl_data.Buttons & button_repeat)
    {
      u64 new_ticks;
      get_ticks_us(&new_ticks);

      if(button_repeat_state == BUTTON_HELD_INITIAL)
      {
        if((new_ticks - button_repeat_timestamp) > BUTTON_REPEAT_START)
        {
          new_button = cursor_repeat;
          button_repeat_timestamp = new_ticks;
          button_repeat_state = BUTTON_HELD_REPEAT;
        }
      }

      if(button_repeat_state == BUTTON_HELD_REPEAT)
      {
        if((new_ticks - button_repeat_timestamp) > BUTTON_REPEAT_CONTINUE)
        {
          new_button = cursor_repeat;
          button_repeat_timestamp = new_ticks;
        }
      }
    }
  }

  return new_button;
}

GUI_ACTION_TYPE get_gui_input_fs_hold(u32 button_id)
{
  GUI_ACTION_TYPE new_button = get_gui_input();

  if((last_buttons & button_psp_mask_to_config[button_id]) == 0)
    return CURSOR_BACK;

  return new_button;
}

u32 update_input(void)
{
  SceCtrlData ctrl_data;
  u32 buttons;
  u32 non_repeat_buttons;
  u32 button_id = 0;
  u32 i;
  u32 new_key = 0;
  u32 analog_sensitivity = 3 + (option_analog_sensitivity * 8);
  u32 inv_analog_sensitivity = 255 - analog_sensitivity;

  tilt_sensorX = 0x800;
  tilt_sensorY = 0x800;

  sceCtrlPeekBufferPositive(&ctrl_data, 1);

  buttons = ctrl_data.Buttons | readHomeButton();

  if(option_enable_analog && !(ctrl_data.Buttons & PSP_CTRL_HOLD))
  {
    if(enable_tilt_sensor)
    {
      if((ctrl_data.Lx < analog_sensitivity) ||
         (ctrl_data.Lx > inv_analog_sensitivity))
      {
        tilt_sensorX = (0xFF - ctrl_data.Lx) * 16;
      }

      if((ctrl_data.Ly < analog_sensitivity) ||
         (ctrl_data.Ly > inv_analog_sensitivity))
      {
        tilt_sensorY = (0xFF - ctrl_data.Ly) * 16;
      }

      enable_tilt_sensor = 0;
    }
    else
    {
      if(ctrl_data.Lx < analog_sensitivity)
        buttons |= PSP_CTRL_ANALOG_LEFT;

      if(ctrl_data.Lx > inv_analog_sensitivity)
        buttons |= PSP_CTRL_ANALOG_RIGHT;

      if(ctrl_data.Ly < analog_sensitivity)
        buttons |= PSP_CTRL_ANALOG_UP;

      if(ctrl_data.Ly > inv_analog_sensitivity)
        buttons |= PSP_CTRL_ANALOG_DOWN;
    }
  }

  non_repeat_buttons = (last_buttons ^ buttons) & buttons;
  last_buttons = buttons;

  for(i = 0; i < 16; i++)
  {
    if(non_repeat_buttons & button_psp_mask_to_config[i])
      button_id = gamepad_config_map[i];

    if(buttons & PSP_CTRL_HOME)
      button_id = BUTTON_ID_MENU;

    switch(button_id)
    {
      case BUTTON_ID_MENU:
        return menu();

      case BUTTON_ID_LOADSTATE:
        action_loadstate();
        return 1;

      case BUTTON_ID_SAVESTATE:
        action_savestate();
        return 0;

      case BUTTON_ID_FASTFORWARD:
        synchronize_flag ^= 1;
        break;

      case BUTTON_ID_FPS:
        psp_fps_debug ^= 1;
        break;
    }

    if(buttons & button_psp_mask_to_config[i])
    {
      button_id = gamepad_config_map[i];

      if(button_id < BUTTON_ID_MENU)
      {
        new_key |= button_id_to_gba_mask[button_id];
      }
      else
      {
        if((button_id >= BUTTON_ID_RAPIDFIRE_A) &&
         (button_id <= BUTTON_ID_RAPIDFIRE_R))
        {
          rapidfire_flag ^= 1;
          if(rapidfire_flag)
          {
            new_key |= button_id_to_gba_mask[button_id -
             BUTTON_ID_RAPIDFIRE_A + BUTTON_ID_A];
          }
          else
          {
            new_key &= ~button_id_to_gba_mask[button_id -
             BUTTON_ID_RAPIDFIRE_A + BUTTON_ID_A];
          }
        }
      }
    }

  }

  if((new_key | key) != key)
    trigger_key(new_key);

  key = new_key;

  ADDRESS16(io_registers, (REG_P1 * 2)) = (~key) & 0x3FF;

  return 0;
}

void init_input(void)
{
  sceCtrlSetSamplingCycle(0);
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}


// type = READ / WRITE_MEM
#define INPUT_SAVESTATE_BODY(type)                                            \
{                                                                             \
  FILE_##type##_VARIABLE(savestate_file, key);                                \
}                                                                             \

void input_read_savestate(FILE_TAG_TYPE savestate_file)
  INPUT_SAVESTATE_BODY(READ);

void input_write_mem_savestate(FILE_TAG_TYPE savestate_file)
  INPUT_SAVESTATE_BODY(WRITE_MEM);

