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

PSP_MODULE_INFO("gpSP-mod", PSP_MODULE_USER, 0, 9);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(512);


int main(int argc, char *argv[]);
static void psp_main(int argc, char *argv[]);

static void init_main(void);
static void setup_main(void);
static void load_setting_cfg(void);
static void load_bios_file(void);

static void setup_callbacks(void);
static int power_callback(int unknown, int powerInfo, void *arg);
static int callback_thread(SceSize args, void *argp);

static void vblank_interrupt_handler(u32 sub, u32 *parg);

static void synchronize(void);
static void loop_sleep(void);

static u32 get_psp_model(u32 fw_ver);


#define TM0CNT_L  0x100
#define TM1CNT_L  0x104
#define TM2CNT_L  0x108
#define TM3CNT_L  0x10C

typedef enum
{
  TIMER_INACTIVE,
  TIMER_PRESCALE,
  TIMER_CASCADE
} TIMER_STATUS_TYPE;

typedef enum
{
  TIMER_NO_IRQ,
  TIMER_TRIGGER_IRQ
} TIMER_IRQ_TYPE;

typedef enum
{
  TIMER_DS_CHANNEL_NONE,
  TIMER_DS_CHANNEL_A,
  TIMER_DS_CHANNEL_B,
  TIMER_DS_CHANNEL_BOTH
} TIMER_DS_CHANNEL_TYPE;

typedef struct
{
  s32 count;
  u32 reload;
  u8 reload_update;
  u8 prescale;
  FIXED08_24 frequency_step;
  TIMER_DS_CHANNEL_TYPE direct_sound_channels;
  TIMER_IRQ_TYPE irq;
  TIMER_STATUS_TYPE status;
} TIMER_TYPE;

const u8 ALIGN_DATA prescale_table[] = { 0, 6, 8, 10 };
TIMER_TYPE ALIGN_DATA timer[4];

u32 option_frameskip_type = AUTO_FRAMESKIP;
u32 option_frameskip_value = 9;
u32 option_random_skip = 0;
u32 option_clock_speed = 333;
u32 option_update_backup = 1;

u32 psp_model = 0;
u32 firmware_version;

u32 cpu_ticks = 0;
u32 execute_cycles = 960;
s32 video_count = 960;

// u32 cycle_dma16_words = 0;
// u32 cycle_dma32_words = 0;
u32 cycle_count_dma = 0;

// u32 arm_frame = 0;
// u32 thumb_frame = 0;
// u32 last_frame = 0;

char main_path[MAX_PATH];

u8 quit_flag = 0;
u8 sleep_flag = 0;

u8 psp_fps_debug = 0;
u8 synchronize_flag = 1;

u32 real_frame_count = 0;
u32 virtual_frame_count = 0;

u8 skip_next_frame = 0;
u8 num_skipped_frames = 0;
u8 frameskip_counter = 0;
u8 interval_skipped_frames = 0;
u8 frames = 0;
u16 vblank_count = 0;


#define SOUND_UPDATE_FREQUENCY_STEP(timer_number)                             \
  timer[timer_number].frequency_step =                                        \
   FLOAT_TO_FP08_24(16777216.0 / (timer_reload * SOUND_FREQUENCY))            \

void timer_control_low(u8 timer_number, u32 value)
{
  timer[timer_number].reload = 0x10000 - value;
  timer[timer_number].reload_update = 1;
}

