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

#ifndef MEMORY_H
#define MEMORY_H


typedef enum
{
  DMA_START_IMMEDIATELY,
  DMA_START_VBLANK,
  DMA_START_HBLANK,
  DMA_START_SPECIAL,
  DMA_INACTIVE
} DMA_START_TYPE;

typedef enum
{
  DMA_16BIT,
  DMA_32BIT
} DMA_LENGTH_TYPE;

typedef enum
{
  DMA_NO_REPEAT,
  DMA_REPEAT
} DMA_REPEAT_TYPE;

typedef enum
{
  DMA_INCREMENT,
  DMA_DECREMENT,
  DMA_FIXED,
  DMA_RELOAD
} DMA_INCREMENT_TYPE;

typedef enum
{
  DMA_NO_IRQ,
  DMA_TRIGGER_IRQ
} DMA_IRQ_TYPE;

typedef enum
{
  DMA_DIRECT_SOUND_A,
  DMA_DIRECT_SOUND_B,
  DMA_NO_DIRECT_SOUND
} DMA_DS_TYPE;

typedef struct
{
  u32 dma_channel;
  u32 source_address;
  u32 dest_address;
  u32 length;
  DMA_REPEAT_TYPE repeat_type;
  DMA_DS_TYPE direct_sound_channel;
  DMA_INCREMENT_TYPE source_direction;
  DMA_INCREMENT_TYPE dest_direction;
  DMA_LENGTH_TYPE length_type;
  DMA_START_TYPE start_type;
  DMA_IRQ_TYPE irq;
} DMA_TRANSFER_TYPE;

typedef enum
{
  REG_DISPCNT  = 0x000,
  REG_DISPSTAT = 0x004 / 2,
  REG_VCOUNT   = 0x006 / 2,
  REG_BG0CNT   = 0x008 / 2,
  REG_BG1CNT   = 0x00A / 2,
  REG_BG2CNT   = 0x00C / 2,
  REG_BG3CNT   = 0x00E / 2,
  REG_BG0HOFS  = 0x010 / 2,
  REG_BG0VOFS  = 0x012 / 2,
  REG_BG1HOFS  = 0x014 / 2,
  REG_BG1VOFS  = 0x016 / 2,
  REG_BG2HOFS  = 0x018 / 2,
  REG_BG2VOFS  = 0x01A / 2,
  REG_BG3HOFS  = 0x01C / 2,
  REG_BG3VOFS  = 0x01E / 2,
  REG_BG2PA    = 0x020 / 2,
  REG_BG2PB    = 0x022 / 2,
  REG_BG2PC    = 0x024 / 2,
  REG_BG2PD    = 0x026 / 2,
  REG_BG2X_L   = 0x028 / 2,
  REG_BG2X_H   = 0x02A / 2,
  REG_BG2Y_L   = 0x02C / 2,
  REG_BG2Y_H   = 0x02E / 2,
  REG_BG3PA    = 0x030 / 2,
  REG_BG3PB    = 0x032 / 2,
  REG_BG3PC    = 0x034 / 2,
  REG_BG3PD    = 0x036 / 2,
  REG_BG3X_L   = 0x038 / 2,
  REG_BG3X_H   = 0x03A / 2,
  REG_BG3Y_L   = 0x03C / 2,
  REG_BG3Y_H   = 0x03E / 2,
  REG_WIN0H    = 0x040 / 2,
  REG_WIN1H    = 0x042 / 2,
  REG_WIN0V    = 0x044 / 2,
  REG_WIN1V    = 0x046 / 2,
  REG_WININ    = 0x048 / 2,
  REG_WINOUT   = 0x04A / 2,
  REG_BLDCNT   = 0x050 / 2,
  REG_BLDALPHA = 0x052 / 2,
  REG_BLDY     = 0x054 / 2,
  REG_TM0D     = 0x100 / 2,
  REG_TM0CNT   = 0x102 / 2,
  REG_TM1D     = 0x104 / 2,
  REG_TM1CNT   = 0x106 / 2,
  REG_TM2D     = 0x108 / 2,
  REG_TM2CNT   = 0x10A / 2,
  REG_TM3D     = 0x10C / 2,
  REG_TM3CNT   = 0x10E / 2,
  REG_P1       = 0x130 / 2,
  REG_P1CNT    = 0x132 / 2,
  REG_RCNT     = 0x134 / 2,
  REG_IE       = 0x200 / 2,
  REG_IF       = 0x202 / 2,
  REG_IME      = 0x208 / 2,
  REG_HALTCNT  = 0x300 / 2
} HARDWARE_REGISTER;


extern char gamepak_filename[MAX_FILE];
extern char gamepak_filename_raw[MAX_FILE];

extern FILE_TAG_TYPE gamepak_file_large;

extern u8 *gamepak_rom;
extern u8 *gamepak_shelter;

extern u32 gamepak_ram_buffer_size;

extern u32 oam_update;

extern DMA_TRANSFER_TYPE dma[4];

extern u16 palette_ram[1024 / 2];
extern u16 oam_ram[1024 / 2];
extern u16 io_registers[1024 / 2];
extern u8 ewram[1024 * 256 * 2];
extern u8 iwram[1024 * 32 * 2];
extern u8 vram[1024 * 96 * 1];

extern u8 bios_rom[1024 * 16 * 2];
extern u32 bios_read_protect;

extern u8 data_waitstate_cycles_n[2][16];
extern u8 data_waitstate_cycles_s[2][16];
extern u8 code_waitstate_cycles_s[2][16];

extern const u32 obj_address[2];

extern u8 *memory_map_read[8 * 1024];
extern u8 *memory_map_write[8 * 1024];

extern u32 reg[64];

extern u32 tower_hack;

u8  read_memory8(u32 address);
u32 read_memory16(u32 address);
u16 read_memory16_signed(u32 address);
u32 read_memory32(u32 address);

CPU_ALERT_TYPE write_memory8(u32 address, u8 value);
CPU_ALERT_TYPE write_memory16(u32 address, u16 value);
CPU_ALERT_TYPE write_memory32(u32 address, u32 value);

CPU_ALERT_TYPE write_io_register8(u32 address, u32 value);
CPU_ALERT_TYPE write_io_register16(u32 address, u32 value);
CPU_ALERT_TYPE write_io_register32(u32 address, u32 value);

CPU_ALERT_TYPE dma_transfer(DMA_TRANSFER_TYPE *dma);

u8 *memory_region(u32 address, u32 *memory_limit);

u8 *load_gamepak_page(u32 physical_index);

s8 load_bios(char *name);
s8 load_gamepak(char *name);
u8 load_backup(char *name);

u8  read_backup(u32 address);
u32 read_eeprom(void);
void write_eeprom(u32 address, u32 value);
void write_backup(u32 address, u32 value);
void write_rtc(u32 address, u32 value);

s8 parse_config_line(char *current_line, char *current_variable,
 char *current_value);

void init_memory(void);
void init_gamepak_buffer(void);

void memory_term(void);

void update_backup(void);
void update_backup_force(void);

void bios_region_read_allow(void);
// void bios_region_read_protect(void);

extern u8 *write_mem_ptr;

void load_state(char *savestate_filename);
void save_state(char *savestate_filename, u16 *screen_capture);


#endif /* MEMORY_H */