void timer_control_high(u8 timer_number, u32 value)
{
  if(value & 0x80)
  {
    if(timer[timer_number].status == TIMER_INACTIVE)
    {
      u32 timer_reload = timer[timer_number].reload;

      if(value & 0x04)
      {
        timer[timer_number].status = TIMER_CASCADE;
        timer[timer_number].prescale = 0;
      }
      else
      {
        timer[timer_number].status = TIMER_PRESCALE;
        timer[timer_number].prescale = prescale_table[value & 0x03];
      }

      timer[timer_number].irq = (value >> 6) & 0x01;

      ADDRESS16(io_registers, TM0CNT_L + (timer_number * 4)) =
       0x10000 - timer_reload;

      timer_reload <<= timer[timer_number].prescale;
      timer[timer_number].count = timer_reload;

      if(timer_reload < execute_cycles)
        execute_cycles = timer_reload;

      if(timer_number < 2)
      {
        SOUND_UPDATE_FREQUENCY_STEP(timer_number);
        timer[timer_number].reload_update = 0;

        if(timer[timer_number].direct_sound_channels & 0x01)
          adjust_direct_sound_buffer(0, cpu_ticks + timer_reload);

        if(timer[timer_number].direct_sound_channels & 0x02)
          adjust_direct_sound_buffer(1, cpu_ticks + timer_reload);
      }
    }
  }
  else
  {
    if(timer[timer_number].status != TIMER_INACTIVE)
      timer[timer_number].status = TIMER_INACTIVE;
  }
}

void direct_sound_timer_select(u32 value)
{
  timer[1].direct_sound_channels =
   ((value >> 13) & 0x02) | ((value >> 10) & 0x01);
  timer[0].direct_sound_channels = timer[1].direct_sound_channels ^ 0x03;
}


#define CHECK_COUNT(count_var)                                                \
  if(count_var < execute_cycles)                                              \
  {                                                                           \
    execute_cycles = count_var;                                               \
  }                                                                           \

#define CHECK_TIMER(timer_number)                                             \
  if(timer[timer_number].status == TIMER_PRESCALE)                            \
  {                                                                           \
    CHECK_COUNT(timer[timer_number].count);                                   \
  }                                                                           \

#define UPDATE_TIMER0()                                                       \
  if(timer[0].status != TIMER_INACTIVE)                                       \
  {                                                                           \
    timer[0].count -= execute_cycles;                                         \
                                                                              \
    if(timer[0].count <= 0)                                                   \
    {                                                                         \
      if(timer[0].irq == TIMER_TRIGGER_IRQ)                                   \
        irq_raised |= IRQ_TIMER0;                                             \
                                                                              \
      if(timer[1].status == TIMER_CASCADE)                                    \
      {                                                                       \
        timer[1].count--;                                                     \
        ADDRESS16(io_registers, TM1CNT_L) = 0x10000 - timer[1].count;         \
      }                                                                       \
                                                                              \
      if(timer[0].direct_sound_channels & 0x01)                               \
        sound_timer(timer[0].frequency_step, 0);                              \
                                                                              \
      if(timer[0].direct_sound_channels & 0x02)                               \
        sound_timer(timer[0].frequency_step, 1);                              \
                                                                              \
      u32 timer_reload = timer[0].reload << timer[0].prescale;                \
                                                                              \
      if(timer[0].reload_update)                                              \
      {                                                                       \
        SOUND_UPDATE_FREQUENCY_STEP(0);                                       \
        timer[0].reload_update = 0;                                           \
      }                                                                       \
                                                                              \
      timer[0].count += timer_reload;                                         \
    }                                                                         \
                                                                              \
    ADDRESS16(io_registers, TM0CNT_L) =                                       \
     0x10000 - (timer[0].count >> timer[0].prescale);                         \
  }                                                                           \

#define UPDATE_TIMER1()                                                       \
  if(timer[1].status != TIMER_INACTIVE)                                       \
  {                                                                           \
    if(timer[1].status != TIMER_CASCADE)                                      \
      timer[1].count -= execute_cycles;                                       \
                                                                              \
    if(timer[1].count <= 0)                                                   \
    {                                                                         \
      if(timer[1].irq == TIMER_TRIGGER_IRQ)                                   \
        irq_raised |= IRQ_TIMER1;                                             \
                                                                              \
      if(timer[2].status == TIMER_CASCADE)                                    \
      {                                                                       \
        timer[2].count--;                                                     \
        ADDRESS16(io_registers, TM2CNT_L) = 0x10000 - timer[2].count;         \
      }                                                                       \
                                                                              \
      if(timer[1].direct_sound_channels & 0x01)                               \
        sound_timer(timer[1].frequency_step, 0);                              \
                                                                              \
      if(timer[1].direct_sound_channels & 0x02)                               \
        sound_timer(timer[1].frequency_step, 1);                              \
                                                                              \
      u32 timer_reload = timer[1].reload << timer[1].prescale;                \
                                                                              \
      if(timer[1].reload_update)                                              \
      {                                                                       \
        SOUND_UPDATE_FREQUENCY_STEP(1);                                       \
        timer[1].reload_update = 0;                                           \
      }                                                                       \
                                                                              \
      timer[1].count += timer_reload;                                         \
    }                                                                         \
                                                                              \
    ADDRESS16(io_registers, TM1CNT_L) =                                       \
     0x10000 - (timer[1].count >> timer[1].prescale);                         \
  }                                                                           \

#define UPDATE_TIMER2()                                                       \
  if(timer[2].status != TIMER_INACTIVE)                                       \
  {                                                                           \
    if(timer[2].status != TIMER_CASCADE)                                      \
      timer[2].count -= execute_cycles;                                       \
                                                                              \
    if(timer[2].count <= 0)                                                   \
    {                                                                         \
      if(timer[2].irq == TIMER_TRIGGER_IRQ)                                   \
        irq_raised |= IRQ_TIMER2;                                             \
                                                                              \
      if(timer[3].status == TIMER_CASCADE)                                    \
      {                                                                       \
        timer[3].count--;                                                     \
        ADDRESS16(io_registers, TM3CNT_L) = 0x10000 - timer[3].count;         \
      }                                                                       \
                                                                              \
      timer[2].count += timer[2].reload << timer[2].prescale;                 \
    }                                                                         \
                                                                              \
    ADDRESS16(io_registers, TM2CNT_L) =                                       \
     0x10000 - (timer[2].count >> timer[2].prescale);                         \
  }                                                                           \

#define UPDATE_TIMER3()                                                       \
  if(timer[3].status != TIMER_INACTIVE)                                       \
  {                                                                           \
    if(timer[3].status != TIMER_CASCADE)                                      \
      timer[3].count -= execute_cycles;                                       \
                                                                              \
    if(timer[3].count <= 0)                                                   \
    {                                                                         \
      if(timer[3].irq == TIMER_TRIGGER_IRQ)                                   \
        irq_raised |= IRQ_TIMER3;                                             \
                                                                              \
      timer[3].count += timer[3].reload << timer[3].prescale;                 \
    }                                                                         \
                                                                              \
    ADDRESS16(io_registers, TM3CNT_L) =                                       \
     0x10000 - (timer[3].count >> timer[3].prescale);                         \
  }                                                                           \


#define TRANSFER_HBLANK_DMA(channel)                                          \
  if(dma[channel].start_type == DMA_START_HBLANK)                             \
  {                                                                           \
    dma_transfer(dma + channel);                                              \
  }                                                                           \

#define TRANSFER_VBLANK_DMA(channel)                                          \
  if(dma[channel].start_type == DMA_START_VBLANK)                             \
  {                                                                           \
    dma_transfer(dma + channel);                                              \
  }                                                                           \

u32 update_gba(void)
{
  IRQ_TYPE irq_raised = IRQ_NONE;

  do
  {
    cpu_ticks += execute_cycles;
    reg[CHANGED_PC_STATUS] = 0;

    if(sleep_flag)
      loop_sleep();

    if(gbc_sound_update)
    {
      update_gbc_sound(cpu_ticks);
      gbc_sound_update = 0;
    }

    UPDATE_TIMER0();
    UPDATE_TIMER1();
    UPDATE_TIMER2();
    UPDATE_TIMER3();

    video_count -= execute_cycles;

    if(video_count <= 0)
    {
      u16 vcount = io_registers[REG_VCOUNT];
      u16 dispstat = io_registers[REG_DISPSTAT];

      if((dispstat & 0x02) == 0)
      {
        // Transition from hrefresh to hblank
        video_count += 272;
        dispstat |= 0x02;

        if(dispstat & 0x10)
          irq_raised |= IRQ_HBLANK;

        if((dispstat & 0x01) == 0)
        {
          // If in visible area also fire HDMA
          if(!skip_next_frame)
            update_scanline();

          TRANSFER_HBLANK_DMA(0);
          TRANSFER_HBLANK_DMA(1);
          TRANSFER_HBLANK_DMA(2);
          TRANSFER_HBLANK_DMA(3);
        }
      }
      else
      {
        // Transition from hblank to next line
        video_count += 960;
        dispstat &= ~0x02;

        vcount++;

        if(vcount == 160)
        {
          // Transition from vrefresh to vblank
          dispstat |= 0x01;

          if(update_input())
            continue;

          if(dispstat & 0x08)
            irq_raised |= IRQ_VBLANK;

          if(!skip_next_frame)
            (*update_screen)();

          update_gbc_sound(cpu_ticks);
          gbc_sound_update = 0;

          affine_reference_x[0] =
           (s32)(ADDRESS32(io_registers, 0x28) << 4) >> 4;
          affine_reference_y[0] =
           (s32)(ADDRESS32(io_registers, 0x2C) << 4) >> 4;
          affine_reference_x[1] =
           (s32)(ADDRESS32(io_registers, 0x38) << 4) >> 4;
          affine_reference_y[1] =
           (s32)(ADDRESS32(io_registers, 0x3C) << 4) >> 4;

          TRANSFER_VBLANK_DMA(0);
          TRANSFER_VBLANK_DMA(1);
          TRANSFER_VBLANK_DMA(2);
          TRANSFER_VBLANK_DMA(3);
        }
        else

        if(vcount == 228)
        {
          // Transition from vblank to next screen
          dispstat &= ~0x01;

          if(option_update_backup)
            update_backup();

          if(quit_flag == 1)
            quit();

          process_cheats();

          u8 skip_this_frame = skip_next_frame;

          synchronize();
          synchronize_sound();

          if(!skip_this_frame)
            flip_screen(0);
          else
            draw_volume_status(0);

          vcount = 0;
        }

        if(vcount == (dispstat >> 8))
        {
          // vcount trigger
          dispstat |= 0x04;

          if(dispstat & 0x20)
            irq_raised |= IRQ_VCOUNT;
        }
        else
        {
          dispstat &= ~0x04;
        }

        ADDRESS16(io_registers, (REG_VCOUNT * 2)) = vcount;
      }

      ADDRESS16(io_registers, (REG_DISPSTAT * 2)) = dispstat;
    }

    if(irq_raised)
      raise_interrupt(irq_raised);

    execute_cycles = video_count;

    CHECK_TIMER(0);
    CHECK_TIMER(1);
    CHECK_TIMER(2);
    CHECK_TIMER(3);

  } while(reg[CPU_HALT_STATE] != CPU_ACTIVE);

  return execute_cycles;
}


static void vblank_interrupt_handler(u32 sub, u32 *parg)
{
  real_frame_count++;
  vblank_count++;
}

static void synchronize(void)
{
  static u16 fps = 60;
  static u16 frames_drawn = 60;

  u32 used_frameskip_type = option_frameskip_type;
  u32 used_frameskip_value = option_frameskip_value;

  frames++;

  if(frames == 60)
  {
    fps = 3600 / vblank_count;
    frames_drawn = 60 - interval_skipped_frames;

    vblank_count = 0;
    frames = 0;
    interval_skipped_frames = 0;
  }

  if(psp_fps_debug)
  {
    char print_buffer[64];
    sprintf(print_buffer, "%02d(%02d)", (int)fps % 100, (int)frames_drawn);
    print_string(print_buffer, 0xFFFF, 0x0000, 0, 0);
  }

  if(!synchronize_flag)
  {
    print_string("--FF--", 0xFFFF, 0x0000, 0, 0);

    used_frameskip_type = MANUAL_FRAMESKIP;
    used_frameskip_value = 4;
  }

  skip_next_frame = 0;
  virtual_frame_count++;

  if(real_frame_count >= virtual_frame_count)
  {
    if((real_frame_count > virtual_frame_count) &&
       (used_frameskip_type == AUTO_FRAMESKIP) &&
       (num_skipped_frames < option_frameskip_value))
    {
      skip_next_frame = 1;
      num_skipped_frames++;
    }
    else
    {
      real_frame_count = 0;
      virtual_frame_count = 0;
      num_skipped_frames = 0;
    }

    // Here so that the home button return will eventually work.
    // If it's not running fullspeed anyway this won't really hurt
    // it much more.
    sceKernelDelayThread(1);
  }
  else
  {
    if(synchronize_flag)
    {
      sceDisplayWaitVblankStart();
      real_frame_count = 0;
      virtual_frame_count = 0;
      num_skipped_frames = 0;
    }
  }

  if(used_frameskip_type == MANUAL_FRAMESKIP)
  {
    frameskip_counter = (frameskip_counter + 1) % (used_frameskip_value + 1);

    if(option_random_skip)
    {
      if(frameskip_counter != (rand() % (used_frameskip_value + 1)))
      {
        skip_next_frame = 1;
      }
    }
    else
    {
      if(frameskip_counter)
      {
        skip_next_frame = 1;
      }
    }
  }

  interval_skipped_frames += skip_next_frame;
}


static void init_main(void)
{
  u8 i;

  skip_next_frame = 0;

  vblank_count = 0;
  frames = 0;
  interval_skipped_frames = 0;

  for(i = 0; i < 4; i++)
  {
    dma[i].start_type = DMA_INACTIVE;
    dma[i].direct_sound_channel = DMA_NO_DIRECT_SOUND;
    timer[i].status = TIMER_INACTIVE;
    timer[i].reload = 0x10000;
    timer[i].reload_update = 0;
  }

  timer[0].direct_sound_channels = TIMER_DS_CHANNEL_BOTH;
  timer[1].direct_sound_channels = TIMER_DS_CHANNEL_NONE;

  cpu_ticks = 0;

  execute_cycles = 960;
  video_count = 960;

  flush_translation_cache_rom();
  flush_translation_cache_ram();
  flush_translation_cache_bios();
}

static void load_setting_cfg(void)
{
  char filename[MAX_FILE];

  char s_font[MAX_FILE];
  char d_font[MAX_FILE];
  s8 sfont_width;

  int lang_num;
  char *lang[] =
  {
    "japanese",      // 0
    "english",       // 1
    "french",        // 2
    "spanish",       // 3
    "german",        // 4
    "italian",       // 5
    "dutch",         // 6
    "portuguese",    // 7
    "russian",       // 8
    "korean",        // 9
    "chinese_big5",  // 10
    "chinese_gbk"    // 11
  };

  sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &lang_num);

  sprintf(filename, "%s/settings/%s.fnt", main_path, lang[lang_num]);
  if(load_font_cfg(filename, s_font, d_font) < 0)
  {
    lang_num = 1;
    sprintf(filename, "%s/settings/english.fnt", main_path);

    if(load_font_cfg(filename, s_font, d_font) < 0)
      quit();
  }

  if((sfont_width = fbm_init(s_font, d_font, 1)) < 0)
  {
    quit();
  }

  sprintf(filename, "%s/settings/%s.msg", main_path, lang[lang_num]);
  if(load_msg_cfg(filename) < 0)
  {
    error_msg("message file error.\n\nPress any button to exit.");
    quit();
  }
  setup_status_str(sfont_width);

  sprintf(filename, "%s/settings/dir.cfg", main_path);
  if(load_dir_cfg(filename) < 0)
  {
    sprintf(filename,
     "Failed to load dir.cfg\nset all directory : %s\n\nPress any button to continue.",
     main_path);
    error_msg(filename);
  }
}

static void load_bios_file(void)
{
  char filename[MAX_FILE];
  char msg_buf[1024];

  sprintf(filename, "%s/gba_bios.bin", main_path);
  s8 bios_ret = load_bios(filename);

  if(bios_ret == -1)
  {
    sprintf(msg_buf, "%s%s", msg[MSG_ERR_BIOS_NONE1], msg[MSG_ERR_BIOS_NONE2]);
    error_msg(msg_buf);
    quit();
  }

  if(bios_ret == -2)
  {
    sprintf(msg_buf, "%s%s", msg[MSG_ERR_BIOS_HASH1], msg[MSG_ERR_BIOS_HASH2]);
    error_msg(msg_buf);
    quit();
  }
}

static void setup_main(void)
{
  SceUID modID;
  char prx_path[MAX_PATH];

  sceKernelRegisterSubIntrHandler(PSP_VBLANK_INT, 0, vblank_interrupt_handler,
   NULL);
  sceKernelEnableSubIntr(PSP_VBLANK_INT, 0);

  // Copy the directory path of the executable into main_path
  getcwd(main_path, MAX_PATH);

  init_gamepak_buffer();
  init_sound();
  init_input();
  init_video();
  video_resolution_large();

  load_setting_cfg();
  load_bios_file();
  load_config_file();

  sprintf(prx_path, "%s/SystemButtons.prx", main_path);
  modID = pspSdkLoadStartModule(prx_path, PSP_MEMORY_PARTITION_KERNEL);
  if(modID >= 0)
  {
    initSystemButtons(firmware_version);
  }
  else
  {
    sprintf(prx_path,
     "Error 0x%08X start SystemButtons.prx.\n\nPress any button to exit.",
     modID);
    error_msg(prx_path);
    quit();
  }
}

static void psp_main(int argc, char *argv[])
{
  char load_filename[MAX_FILE];
  char *file_ext[] = { ".gba", ".bin", ".agb", ".zip", ".gbz", NULL };

  firmware_version = sceKernelDevkitVersion();
  psp_model = get_psp_model(firmware_version);

  initExceptionHandler();
  setup_callbacks();
  setup_main();

  gamepak_filename[0] = 0;

  if(argc > 1)
  {
    if(load_gamepak((char *)argv[1]) == -1)
    {
      clear_screen(0x0000);
      error_msg("Failed to load gamepak.\n\nPress any button to exit.");
      quit();
    }
    video_resolution_small();
    reset_gba();
  }
  else
  {
    if(load_file(file_ext, load_filename, dir_roms) == -1)
    {
      menu();
    }
    else
    {
      if(load_gamepak(load_filename) == -1)
      {
        clear_screen(0x0000);
        error_msg("Failed to load gamepak.\n\nPress any button to exit.");
        quit();
      }
      video_resolution_small();
      reset_gba();
    }
  }

//  last_frame = 0;
  set_cpu_clock(option_clock_speed);

  // We'll never actually return from here.
  execute_arm_translate(execute_cycles);
}

int main(int argc, char *argv[])
{
  psp_main(argc, argv);

  return 0;
}


void quit(void)
{
  update_backup_force();
  save_config_file();

  sound_term();
  memory_term();
  video_term();

  fbm_freeall();

  set_cpu_clock(222);
  sceKernelExitGame();
}

void reset_gba(void)
{
  init_memory();
  init_cpu();
  init_main();
  reset_sound();
}


static void loop_sleep(void)
{
  if(FILE_CHECK_VALID(gamepak_file_large))
  {
    FILE_CLOSE(gamepak_file_large);
    gamepak_file_large = -1;

    do {
      sceKernelDelayThread(500000);
    } while(sleep_flag);

    u8 i = 5;

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
  else
  {
    do {
      sceKernelDelayThread(500000);
    } while(sleep_flag);
  }
}


static int power_callback(int unknown, int powerInfo, void *arg)
{
  if(powerInfo & PSP_POWER_CB_SUSPENDING)
  {
    sleep_flag = 1;

#ifndef BUILD_CFW_M33
    if(psp_model == 1)
      memcpy(gamepak_shelter, (void *)0x0BC00000, 0x400000);
#endif
  }
  else

  if(powerInfo & PSP_POWER_CB_RESUME_COMPLETE)
  {
#ifndef BUILD_CFW_M33
    if(psp_model == 1)
      memcpy((void *)0x0BC00000, gamepak_shelter, 0x400000);
#endif
    sceAudioSRCChRelease();
    sceAudioSRCChReserve(SOUND_SAMPLES, SOUND_FREQUENCY, 2);

    sleep_flag = 0;
  }

  return 0;
}

static int callback_thread(SceSize args, void *argp)
{
  int cbid;

  cbid = sceKernelCreateCallback("Power Callback", power_callback, NULL);
  scePowerRegisterCallback(0, cbid);

  sceKernelSleepThreadCB();

  return 0;
}

static void setup_callbacks(void)
{
  SceUID thid = -1;

  thid = sceKernelCreateThread("Update Thread", callback_thread, 0x11, 0xFA0,
                               PSP_MODULE_USER, NULL);
  if(thid < 0)
    quit();

  sceKernelStartThread(thid, 0, 0);

  quit_flag = 0;
  sleep_flag = 0;
}


u32 file_length(char *filename)
{
  SceIoStat stats;
  sceIoGetstat(filename, &stats);

  return stats.st_size;
}

void get_ticks_us(u64 *tick_return)
{
  u64 ticks;
  sceRtcGetCurrentTick(&ticks);

  *tick_return = (ticks * 1000000) / sceRtcGetTickResolution();
}

void change_ext(char *src, char *buffer, char *extension)
{
  char *dot_position;

  strcpy(buffer, src);
  dot_position = strrchr(buffer, '.');

  if(dot_position)
  {
    strcpy(dot_position, extension);
  }
}


void error_msg(char *text)
{
  GUI_ACTION_TYPE gui_action = CURSOR_NONE;

  print_string(text, 0xFFFF, 0x0000, 5, 5);
  flip_screen(1);

  while(gui_action == CURSOR_NONE)
  {
    gui_action = get_gui_input();
    sceKernelDelayThread(15000);

    if(quit_flag == 1)
      quit();
  }
}

static u32 get_psp_model(u32 fw_ver)
{
 // PSP-2000 && CFW 3.71 M33 or higher.
  if((kuKernelGetModel() == PSP_MODEL_SLIM_AND_LITE) && (fw_ver >= 0x03070110))
  {
    return 1;
  }

  return 0;
}

void set_cpu_clock(int psp_clock)
{
  scePowerSetClockFrequency(psp_clock, psp_clock, psp_clock / 2);
}


// type = READ / WRITE_MEM
#define MAIN_SAVESTATE_BODY(type)                                             \
{                                                                             \
  FILE_##type##_VARIABLE(savestate_file, cpu_ticks);                          \
  FILE_##type##_VARIABLE(savestate_file, execute_cycles);                     \
  FILE_##type##_VARIABLE(savestate_file, video_count);                        \
  FILE_##type##_ARRAY(savestate_file, timer);                                 \
}                                                                             \

void main_read_savestate(FILE_TAG_TYPE savestate_file)
  MAIN_SAVESTATE_BODY(READ);

void main_write_mem_savestate(FILE_TAG_TYPE savestate_file)
  MAIN_SAVESTATE_BODY(WRITE_MEM);

