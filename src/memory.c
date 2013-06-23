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


#define CONFIG_FILENAME "game_config.txt"

static u8 save_backup(char *name);

static u32 encode_bcd(u8 value);

static char *skip_spaces(char *line_ptr);
static s8 load_game_config(char *gamepak_title, char *gamepak_code,
 char *gamepak_maker);

static s32 load_gamepak_raw(char *name);
static u32 evict_gamepak_page(void);
static void init_memory_gamepak(void);

static void waitstate_control(u32 value);


char backup_filename[MAX_FILE];

char gamepak_filename[MAX_FILE];
char gamepak_filename_raw[MAX_FILE];

static u8 ALIGN_DATA savestate_write_buffer[506947] = { 0 };
u8 *write_mem_ptr;

// read data (non sequential)
u8 ALIGN_DATA data_waitstate_cycles_n[2][16] =
{
 /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
  { 1, 1, 3, 1, 1, 1, 1, 1, 5, 5, 5, 5, 5, 5, 5, 5 }, /* 8,16bit */
  { 1, 1, 6, 1, 1, 2, 2, 1, 7, 7, 9, 9,13,13, 5, 5 }  /* 32bit */
};

// read data (sequential)
u8 ALIGN_DATA data_waitstate_cycles_s[2][16] =
{
 /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
  { 1, 1, 3, 1, 1, 1, 1, 1, 3, 3, 5, 5, 9, 9, 5, 5 }, /* 8,16bit */
  { 1, 1, 6, 1, 1, 2, 2, 1, 5, 5, 9, 9,17,17, 5, 5 }  /* 32bit */
};

// read opecode
u8 ALIGN_DATA code_waitstate_cycles_s[2][16] =
{
 /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
  { 1, 1, 3, 1, 1, 1, 1, 1, 3, 3, 5, 5, 9, 9, 5, 5 }, /* 8,16bit */
  { 1, 1, 6, 1, 1, 2, 2, 1, 5, 5, 9, 9,17,17, 5, 5 }  /* 32bit */
};

const u32 ALIGN_DATA obj_address[2] = { 0x10000, 0x14000 };

u16 ALIGN_DATA  palette_ram[1024 / 2];   // 1K byte
u16 ALIGN_DATA      oam_ram[1024 / 2];   // 1K byte
u16 ALIGN_DATA io_registers[1024 / 2];   // 1K byte
u8  ALIGN_DATA    ewram[1024 * 256 * 2]; // 256K byte + 256K byte
u8  ALIGN_DATA    iwram[1024 *  32 * 2]; //  32K byte +  32K byte
u8  ALIGN_DATA     vram[1024 *  96 * 1]; //  96K byte
u8  ALIGN_DATA bios_rom[1024 *  16 * 2]; //  16K byte +  16K byte
u32 bios_read_protect;

// Up to 128kb, store SRAM, flash ROM, or EEPROM here.
static u8 ALIGN_DATA gamepak_backup[1024 * 128] = { 0 };

// Keeps us knowing how much we have left.
u8 *gamepak_rom;
u8 *gamepak_shelter;
u32 gamepak_size;

u32 tower_hack = 0;

DMA_TRANSFER_TYPE ALIGN_DATA dma[4];

// Picks a page to evict
u32 page_time = 0;

typedef struct
{
  u32 page_timestamp;
  u32 physical_index;
} GAMEPAK_SWAP_ENTRY_TYPE;

u32 gamepak_ram_buffer_size;
u32 gamepak_ram_pages;

// Enough to map the gamepak RAM space.
GAMEPAK_SWAP_ENTRY_TYPE *gamepak_memory_map;

// This is global so that it can be kept open for large ROMs to swap
// pages from, so there's no slowdown with opening and closing the file
// a lot.

FILE_TAG_TYPE gamepak_file_large = -1;

u8 direct_map_vram = 0;

// Writes to these respective locations should trigger an update
// so the related subsystem may react to it.

// If OAM is written to:
u32 oam_update = 1;


// RTC
typedef enum
{
  RTC_DISABLED,
  RTC_IDLE,
  RTC_COMMAND,
  RTC_OUTPUT_DATA,
  RTC_INPUT_DATA
} RTC_STATE_TYPE;

typedef enum
{
  RTC_COMMAND_RESET            = 0x60,
  RTC_COMMAND_WRITE_STATUS     = 0x62,
  RTC_COMMAND_READ_STATUS      = 0x63,
  RTC_COMMAND_OUTPUT_TIME_FULL = 0x65,
  RTC_COMMAND_OUTPUT_TIME      = 0x67
} RTC_COMMAND_TYPE;

typedef enum
{
  RTC_WRITE_TIME,
  RTC_WRITE_TIME_FULL,
  RTC_WRITE_STATUS
} RTC_WRITE_MODE_TYPE;

RTC_STATE_TYPE rtc_state = RTC_DISABLED;
RTC_WRITE_MODE_TYPE rtc_write_mode;

u8  ALIGN_DATA rtc_registers[3];
u32 ALIGN_DATA rtc_data[12];
u32 rtc_command;
u32 rtc_status = 0x40;
u32 rtc_data_bytes;
s32 rtc_bit_count;


// If the backup space is written (only update once this hits 0)
u8 backup_update = 0;

// Write out backup file this many cycles after the most recent
// backup write.
const u8 write_backup_delay = 10;

typedef enum
{
  BACKUP_SRAM,
  BACKUP_FLASH,
  BACKUP_EEPROM,
  BACKUP_NONE
} BACKUP_TYPE_TYPE;

typedef enum
{
  SRAM_SIZE_32KB,
  SRAM_SIZE_64KB
} SRAM_SIZE_TYPE;

// Keep it 32KB until the upper 64KB is accessed, then make it 64KB.

BACKUP_TYPE_TYPE backup_type = BACKUP_NONE;
SRAM_SIZE_TYPE sram_size = SRAM_SIZE_32KB;

typedef enum
{
  FLASH_BASE_MODE,
  FLASH_ERASE_MODE,
  FLASH_ID_MODE,
  FLASH_WRITE_MODE,
  FLASH_BANKSWITCH_MODE
} FLASH_MODE_TYPE;

typedef enum
{
  FLASH_SIZE_64KB,
  FLASH_SIZE_128KB
} FLASH_SIZE_TYPE;

typedef enum
{
  FLASH_DEVICE_MACRONIX_64KB   = 0x1C,
  FLASH_DEVICE_AMTEL_64KB      = 0x3D,
  FLASH_DEVICE_SST_64K         = 0xD4,
  FLASH_DEVICE_PANASONIC_64KB  = 0x1B,
  FLASH_DEVICE_MACRONIX_128KB  = 0x09
} FLASH_DEVICE_ID_TYPE;

typedef enum
{
  FLASH_MANUFACTURER_MACRONIX  = 0xC2,
  FLASH_MANUFACTURER_AMTEL     = 0x1F,
  FLASH_MANUFACTURER_PANASONIC = 0x32,
  FLASH_MANUFACTURER_SST       = 0xBF
} FLASH_MANUFACTURER_ID_TYPE;

FLASH_MODE_TYPE flash_mode = FLASH_BASE_MODE;
u8 flash_command_position = 0;
u8 *flash_bank_ptr = gamepak_backup;

FLASH_DEVICE_ID_TYPE flash_device_id = FLASH_DEVICE_MACRONIX_64KB;
FLASH_MANUFACTURER_ID_TYPE flash_manufacturer_id = FLASH_MANUFACTURER_MACRONIX;
FLASH_SIZE_TYPE flash_size = FLASH_SIZE_64KB;

typedef enum
{
  EEPROM_512_BYTE,
  EEPROM_8_KBYTE
} EEPROM_SIZE_TYPE;

typedef enum
{
  EEPROM_BASE_MODE,
  EEPROM_READ_MODE,
  EEPROM_READ_HEADER_MODE,
  EEPROM_ADDRESS_MODE,
  EEPROM_WRITE_MODE,
  EEPROM_WRITE_ADDRESS_MODE,
  EEPROM_ADDRESS_FOOTER_MODE,
  EEPROM_WRITE_FOOTER_MODE
} EEPROM_MODE_TYPE;

EEPROM_SIZE_TYPE eeprom_size = EEPROM_512_BYTE;
EEPROM_MODE_TYPE eeprom_mode = EEPROM_BASE_MODE;
u32 eeprom_address_length;
u32 eeprom_address = 0;
s32 eeprom_counter = 0;
u8 ALIGN_DATA eeprom_buffer[8];


u8 read_backup(u32 address)
{
  u8 value = 0;

  if(backup_type == BACKUP_NONE)
  {
    backup_type = BACKUP_SRAM;
  }

  switch(backup_type)
  {
    case BACKUP_SRAM:
      value = gamepak_backup[address];
      break;

    case BACKUP_FLASH:
      if(flash_mode == FLASH_ID_MODE)
      {
        /* ID manufacturer type */
        if(address == 0x0000)
        {
          value = flash_manufacturer_id;
        }
        else
        /* ID device type */
        if(address == 0x0001)
        {
          value = flash_device_id;
        }
      }
      else
      {
        value = flash_bank_ptr[address];
      }
      break;

    case BACKUP_EEPROM:
      enable_tilt_sensor = 1;

      switch(address)
      {
        case 0x8200:
          // Lower 8 bits of X axis
          value = tilt_sensorX & 0xFF;
          break;

        case 0x8300:
          // Upper 4 bits of X axis,
          // and Bit7: ADC Status (0=Busy, 1=Ready)
          value = (tilt_sensorX >> 8) | 0x80;
          break;

        case 0x8400:
          // Lower 8 bits of Y axis
          value = tilt_sensorY & 0xFF;
          break;

        case 0x8500:
          // Upper 4 bits of Y axis
          value = tilt_sensorY >> 8;
          break;
      }
      break;
  }

  return value;
}

#define READ_BACKUP8()                                                        \
  value = read_backup(address & 0xFFFF)                                       \

#define READ_BACKUP16()                                                       \
  value = 0                                                                   \

#define READ_BACKUP32()                                                       \
  value = 0                                                                   \


// EEPROM is 512 bytes by default; it is autodetecte as 8KB if
// 14bit address DMAs are made (this is done in the DMA handler).

void write_eeprom(u32 address, u32 value)
{
  // eeprom_v126 ?
  // ROM is restricted to 8000000h-9FFFeFFh
  // (max.1FFFF00h bytes = 32MB minus 256 bytes)
  if(gamepak_size > 0x1FFFF00)
    gamepak_size = 0x1FFFF00;

  switch(eeprom_mode)
  {
    case EEPROM_BASE_MODE:
      backup_type = BACKUP_EEPROM;
      eeprom_buffer[0] |= (value & 0x01) << (1 - eeprom_counter);
      eeprom_counter++;
      if(eeprom_counter == 2)
      {
        if(eeprom_size == EEPROM_512_BYTE)
          eeprom_address_length = 6;
        else
          eeprom_address_length = 14;

        eeprom_counter = 0;

        switch(eeprom_buffer[0] & 0x03)
        {
          case 0x02:
            eeprom_mode = EEPROM_WRITE_ADDRESS_MODE;
            break;

          case 0x03:
            eeprom_mode = EEPROM_ADDRESS_MODE;
            break;
        }
        ADDRESS16(eeprom_buffer, 0) = 0;
      }
      break;

    case EEPROM_ADDRESS_MODE:
    case EEPROM_WRITE_ADDRESS_MODE:
      eeprom_buffer[eeprom_counter / 8]
       |= (value & 0x01) << (7 - (eeprom_counter % 8));
      eeprom_counter++;
      if(eeprom_counter == eeprom_address_length)
      {
        if(eeprom_size == EEPROM_512_BYTE)
        {
          eeprom_address =
           (ADDRESS16(eeprom_buffer, 0) >> 2) * 8;
        }
        else
        {
          eeprom_address = (((u32)eeprom_buffer[1] >> 2) |
           ((u32)eeprom_buffer[0] << 6)) * 8;
        }

        ADDRESS16(eeprom_buffer, 0) = 0;
        eeprom_counter = 0;

        if(eeprom_mode == EEPROM_ADDRESS_MODE)
        {
          eeprom_mode = EEPROM_ADDRESS_FOOTER_MODE;
        }
        else
        {
          eeprom_mode = EEPROM_WRITE_MODE;
          memset(gamepak_backup + eeprom_address, 0, 8);
        }
      }
      break;

    case EEPROM_WRITE_MODE:
      gamepak_backup[eeprom_address + (eeprom_counter / 8)] |=
       (value & 0x01) << (7 - (eeprom_counter % 8));
      eeprom_counter++;
      if(eeprom_counter == 64)
      {
        backup_update = write_backup_delay;
        eeprom_counter = 0;
        eeprom_mode = EEPROM_WRITE_FOOTER_MODE;
      }
      break;

    case EEPROM_ADDRESS_FOOTER_MODE:
    case EEPROM_WRITE_FOOTER_MODE:
      eeprom_counter = 0;
      if(eeprom_mode == EEPROM_ADDRESS_FOOTER_MODE)
      {
        eeprom_mode = EEPROM_READ_HEADER_MODE;
      }
      else
      {
        eeprom_mode = EEPROM_BASE_MODE;
      }
      break;
  }
}


#define READ_MEMORY_GAMEPAK(type)                                             \
  u32 gamepak_index = address >> 15;                                          \
  u8 *map = memory_map_read[gamepak_index];                                   \
                                                                              \
  if(map == NULL)                                                             \
    map = load_gamepak_page(gamepak_index & 0x3FF);                           \
                                                                              \
  value = ADDRESS##type(map, address & 0x7FFF)                                \


#define READ_OPEN8()                                                          \
  if(reg[REG_CPSR] & 0x20)                                                    \
    value = read_memory8(reg[REG_PC] + 2 + (address & 0x01));                 \
  else                                                                        \
    value = read_memory8(reg[REG_PC] + 4 + (address & 0x03))                  \

#define READ_OPEN16()                                                         \
  if(reg[REG_CPSR] & 0x20)                                                    \
    value = read_memory16(reg[REG_PC] + 2);                                   \
  else                                                                        \
    value = read_memory16(reg[REG_PC] + 4 + (address & 0x02))                 \

#define READ_OPEN32()                                                         \
  if(reg[REG_CPSR] & 0x20)                                                    \
  {                                                                           \
    u32 current_instruction = read_memory16(reg[REG_PC] + 2);                 \
    value = current_instruction | (current_instruction << 16);                \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    value = read_memory32(reg[REG_PC] + 4);                                   \
  }                                                                           \


u32 read_eeprom(void)
{
  u32 value;

  switch(eeprom_mode)
  {
    case EEPROM_BASE_MODE:
      value = 1;
      break;

    case EEPROM_READ_MODE:
      value = (gamepak_backup[eeprom_address + (eeprom_counter / 8)] >>
       (7 - (eeprom_counter % 8))) & 0x01;
      eeprom_counter++;
      if(eeprom_counter == 64)
      {
        eeprom_counter = 0;
        eeprom_mode = EEPROM_BASE_MODE;
      }
      break;

    case EEPROM_READ_HEADER_MODE:
      value = 0;
      eeprom_counter++;
      if(eeprom_counter == 4)
      {
        eeprom_mode = EEPROM_READ_MODE;
        eeprom_counter = 0;
      }
      break;

    default:
      value = 0;
      break;
  }

  return value;
}


#define READ_MEMORY(type)                                                     \
  switch(address >> 24)                                                       \
  {                                                                           \
    case 0x00:                                                                \
      /* BIOS */                                                              \
      if(reg[REG_PC] >= 0x4000)                                               \
      {                                                                       \
        if(address < 0x4000)                                                  \
          value = ADDRESS##type(&bios_read_protect, address & 0x03);          \
        else                                                                  \
          goto read_open;                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        value = ADDRESS##type(bios_rom, address & 0x3FFF);                    \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x02:                                                                \
      /* external work RAM */                                                 \
      address = (address & 0x7FFF) + ((address & 0x38000) << 1);              \
      value = ADDRESS##type(ewram + 0x8000, address);                         \
      break;                                                                  \
                                                                              \
    case 0x03:                                                                \
      /* internal work RAM */                                                 \
      value = ADDRESS##type(iwram + 0x8000, address & 0x7FFF);                \
      break;                                                                  \
                                                                              \
    case 0x04:                                                                \
      /* I/O registers */                                                     \
      if(address < 0x04000400)                                                \
        value = ADDRESS##type(io_registers, address & 0x3FF);                 \
      else                                                                    \
        goto read_open;                                                       \
      break;                                                                  \
                                                                              \
    case 0x05:                                                                \
      /* palette RAM */                                                       \
      value = ADDRESS##type(palette_ram, address & 0x3FF);                    \
      break;                                                                  \
                                                                              \
    case 0x06:                                                                \
      /* VRAM */                                                              \
      if(address & 0x10000)                                                   \
        value = ADDRESS##type(vram, address & 0x17FFF);                       \
      else                                                                    \
        value = ADDRESS##type(vram, address & 0x1FFFF);                       \
      break;                                                                  \
                                                                              \
    case 0x07:                                                                \
      /* OAM RAM */                                                           \
      value = ADDRESS##type(oam_ram, address & 0x3FF);                        \
      break;                                                                  \
                                                                              \
    case 0x08: case 0x09:                                                     \
    case 0x0A: case 0x0B:                                                     \
    case 0x0C:                                                                \
      /* gamepak ROM */                                                       \
      if((address & 0x1FFFFFF) < gamepak_size)                                \
      {                                                                       \
        READ_MEMORY_GAMEPAK(type);                                            \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        value = 0;                                                            \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x0D:                                                                \
      if((address & 0x1FFFFFF) < gamepak_size)                                \
      {                                                                       \
        READ_MEMORY_GAMEPAK(type);                                            \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        value = read_eeprom();                                                \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x0E:                                                                \
    case 0x0F:                                                                \
      READ_BACKUP##type();                                                    \
      break;                                                                  \
                                                                              \
    default:                                                                  \
    read_open:                                                                \
      READ_OPEN##type();                                                      \
      break;                                                                  \
  }                                                                           \


static void waitstate_control(u32 value)
{
  u8 i;
  const u8 waitstate_table[4] = { 5, 4, 3, 9 };
  const u8 gamepak_ws0_seq[2] = { 3, 2 };
  const u8 gamepak_ws1_seq[2] = { 5, 2 };
  const u8 gamepak_ws2_seq[2] = { 9, 2 };

  /* SRAM Wait Control */
  data_waitstate_cycles_n[0][0x0e] = data_waitstate_cycles_n[0][0x0e]
   = waitstate_table[value & 0x03];

  /* Wait State First Access (16bit) */
  data_waitstate_cycles_n[0][0x08] = data_waitstate_cycles_n[0][0x09]
   = waitstate_table[(value >> 2) & 0x03];

  data_waitstate_cycles_n[0][0x0A] = data_waitstate_cycles_n[0][0x0B]
   = waitstate_table[(value >> 5) & 0x03];

  data_waitstate_cycles_n[0][0x0C] = data_waitstate_cycles_n[0][0x0D]
   = waitstate_table[(value >> 8) & 0x03];

  /* Wait State Second Access (16bit) */
  data_waitstate_cycles_s[0][0x08] = data_waitstate_cycles_s[0][0x09]
   = gamepak_ws0_seq[(value >> 4) & 0x01];

  data_waitstate_cycles_s[0][0x0A] = data_waitstate_cycles_s[0][0x0B]
   = gamepak_ws1_seq[(value >> 7) & 0x01];

  data_waitstate_cycles_s[0][0x0C] = data_waitstate_cycles_s[0][0x0D]
   = gamepak_ws2_seq[(value >> 10) & 0x01];

  /* Wait State First Access (32bit) */
  data_waitstate_cycles_n[1][0x08] = data_waitstate_cycles_n[1][0x09]
   = data_waitstate_cycles_n[0][0x08] + data_waitstate_cycles_s[0][0x08];

  data_waitstate_cycles_n[1][0x0A] = data_waitstate_cycles_n[1][0x0B]
   = data_waitstate_cycles_n[0][0x0A] + data_waitstate_cycles_s[0][0x0A];

  data_waitstate_cycles_n[1][0x0C] = data_waitstate_cycles_n[1][0x0D]
  =  data_waitstate_cycles_n[0][0x0C] + data_waitstate_cycles_s[0][0x0C];

  /* Wait State Second Access (32bit) */
  data_waitstate_cycles_s[1][0x08] = data_waitstate_cycles_s[1][0x09]
   = data_waitstate_cycles_s[0][0x08] * 2;

  data_waitstate_cycles_s[1][0x0A] = data_waitstate_cycles_s[1][0x0B]
   = data_waitstate_cycles_s[0][0x0A] * 2;

  data_waitstate_cycles_s[1][0x0C] = data_waitstate_cycles_s[1][0x0D]
  =  data_waitstate_cycles_s[0][0x0C] * 2;

  /* gamepak prefetch */
  if(value & 0x4000)
  {
    for(i = 0x08; i <= 0x0D; i++)
    {
      code_waitstate_cycles_s[0][i] = 3;
      code_waitstate_cycles_s[1][i] = 6;
    }
  }
  else
  {
    for(i = 0; i < 2; i++)
    {
      code_waitstate_cycles_s[i][0x08] = code_waitstate_cycles_s[i][0x09]
       = data_waitstate_cycles_s[i][0x08];

      code_waitstate_cycles_s[i][0x0A] = code_waitstate_cycles_s[i][0x0B]
       = data_waitstate_cycles_s[i][0x0A];

      code_waitstate_cycles_s[i][0x0C] = code_waitstate_cycles_s[i][0x0D]
       = data_waitstate_cycles_s[i][0x0C];
    }
  }

  ADDRESS16(io_registers, 0x204) =
   (ADDRESS16(io_registers, 0x204) & 0x8000) | (value & 0x7FFF);
}


#define TRIGGER_DMA(dma_number)                                               \
  if(value & 0x8000)                                                          \
  {                                                                           \
    if(dma[dma_number].start_type == DMA_INACTIVE)                            \
    {                                                                         \
      u32 start_type = (value >> 12) & 0x03;                                  \
      u32 dest_address =                                                      \
       ADDRESS32(io_registers, (dma_number * 12) + 0xB4) & 0x0FFFFFFF;        \
                                                                              \
      dma[dma_number].dma_channel = dma_number;                               \
      dma[dma_number].source_address =                                        \
       ADDRESS32(io_registers, (dma_number * 12) + 0xB0) & 0x0FFFFFFF;        \
      dma[dma_number].dest_address = dest_address;                            \
      dma[dma_number].source_direction = (value >> 7) & 0x03;                 \
      dma[dma_number].start_type = start_type;                                \
      dma[dma_number].irq = (value >> 14) & 0x01;                             \
                                                                              \
      /* If it is sound FIFO DMA make sure the settings are a certain way */  \
      if((dma_number >= 1) && (dma_number <= 2) &&                            \
         (start_type == DMA_START_SPECIAL))                                   \
      {                                                                       \
        dma[dma_number].repeat_type = DMA_REPEAT;                             \
        dma[dma_number].length_type = DMA_32BIT;                              \
        dma[dma_number].length = 4;                                           \
        dma[dma_number].dest_direction = DMA_FIXED;                           \
                                                                              \
        if(dest_address == 0x40000A4)                                         \
          dma[dma_number].direct_sound_channel = DMA_DIRECT_SOUND_B;          \
        else                                                                  \
          dma[dma_number].direct_sound_channel = DMA_DIRECT_SOUND_A;          \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        u32 length = ADDRESS16(io_registers, (dma_number * 12) + 0xB8);       \
                                                                              \
        if((dma_number == 3) && ((dest_address >> 24) == 0x0D) &&             \
           ((length & 0x1F) == 17))                                           \
        {                                                                     \
          eeprom_size = EEPROM_8_KBYTE;                                       \
        }                                                                     \
                                                                              \
        if(dma_number < 3)                                                    \
          length &= 0x3FFF;                                                   \
                                                                              \
        if(length == 0)                                                       \
        {                                                                     \
          if(dma_number == 3)                                                 \
            length = 0x10000;                                                 \
          else                                                                \
            length = 0x04000;                                                 \
        }                                                                     \
                                                                              \
        dma[dma_number].repeat_type = (value >> 9) & 0x01;                    \
        dma[dma_number].length = length;                                      \
        dma[dma_number].length_type = (value >> 10) & 0x01;                   \
        dma[dma_number].dest_direction = (value >> 5) & 0x03;                 \
                                                                              \
        if(start_type == DMA_START_IMMEDIATELY)                               \
          return dma_transfer(dma + dma_number);                              \
      }                                                                       \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      u32 start_type = (value >> 12) & 0x03;                                  \
                                                                              \
      dma[dma_number].dma_channel = dma_number;                               \
      dma[dma_number].source_direction = (value >> 7) & 0x03;                 \
      dma[dma_number].start_type = start_type;                                \
      dma[dma_number].irq = (value >> 14) & 0x01;                             \
                                                                              \
      if((dma_number >= 1) && (dma_number <= 2) &&                            \
         (start_type == DMA_START_SPECIAL))                                   \
      {                                                                       \
        dma[dma_number].repeat_type = DMA_REPEAT;                             \
        dma[dma_number].length_type = DMA_32BIT;                              \
        dma[dma_number].dest_direction = DMA_FIXED;                           \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        dma[dma_number].repeat_type = (value >> 9) & 0x01;                    \
        dma[dma_number].length_type = (value >> 10) & 0x01;                   \
        dma[dma_number].dest_direction = (value >> 5) & 0x03;                 \
      }                                                                       \
    }                                                                         \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    dma[dma_number].start_type = DMA_INACTIVE;                                \
    dma[dma_number].direct_sound_channel = DMA_NO_DIRECT_SOUND;               \
  }                                                                           \


#define ACCESS_REGISTER8_HIGH(address)                                        \
  value = (value << 8) | (ADDRESS8(io_registers, address))                    \

#define ACCESS_REGISTER8_LOW(address)                                         \
  value = ((ADDRESS8(io_registers, address + 1)) << 8) | value                \

#define ACCESS_REGISTER16_HIGH(address)                                       \
  value = (value << 16) | (ADDRESS16(io_registers, address))                  \

#define ACCESS_REGISTER16_LOW(address)                                        \
  value = ((ADDRESS16(io_registers, address + 2)) << 16) | value              \


CPU_ALERT_TYPE write_io_register8(u32 address, u32 value)
{
  switch(address)
  {
    case 0x00:
    {
      u16 bg_mode = io_registers[REG_DISPCNT] & 0x07;

      if((value & 0x07) != bg_mode)
        oam_update = 1;

      ADDRESS8(io_registers, 0x00) = value;
      break;
    }

    // DISPSTAT (lower byte)
    case 0x04:
      ADDRESS8(io_registers, 0x04) =
       (ADDRESS8(io_registers, 0x04) & 0x07) | (value & ~0x07);
      break;

    // VCOUNT
    case 0x06:
    case 0x07:
      /* Read only */
      break;

    // BG2 reference X
    case 0x28:
      ACCESS_REGISTER8_LOW(0x28);
      ACCESS_REGISTER16_LOW(0x28);
      affine_reference_x[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x28) = value;
      break;

    case 0x29:
      ACCESS_REGISTER8_HIGH(0x28);
      ACCESS_REGISTER16_LOW(0x28);
      affine_reference_x[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x28) = value;
      break;

    case 0x2A:
      ACCESS_REGISTER8_LOW(0x2A);
      ACCESS_REGISTER16_HIGH(0x28);
      affine_reference_x[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x28) = value;
      break;

    case 0x2B:
      ACCESS_REGISTER8_HIGH(0x2A);
      ACCESS_REGISTER16_HIGH(0x28);
      affine_reference_x[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x28) = value;
      break;

    // BG2 reference Y
    case 0x2C:
      ACCESS_REGISTER8_LOW(0x2C);
      ACCESS_REGISTER16_LOW(0x2C);
      affine_reference_y[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x2C) = value;
      break;

    case 0x2D:
      ACCESS_REGISTER8_HIGH(0x2C);
      ACCESS_REGISTER16_LOW(0x2C);
      affine_reference_y[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x2C) = value;
      break;

    case 0x2E:
      ACCESS_REGISTER8_LOW(0x2E);
      ACCESS_REGISTER16_HIGH(0x2C);
      affine_reference_y[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x2C) = value;
      break;

    case 0x2F:
      ACCESS_REGISTER8_HIGH(0x2E);
      ACCESS_REGISTER16_HIGH(0x2C);
      affine_reference_y[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x2C) = value;
      break;

    // BG3 reference X
    case 0x38:
      ACCESS_REGISTER8_LOW(0x38);
      ACCESS_REGISTER16_LOW(0x38);
      affine_reference_x[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x38) = value;
      break;

    case 0x39:
      ACCESS_REGISTER8_HIGH(0x38);
      ACCESS_REGISTER16_LOW(0x38);
      affine_reference_x[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x38) = value;
      break;

    case 0x3A:
      ACCESS_REGISTER8_LOW(0x3A);
      ACCESS_REGISTER16_HIGH(0x38);
      affine_reference_x[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x38) = value;
      break;

    case 0x3B:
      ACCESS_REGISTER8_HIGH(0x3A);
      ACCESS_REGISTER16_HIGH(0x38);
      affine_reference_x[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x38) = value;
      break;

    // BG3 reference Y
    case 0x3C:
      ACCESS_REGISTER8_LOW(0x3C);
      ACCESS_REGISTER16_LOW(0x3C);
      affine_reference_y[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x3C) = value;
      break;

    case 0x3D:
      ACCESS_REGISTER8_HIGH(0x3C);
      ACCESS_REGISTER16_LOW(0x3C);
      affine_reference_y[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x3C) = value;
      break;

    case 0x3E:
      ACCESS_REGISTER8_LOW(0x3E);
      ACCESS_REGISTER16_HIGH(0x3C);
      affine_reference_y[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x3C) = value;
      break;

    case 0x3F:
      ACCESS_REGISTER8_HIGH(0x3E);
      ACCESS_REGISTER16_HIGH(0x3C);
      affine_reference_y[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x3C) = value;
      break;

    // Sound 1 control sweep
    case 0x60:
      ACCESS_REGISTER8_LOW(0x60);
      gbc_sound_tone_control_sweep(value);
      ADDRESS16(io_registers, 0x60) = value;
      break;

    case 0x61:
      ACCESS_REGISTER8_HIGH(0x60);
      gbc_sound_tone_control_sweep(value);
      ADDRESS16(io_registers, 0x60) = value;
      break;

    // Sound 1 control duty/length/envelope

    case 0x62:
      ACCESS_REGISTER8_LOW(0x62);
      gbc_sound_tone_control_low(0, value);
      ADDRESS16(io_registers, 0x62) = value;
      break;

    case 0x63:
      ACCESS_REGISTER8_HIGH(0x62);
      gbc_sound_tone_control_low(0, value);
      ADDRESS16(io_registers, 0x62) = value;
      break;

    // Sound 1 control frequency
    case 0x64:
      ACCESS_REGISTER8_LOW(0x64);
      gbc_sound_tone_control_high(0, value);
      ADDRESS16(io_registers, 0x64) = value;
      break;

    case 0x65:
      ACCESS_REGISTER8_HIGH(0x64);
      gbc_sound_tone_control_high(0, value);
      ADDRESS16(io_registers, 0x64) = value;
      break;

    // Sound 2 control duty/length/envelope
    case 0x68:
      ACCESS_REGISTER8_LOW(0x68);
      gbc_sound_tone_control_low(1, value);
      ADDRESS16(io_registers, 0x68) = value;
      break;

    case 0x69:
      ACCESS_REGISTER8_HIGH(0x68);
      gbc_sound_tone_control_low(1, value);
      ADDRESS16(io_registers, 0x68) = value;
      break;

    // Sound 2 control frequency
    case 0x6C:
      ACCESS_REGISTER8_LOW(0x6C);
      gbc_sound_tone_control_high(1, value);
      ADDRESS16(io_registers, 0x6C) = value;
      break;

    case 0x6D:
      ACCESS_REGISTER8_HIGH(0x6C);
      gbc_sound_tone_control_high(1, value);
      ADDRESS16(io_registers, 0x6C) = value;
      break;

    // Sound 3 control wave
    case 0x70:
      ACCESS_REGISTER8_LOW(0x70);
      gbc_sound_wave_control(value);
      ADDRESS16(io_registers, 0x70) = value;
      break;

    case 0x71:
      ACCESS_REGISTER8_HIGH(0x70);
      gbc_sound_wave_control(value);
      ADDRESS16(io_registers, 0x70) = value;
      break;

    // Sound 3 control length/volume
    case 0x72:
      ACCESS_REGISTER8_LOW(0x72);
      gbc_sound_tone_control_low_wave(value);
      ADDRESS16(io_registers, 0x72) = value;
      break;

    case 0x73:
      ACCESS_REGISTER8_HIGH(0x72);
      gbc_sound_tone_control_low_wave(value);
      ADDRESS16(io_registers, 0x72) = value;
      break;

    // Sound 3 control frequency
    case 0x74:
      ACCESS_REGISTER8_LOW(0x74);
      gbc_sound_tone_control_high_wave(value);
      ADDRESS16(io_registers, 0x74) = value;
      break;

    case 0x75:
      ACCESS_REGISTER8_HIGH(0x74);
      gbc_sound_tone_control_high_wave(value);
      ADDRESS16(io_registers, 0x74) = value;
      break;

    // Sound 4 control length/envelope
    case 0x78:
      ACCESS_REGISTER8_LOW(0x78);
      gbc_sound_tone_control_low(3, value);
      ADDRESS16(io_registers, 0x78) = value;
      break;

    case 0x79:
      ACCESS_REGISTER8_HIGH(0x78);
      gbc_sound_tone_control_low(3, value);
      ADDRESS16(io_registers, 0x78) = value;
      break;

    // Sound 4 control frequency
    case 0x7C:
      ACCESS_REGISTER8_LOW(0x7C);
      gbc_sound_noise_control(value);
      ADDRESS16(io_registers, 0x7C) = value;
      break;

    case 0x7D:
      ACCESS_REGISTER8_HIGH(0x7C);
      gbc_sound_noise_control(value);
      ADDRESS16(io_registers, 0x7C) = value;
      break;

    // Sound control L
    case 0x80:
      ACCESS_REGISTER8_LOW(0x80);
      sound_control_low(value);
      ADDRESS16(io_registers, 0x80) = value;
      break;

    case 0x81:
      ACCESS_REGISTER8_HIGH(0x80);
      sound_control_low(value);
      ADDRESS16(io_registers, 0x80) = value;
      break;

    // Sound control H
    case 0x82:
      ACCESS_REGISTER8_LOW(0x82);
      sound_control_high(value);
      ADDRESS16(io_registers, 0x82) = value;
      break;

    case 0x83:
      ACCESS_REGISTER8_HIGH(0x82);
      sound_control_high(value);
      ADDRESS16(io_registers, 0x82) = value;
      break;

    // Sound control X
    case 0x84:
      sound_control_x(value);
      ADDRESS8(io_registers, 0x84) =
       (ADDRESS8(io_registers, 0x84) & 0x0F) | (value & 0xF0);
      break;

    // Sound wave RAM
    case 0x90: case 0x91: case 0x92: case 0x93:
    case 0x94: case 0x95: case 0x96: case 0x97:
    case 0x98: case 0x99: case 0x9A: case 0x9B:
    case 0x9C: case 0x9D: case 0x9E: case 0x9F:
      gbc_sound_wave_pattern_ram8(address, value);
      ADDRESS8(io_registers, address) = value;
      break;

    // Sound FIFO A
    case 0xA0: case 0xA1:
    case 0xA2: case 0xA3:
      ADDRESS8(io_registers, address) = value;
      sound_timer_queue32(0);
      break;

    // Sound FIFO B
    case 0xA4: case 0xA5:
    case 0xA6: case 0xA7:
      ADDRESS8(io_registers, address) = value;
      sound_timer_queue32(1);
      break;

    // DMA control (trigger byte)
    case 0xBB:
      ACCESS_REGISTER8_HIGH(0xBA);
      ADDRESS16(io_registers, 0xBA) = value;
      TRIGGER_DMA(0);
      break;

    case 0xC7:
      ACCESS_REGISTER8_HIGH(0xC6);
      ADDRESS16(io_registers, 0xC6) = value;
      TRIGGER_DMA(1);
      break;

    case 0xD3:
      ACCESS_REGISTER8_HIGH(0xD2);
      ADDRESS16(io_registers, 0xD2) = value;
      TRIGGER_DMA(2);
      break;

    case 0xDF:
      ACCESS_REGISTER8_HIGH(0xDE);
      ADDRESS16(io_registers, 0xDE) = value;
      TRIGGER_DMA(3);
      break;

    // Timer counts
    case 0x100:
      ACCESS_REGISTER8_LOW(0x100);
      timer_control_low(0, value);
      break;

    case 0x101:
      ACCESS_REGISTER8_HIGH(0x100);
      timer_control_low(0, value);
      break;

    case 0x104:
      ACCESS_REGISTER8_LOW(0x104);
      timer_control_low(1, value);
      break;

    case 0x105:
      ACCESS_REGISTER8_HIGH(0x104);
      timer_control_low(1, value);
      break;

    case 0x108:
      ACCESS_REGISTER8_LOW(0x108);
      timer_control_low(2, value);
      break;

    case 0x109:
      ACCESS_REGISTER8_HIGH(0x108);
      timer_control_low(2, value);
      break;

    case 0x10C:
      ACCESS_REGISTER8_LOW(0x10C);
      timer_control_low(3, value);
      break;

    case 0x10D:
      ACCESS_REGISTER8_HIGH(0x10C);
      timer_control_low(3, value);
      break;

    // Timer control
    case 0x102:
      timer_control_high(0, value);
      ADDRESS8(io_registers, 0x102) = value;
      break;

    case 0x106:
      timer_control_high(1, value);
      ADDRESS8(io_registers, 0x106) = value;
      break;

    case 0x10A:
      timer_control_high(2, value);
      ADDRESS8(io_registers, 0x10A) = value;
      break;

    case 0x10E:
      timer_control_high(3, value);
      ADDRESS8(io_registers, 0x10E) = value;
      break;

    // SIOCNT
    case 0x128:
      if(((io_registers[0x134 / 2] & 0x8000) == 0) &&
         ((io_registers[0x128 / 2] & 0x3000) == 0x2000))
      {
        // Multi-Player Mode
        value &= 0x83;
        ADDRESS8(io_registers, 0x128) = value | 0x0C;
      }
      else
      {
        ADDRESS8(io_registers, 0x128) = value;
      }
      break;

    // P1
    case 0x130:
    case 0x131:
      /* Read only */
      break;

    // IF
    case 0x202:
    case 0x203:
      ADDRESS8(io_registers, address) &= ~value;
      break;

    // WAITCNT
    case 0x204:
      ACCESS_REGISTER8_LOW(0x204);
      waitstate_control(value);
      break;

    case 0x205:
      ACCESS_REGISTER8_HIGH(0x204);
      waitstate_control(value);
      break;

    // Halt
    case 0x301:
      ADDRESS8(io_registers, 0x301) = value;

      if(value & 0x80)
      {
        reg[CPU_HALT_STATE] = CPU_STOP;
      }
      else
      {
        reg[CPU_HALT_STATE] = CPU_HALT;
      }
      return CPU_ALERT_HALT;

    default:
      ADDRESS8(io_registers, address) = value;
      break;
  }

  return CPU_ALERT_NONE;
}


CPU_ALERT_TYPE write_io_register16(u32 address, u32 value)
{
  switch(address)
  {
    case 0x00:
    {
      u16 bg_mode = io_registers[REG_DISPCNT] & 0x07;

      if((value & 0x07) != bg_mode)
        oam_update = 1;

      ADDRESS16(io_registers, 0x00) = value;
      break;
    }

    // DISPSTAT
    case 0x04:
      ADDRESS16(io_registers, 0x04) =
       (ADDRESS16(io_registers, 0x04) & 0x07) | (value & ~0x07);
      break;

    // VCOUNT
    case 0x06:
      /* Read only */
      break;

    // BG2 reference X
    case 0x28:
      ACCESS_REGISTER16_LOW(0x28);
      affine_reference_x[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x28) = value;
      break;

    case 0x2A:
      ACCESS_REGISTER16_HIGH(0x28);
      affine_reference_x[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x28) = value;
      break;

    // BG2 reference Y
    case 0x2C:
      ACCESS_REGISTER16_LOW(0x2C);
      affine_reference_y[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x2C) = value;
      break;

    case 0x2E:
      ACCESS_REGISTER16_HIGH(0x2C);
      affine_reference_y[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x2C) = value;
      break;

    // BG3 reference X
    case 0x38:
      ACCESS_REGISTER16_LOW(0x38);
      affine_reference_x[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x38) = value;
      break;

    case 0x3A:
      ACCESS_REGISTER16_HIGH(0x38);
      affine_reference_x[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x38) = value;
      break;

    // BG3 reference Y
    case 0x3C:
      ACCESS_REGISTER16_LOW(0x3C);
      affine_reference_y[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x3C) = value;
      break;

    case 0x3E:
      ACCESS_REGISTER16_HIGH(0x3C);
      affine_reference_y[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x3C) = value;
      break;

    // Sound 1 control sweep
    case 0x60:
      gbc_sound_tone_control_sweep(value);
      ADDRESS16(io_registers, 0x60) = value;
      break;

    // Sound 1 control duty/length/envelope
    case 0x62:
      gbc_sound_tone_control_low(0, value);
      ADDRESS16(io_registers, 0x62) = value;
      break;

    // Sound 1 control frequency
    case 0x64:
      gbc_sound_tone_control_high(0, value);
      ADDRESS16(io_registers, 0x64) = value;
      break;

    // Sound 2 control duty/length/envelope
    case 0x68:
      gbc_sound_tone_control_low(1, value);
      ADDRESS16(io_registers, 0x68) = value;
      break;

    // Sound 2 control frequency
    case 0x6C:
      gbc_sound_tone_control_high(1, value);
      ADDRESS16(io_registers, 0x6C) = value;
      break;

    // Sound 3 control wave
    case 0x70:
      gbc_sound_wave_control(value);
      ADDRESS16(io_registers, 0x70) = value;
      break;

    // Sound 3 control length/volume
    case 0x72:
      gbc_sound_tone_control_low_wave(value);
      ADDRESS16(io_registers, 0x72) = value;
      break;

    // Sound 3 control frequency
    case 0x74:
      gbc_sound_tone_control_high_wave(value);
      ADDRESS16(io_registers, 0x74) = value;
      break;

    // Sound 4 control length/envelope
    case 0x78:
      gbc_sound_tone_control_low(3, value);
      ADDRESS16(io_registers, 0x78) = value;
      break;

    // Sound 4 control frequency
    case 0x7C:
      gbc_sound_noise_control(value);
      ADDRESS16(io_registers, 0x7C) = value;
      break;

    // Sound control L
    case 0x80:
      sound_control_low(value);
      ADDRESS16(io_registers, 0x80) = value;
      break;

    // Sound control H
    case 0x82:
      sound_control_high(value);
      ADDRESS16(io_registers, 0x82) = value;
      break;

    // Sound control X
    case 0x84:
      sound_control_x(value);
      ADDRESS16(io_registers, 0x84) =
       (ADDRESS16(io_registers, 0x84) & 0x000F) | (value & 0xFFF0);
      break;

    // Sound wave RAM
    case 0x90: case 0x92: case 0x94: case 0x96:
    case 0x98: case 0x9A: case 0x9C: case 0x9E:
      gbc_sound_wave_pattern_ram16(address, value);
      ADDRESS16(io_registers, address) = value;
      break;

    // Sound FIFO A
    case 0xA0:
    case 0xA2:
      ADDRESS16(io_registers, address) = value;
      sound_timer_queue32(0);
      break;

    // Sound FIFO B
    case 0xA4:
    case 0xA6:
      ADDRESS16(io_registers, address) = value;
      sound_timer_queue32(1);
      break;

    // DMA control
    case 0xBA:
      ADDRESS16(io_registers, 0xBA) = value;
      TRIGGER_DMA(0);
      break;

    case 0xC6:
      ADDRESS16(io_registers, 0xC6) = value;
      TRIGGER_DMA(1);
      break;

    case 0xD2:
      ADDRESS16(io_registers, 0xD2) = value;
      TRIGGER_DMA(2);
      break;

    case 0xDE:
      ADDRESS16(io_registers, 0xDE) = value;
      TRIGGER_DMA(3);
      break;

    // Timer counts
    case 0x100:
      timer_control_low(0, value);
      break;

    case 0x104:
      timer_control_low(1, value);
      break;

    case 0x108:
      timer_control_low(2, value);
      break;

    case 0x10C:
      timer_control_low(3, value);
      break;

    // Timer control
    case 0x102:
      timer_control_high(0, value);
      ADDRESS16(io_registers, 0x102) = value;
      break;

    case 0x106:
      timer_control_high(1, value);
      ADDRESS16(io_registers, 0x106) = value;
      break;

    case 0x10A:
      timer_control_high(2, value);
      ADDRESS16(io_registers, 0x10A) = value;
      break;

    case 0x10E:
      timer_control_high(3, value);
      ADDRESS16(io_registers, 0x10E) = value;
      break;

    // SIOCNT
    case 0x128:
      if(((io_registers[0x134 / 2] & 0x8000) == 0) &&
         ((io_registers[0x128 / 2] & 0x3000) == 0x2000))
      {
        // Multi-Player Mode
        value &= 0xFF83;
        ADDRESS16(io_registers, 0x128) = value | 0x0C;
      }
      else
      {
        ADDRESS16(io_registers, 0x128) = value;
      }
      break;

    // P1
    case 0x130:
      /* Read only */
      break;

    // Interrupt flag
    case 0x202:
      ADDRESS16(io_registers, 0x202) &= ~value;
      break;

    // WAITCNT
    case 0x204:
      waitstate_control(value);
      break;

    // Halt
    case 0x300:
      ADDRESS16(io_registers, 0x300) = value;
      if(value & 0x8000)
        reg[CPU_HALT_STATE] = CPU_STOP;
      else
        reg[CPU_HALT_STATE] = CPU_HALT;
      return CPU_ALERT_HALT;

    default:
      ADDRESS16(io_registers, address) = value;
      break;
  }

  return CPU_ALERT_NONE;
}


CPU_ALERT_TYPE write_io_register32(u32 address, u32 value)
{
  switch(address)
  {
    // BG2 reference X
    case 0x28:
      affine_reference_x[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x28) = value;
      break;

    // BG2 reference Y
    case 0x2C:
      affine_reference_y[0] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x2C) = value;
      break;

    // BG3 reference X
    case 0x38:
      affine_reference_x[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x38) = value;
      break;

    // BG3 reference Y
    case 0x3C:
      affine_reference_y[1] = (s32)(value << 4) >> 4;
      ADDRESS32(io_registers, 0x3C) = value;
      break;

    // Sound FIFO A
    case 0xA0:
      ADDRESS32(io_registers, 0xA0) = value;
      sound_timer_queue32(0);
      break;

    // Sound FIFO B
    case 0xA4:
      ADDRESS32(io_registers, 0xA4) = value;
      sound_timer_queue32(1);
      break;

    default:
    {
      CPU_ALERT_TYPE alert_low =
        write_io_register16(address, value & 0xFFFF);

      CPU_ALERT_TYPE alert_high =
        write_io_register16(address + 2, value >> 16);

      return (alert_high | alert_low);
    }
  }

  return CPU_ALERT_NONE;
}


#define WRITE_PALETTE8(address, value)                                        \
  ADDRESS16(palette_ram, (address) & ~0x01) = (value << 8) | value            \

#define WRITE_PALETTE16(address, value)                                       \
  ADDRESS16(palette_ram, (address)) = value                                   \

#define WRITE_PALETTE32(address, value)                                       \
  ADDRESS32(palette_ram, (address)) = value                                   \


void write_backup(u32 address, u32 value)
{
  value &= 0xFF;

  if(backup_type == BACKUP_NONE)
  {
    backup_type = BACKUP_SRAM;
  }

  // gamepak SRAM or Flash ROM
  if((address == 0x5555) && (flash_mode != FLASH_WRITE_MODE))
  {
    if((flash_command_position == 0) && (value == 0xAA))
    {
      backup_type = BACKUP_FLASH;
      flash_command_position = 1;
    }

    if(flash_command_position == 2)
    {
      switch(value)
      {
        case 0x90:
          // Enter ID mode, this also tells the emulator that we're using
          // flash, not SRAM

          if(flash_mode == FLASH_BASE_MODE)
            flash_mode = FLASH_ID_MODE;
          break;

        case 0x80:
          // Enter erase mode
          if(flash_mode == FLASH_BASE_MODE)
            flash_mode = FLASH_ERASE_MODE;
          break;

        case 0xF0:
          // Terminate ID mode
          if(flash_mode == FLASH_ID_MODE)
            flash_mode = FLASH_BASE_MODE;
          break;

        case 0xA0:
          // Write mode
          if(flash_mode == FLASH_BASE_MODE)
            flash_mode = FLASH_WRITE_MODE;
          break;

        case 0xB0:
          // Bank switch
          // Here the chip is now officially 128KB.
          flash_size = FLASH_SIZE_128KB;
          if(flash_mode == FLASH_BASE_MODE)
            flash_mode = FLASH_BANKSWITCH_MODE;
          break;

        case 0x10:
          // Erase chip
          if(flash_mode == FLASH_ERASE_MODE)
          {
            if(flash_size == FLASH_SIZE_64KB)
              memset(gamepak_backup, 0xFF, 1024 * 64);
            else
              memset(gamepak_backup, 0xFF, 1024 * 128);
            backup_update = write_backup_delay;
            flash_mode = FLASH_BASE_MODE;
          }
          break;

        default:
          break;
      }
      flash_command_position = 0;
    }

    if(backup_type == BACKUP_SRAM)
      gamepak_backup[0x5555] = value;
  }
  else

  if((address == 0x2AAA) && (value == 0x55) &&
   (flash_command_position == 1))
  {
    flash_command_position = 2;
  }
  else
  {
    if((flash_command_position == 2) &&
     (flash_mode == FLASH_ERASE_MODE) && (value == 0x30))
    {
      // Erase sector
      memset(flash_bank_ptr + (address & 0xF000), 0xFF, 1024 * 4);
      backup_update = write_backup_delay;
      flash_mode = FLASH_BASE_MODE;
      flash_command_position = 0;
    }
    else

    if((flash_command_position == 0) && (flash_mode == FLASH_BANKSWITCH_MODE) &&
     (address == 0x0000) && (flash_size == FLASH_SIZE_128KB))
    {
      flash_bank_ptr = gamepak_backup + ((value & 0x01) * (1024 * 64));
      flash_mode = FLASH_BASE_MODE;
    }
    else

    if((flash_command_position == 0) && (flash_mode == FLASH_WRITE_MODE))
    {
      // Write value to flash ROM
      backup_update = write_backup_delay;
      flash_bank_ptr[address] = value;
      flash_mode = FLASH_BASE_MODE;
    }
    else

    if(backup_type == BACKUP_SRAM)
    {
      // Write value to SRAM
      backup_update = write_backup_delay;
      // Hit 64KB territory?
      if(address >= 0x8000)
        sram_size = SRAM_SIZE_64KB;
      gamepak_backup[address] = value;
    }
  }
}

#define WRITE_BACKUP8()                                                       \
  write_backup(address & 0xFFFF, value)                                       \

#define WRITE_BACKUP16()                                                      \

#define WRITE_BACKUP32()                                                      \


#define WRITE_VRAM8()                                                         \
  if((address & 0x1FFFF) >=                                                   \
   obj_address[((io_registers[REG_DISPCNT] + 1) >> 2) & 0x01])                \
  {                                                                           \
    /* byte writes to OBJ are ignored */                                      \
    break;                                                                    \
  }                                                                           \
  if(address & 0x10000)                                                       \
    ADDRESS16(vram, address & 0x17FFe) = (value << 8) | value;                \
  else                                                                        \
    ADDRESS16(vram, address & 0x1FFFe) = (value << 8) | value;                \

#define WRITE_VRAM16()                                                        \
  if(address & 0x10000)                                                       \
    ADDRESS16(vram, address & 0x17FFF) = value;                               \
  else                                                                        \
    ADDRESS16(vram, address & 0x1FFFF) = value;                               \

#define WRITE_VRAM32()                                                        \
  if(address & 0x10000)                                                       \
    ADDRESS32(vram, address & 0x17FFF) = value;                               \
  else                                                                        \
    ADDRESS32(vram, address & 0x1FFFF) = value;                               \


#define WRITE_OAM_RAM8()                                                      \
  /* byte writes to OAM are ignored */                                        \

#define WRITE_OAM_RAM16()                                                     \
  oam_update = 1;                                                             \
  ADDRESS16(oam_ram, address & 0x3FF) = value                                 \

#define WRITE_OAM_RAM32()                                                     \
  oam_update = 1;                                                             \
  ADDRESS32(oam_ram, address & 0x3FF) = value                                 \


// RTC code derived from VBA's (due to lack of any real publically available
// documentation...)

static u32 encode_bcd(u8 value)
{
  return ((value / 10) << 4) | (value % 10);
}

#define WRITE_RTC_REGISTER(index, _value)                                     \
  update_address = 0x80000C4 + (index * 2);                                   \
  rtc_registers[index] = _value;                                              \
  rtc_page_index = update_address >> 15;                                      \
  map = memory_map_read[rtc_page_index];                                      \
                                                                              \
  if(map == NULL)                                                             \
    map = load_gamepak_page(rtc_page_index & 0x3FF);                          \
                                                                              \
  ADDRESS16(map, update_address & 0x7FFF) = _value                            \

void write_rtc(u32 address, u32 value)
{
  u32 rtc_page_index;
  u32 update_address;
  u8 *map;

  value &= 0xFFFF;

  switch(address)
  {
    // RTC command
    // Bit 0: SCHK, perform action
    // Bit 1: IO, input/output command data
    // Bit 2: CS, select input/output? If high make I/O write only
    case 0xC4:
      if(rtc_state == RTC_DISABLED)
        rtc_state = RTC_IDLE;

      if(!(rtc_registers[0] & 0x04))
        value = (rtc_registers[0] & 0x02) | (value & ~0x02);

      if(rtc_registers[2] & 0x01)
      {
        // To begin writing a command 1, 5 must be written to the command
        // registers.
        if((rtc_state == RTC_IDLE) && (rtc_registers[0] == 0x01) &&
         (value == 0x05))
        {
          // We're now ready to begin receiving a command.
          WRITE_RTC_REGISTER(0, value);
          rtc_state = RTC_COMMAND;
          rtc_command = 0;
          rtc_bit_count = 7;
        }
        else
        {
          WRITE_RTC_REGISTER(0, value);
          switch(rtc_state)
          {
            // Accumulate RTC command by receiving the next bit, and if we
            // have accumulated enough bits to form a complete command
            // execute it.
            case RTC_COMMAND:
              if(rtc_registers[0] & 0x01)
              {
                rtc_command |= ((value & 0x02) >> 1) << rtc_bit_count;
                rtc_bit_count--;
              }

              // Have we received a full RTC command? If so execute it.
              if(rtc_bit_count < 0)
              {
                switch(rtc_command)
                {
                  // Resets RTC
                  case RTC_COMMAND_RESET:
                    rtc_state = RTC_IDLE;
                    memset(rtc_registers, 0, sizeof(rtc_registers));
                    break;

                  // Sets status of RTC
                  case RTC_COMMAND_WRITE_STATUS:
                    rtc_state = RTC_INPUT_DATA;
                    rtc_data_bytes = 1;
                    rtc_write_mode = RTC_WRITE_STATUS;
                    break;

                  // Outputs current status of RTC
                  case RTC_COMMAND_READ_STATUS:
                    rtc_state = RTC_OUTPUT_DATA;
                    rtc_data_bytes = 1;
                    rtc_data[0] = rtc_status;
                    break;

                  // Actually outputs the time, all of it
                  case RTC_COMMAND_OUTPUT_TIME_FULL:
                  {
                    pspTime current_time;
                    sceRtcGetCurrentClockLocalTime(&current_time);

                    int day_of_week = sceRtcGetDayOfWeek(current_time.year,
                     current_time.month , current_time.day);
                    if(day_of_week == 0)
                      day_of_week = 6;
                    else
                      day_of_week--;

                    rtc_state = RTC_OUTPUT_DATA;
                    rtc_data_bytes = 7;
                    rtc_data[0] = encode_bcd(current_time.year % 100);
                    rtc_data[1] = encode_bcd(current_time.month);
                    rtc_data[2] = encode_bcd(current_time.day);
                    rtc_data[3] = encode_bcd(day_of_week);
                    rtc_data[4] = encode_bcd(current_time.hour);
                    rtc_data[5] = encode_bcd(current_time.minutes);
                    rtc_data[6] = encode_bcd(current_time.seconds);
                    break;
                  }

                  // Only outputs the current time of day.
                  case RTC_COMMAND_OUTPUT_TIME:
                  {
                    pspTime current_time;
                    sceRtcGetCurrentClockLocalTime(&current_time);

                    rtc_state = RTC_OUTPUT_DATA;
                    rtc_data_bytes = 3;
                    rtc_data[0] = encode_bcd(current_time.hour);
                    rtc_data[1] = encode_bcd(current_time.minutes);
                    rtc_data[2] = encode_bcd(current_time.seconds);
                    break;
                  }
                }
                rtc_bit_count = 0;
              }
              break;

            // Receive parameters from the game as input to the RTC
            // for a given command. Read one bit at a time.
            case RTC_INPUT_DATA:
              // Bit 1 of parameter A must be high for input
              if(rtc_registers[1] & 0x02)
              {
                // Read next bit for input
                if(!(value & 0x01))
                {
                  rtc_data[rtc_bit_count >> 3] |=
                   ((value & 0x01) << (7 - (rtc_bit_count & 0x07)));
                }
                else
                {
                  rtc_bit_count++;

                  if(rtc_bit_count == (rtc_data_bytes * 8))
                  {
                    rtc_state = RTC_IDLE;
                    switch(rtc_write_mode)
                    {
                      case RTC_WRITE_STATUS:
                        rtc_status = rtc_data[0];
                        break;
                    }
                  }
                }
              }
              break;

            case RTC_OUTPUT_DATA:
              // Bit 1 of parameter A must be low for output
              if(!(rtc_registers[1] & 0x02))
              {
                // Write next bit to output, on bit 1 of parameter B
                if(!(value & 0x01))
                {
                  u8 current_output_byte = rtc_registers[2];

                  current_output_byte =
                   (current_output_byte & ~0x02) |
                   (((rtc_data[rtc_bit_count >> 3] >>
                   (rtc_bit_count & 0x07)) & 0x01) << 1);

                  WRITE_RTC_REGISTER(0, current_output_byte);

                }
                else
                {
                  rtc_bit_count++;

                  if(rtc_bit_count == (rtc_data_bytes * 8))
                  {
                    rtc_state = RTC_IDLE;
                    memset(rtc_registers, 0, sizeof(rtc_registers));
                  }
                }
              }
              break;
          }
        }
      }
      else
      {
        WRITE_RTC_REGISTER(2, value);
      }
      break;

    // Write parameter A
    case 0xC6:
      WRITE_RTC_REGISTER(1, value);
      break;

    // Write parameter B
    case 0xC8:
      WRITE_RTC_REGISTER(2, value);
      break;
  }
}

#define WRITE_RTC8()                                                          \

#define WRITE_RTC16()                                                         \
  write_rtc(address & 0xFF, value)                                            \

#define WRITE_RTC32()                                                         \


#define WRITE_MEMORY(type)                                                    \
  switch(address >> 24)                                                       \
  {                                                                           \
    case 0x02:                                                                \
      /* external work RAM */                                                 \
      address = (address & 0x7FFF) + ((address & 0x38000) << 1);              \
      ADDRESS##type(ewram + 0x8000, address) = value;                         \
                                                                              \
      if(ADDRESS##type(ewram, address))                                       \
        return CPU_ALERT_SMC;                                                 \
      break;                                                                  \
                                                                              \
    case 0x03:                                                                \
      /* internal work RAM */                                                 \
      ADDRESS##type(iwram + 0x8000, address & 0x7FFF) = value;                \
                                                                              \
      if(ADDRESS##type(iwram, address & 0x7FFF))                              \
        return CPU_ALERT_SMC;                                                 \
      break;                                                                  \
                                                                              \
    case 0x04:                                                                \
      /* I/O registers */                                                     \
      if(address < 0x04000400)                                                \
        return write_io_register##type(address & 0x3FF, value);               \
      break;                                                                  \
                                                                              \
    case 0x05:                                                                \
      /* palette RAM */                                                       \
      WRITE_PALETTE##type(address & 0x3FF, value);                            \
      break;                                                                  \
                                                                              \
    case 0x06:                                                                \
      /* VRAM */                                                              \
      WRITE_VRAM##type();                                                     \
      break;                                                                  \
                                                                              \
    case 0x07:                                                                \
      /* OAM RAM */                                                           \
      WRITE_OAM_RAM##type();                                                  \
      break;                                                                  \
                                                                              \
    case 0x08:                                                                \
      /* gamepak ROM or RTC */                                                \
      WRITE_RTC##type();                                                      \
      break;                                                                  \
                                                                              \
    case 0x0D:                                                                \
      write_eeprom(address, value);                                           \
      break;                                                                  \
                                                                              \
    case 0x0E:                                                                \
      WRITE_BACKUP##type();                                                   \
      break;                                                                  \
                                                                              \
    default:                                                                  \
      /* unwritable */                                                        \
      break;                                                                  \
  }                                                                           \


u8 read_memory8(u32 address)
{
  u8 value;
  READ_MEMORY(8);
  return value;
}

u16 read_memory16_signed(u32 address)
{
  u16 value;

  if(address & 0x01)
  {
    return (s8)read_memory8(address);
  }
  else
  {
    READ_MEMORY(16);
  }

  return value;
}

// unaligned reads are actually 32bit

u32 read_memory16(u32 address)
{
  u32 value;
  u8 rotate = address & 0x01;

  address &= ~0x01;
  READ_MEMORY(16);

  if(rotate)
  {
    ROR(value, value, 8);
  }

  return value;
}

u32 read_memory32(u32 address)
{
  u32 value;
  u8 rotate = (address & 0x03) << 3;

  address &= ~0x03;
  READ_MEMORY(32);

  if(rotate)
  {
    ROR(value, value, rotate);
  }

  return value;
}


CPU_ALERT_TYPE write_memory8(u32 address, u8 value)
{
  WRITE_MEMORY(8);
  return CPU_ALERT_NONE;
}

CPU_ALERT_TYPE write_memory16(u32 address, u16 value)
{
  address &= ~0x01;
  WRITE_MEMORY(16);
  return CPU_ALERT_NONE;
}

CPU_ALERT_TYPE write_memory32(u32 address, u32 value)
{
  address &= ~0x03;
  WRITE_MEMORY(32);
  return CPU_ALERT_NONE;
}


u8 load_backup(char *name)
{
  FILE_TAG_TYPE backup_file;
  char backup_path[MAX_PATH];

  sprintf(backup_path, "%s/%s", dir_save, name);

  FILE_OPEN(backup_file, backup_path, READ);

  if(FILE_CHECK_VALID(backup_file))
  {
    u32 backup_size = file_length(backup_path);

    FILE_READ(backup_file, gamepak_backup, backup_size);
    FILE_CLOSE(backup_file);

    // The size might give away what kind of backup it is.
    switch(backup_size)
    {
      case 0x200:
        backup_type = BACKUP_EEPROM;
        eeprom_size = EEPROM_512_BYTE;
        break;

      case 0x2000:
        backup_type = BACKUP_EEPROM;
        eeprom_size = EEPROM_8_KBYTE;
        break;

      case 0x8000:
        backup_type = BACKUP_SRAM;
        sram_size = SRAM_SIZE_32KB;
        break;

      // Could be either flash or SRAM, go with flash
      case 0x10000:
        backup_type = BACKUP_FLASH;
        sram_size = FLASH_SIZE_64KB;
        break;

      case 0x20000:
        backup_type = BACKUP_FLASH;
        flash_size = FLASH_SIZE_128KB;
        break;
    }
    return 1;
  }
  else
  {
    backup_type = BACKUP_NONE;
    memset(gamepak_backup, 0xFF, 1024 * 128);
  }

  return 0;
}

static u8 save_backup(char *name)
{
  FILE_TAG_TYPE backup_file;
  char backup_path[MAX_PATH];

  if(backup_type != BACKUP_NONE)
  {
    sprintf(backup_path, "%s/%s", dir_save, name);

    FILE_OPEN(backup_file, backup_path, WRITE);

    if(FILE_CHECK_VALID(backup_file))
    {
      u32 backup_size = 0x8000;

      switch(backup_type)
      {
        case BACKUP_SRAM:
          if(sram_size == SRAM_SIZE_32KB)
            backup_size = 0x8000;
          else
            backup_size = 0x10000;
          break;

        case BACKUP_FLASH:
          if(flash_size == FLASH_SIZE_64KB)
            backup_size = 0x10000;
          else
            backup_size = 0x20000;
          break;

        case BACKUP_EEPROM:
          if(eeprom_size == EEPROM_512_BYTE)
            backup_size = 0x200;
          else
            backup_size = 0x2000;
          break;
      }

      FILE_WRITE(backup_file, gamepak_backup, backup_size);
      FILE_CLOSE(backup_file);

      return 1;
    }
  }

  return 0;
}

void update_backup(void)
{
  if(backup_update != (write_backup_delay + 1))
  {
    backup_update--;
  }

  if(backup_update == 0)
  {
    save_backup(backup_filename);
    backup_update = write_backup_delay + 1;
  }
}

void update_backup_force(void)
{
  save_backup(backup_filename);
}


static char *skip_spaces(char *line_ptr)
{
  while(*line_ptr == ' ')
    line_ptr++;

  return line_ptr;
}

s8 parse_config_line(char *current_line, char *current_variable,
 char *current_value)
{
  char *line_ptr = current_line;
  char *line_ptr_new;

  if((current_line[0] == 0) || (current_line[0] == '#'))
    return -1;

  line_ptr_new = strchr(line_ptr, ' ');
  if(line_ptr_new == NULL)
    return -1;

  *line_ptr_new = 0;
  strcpy(current_variable, line_ptr);
  line_ptr_new = skip_spaces(line_ptr_new + 1);

  if(*line_ptr_new != '=')
    return -1;

  line_ptr_new = skip_spaces(line_ptr_new + 1);
  strcpy(current_value, line_ptr_new);
  line_ptr_new = current_value + strlen(current_value) - 1;
  if(*line_ptr_new == '\n')
  {
    line_ptr_new--;
    *line_ptr_new = 0;
  }

  if(*line_ptr_new == '\r')
    *line_ptr_new = 0;

  return 0;
}

static s8 load_game_config(char *gamepak_title, char *gamepak_code,
 char *gamepak_maker)
{
  char current_line[256];
  char current_variable[256];
  char current_value[256];
  char config_path[MAX_PATH];
  FILE *config_file;

  idle_loop_targets = 0;
  idle_loop_target_pc[0] = 0xFFFFFFFF;

  iwram_stack_optimize = 1;

  bios_rom[0x39] = 0x00;
  bios_rom[0x2C] = 0x00;

  translation_gate_targets = 0;

  flash_device_id = FLASH_DEVICE_MACRONIX_64KB;
  backup_type = BACKUP_NONE;

  sprintf(config_path, "%s/%s", main_path, CONFIG_FILENAME);

  config_file = fopen(config_path, "rb");

  if(config_file)
  {
    while(fgets(current_line, 256, config_file))
    {
      if(parse_config_line(current_line, current_variable, current_value) != -1)
      {
        if(strcasecmp(current_variable, "game_name") ||
         strcasecmp(current_value, gamepak_title))
        {
          continue;
        }
        if(!fgets(current_line, 256, config_file) ||
         (parse_config_line(current_line, current_variable, current_value) == -1) ||
         strcasecmp(current_variable, "game_code") ||
         strcasecmp(current_value, gamepak_code))
        {
          continue;
        }
        if(!fgets(current_line, 256, config_file) ||
         (parse_config_line(current_line, current_variable, current_value) == -1) ||
         strcasecmp(current_variable, "vender_code") ||
          strcasecmp(current_value, gamepak_maker))
        {
          continue;
        }

        while(fgets(current_line, 256, config_file))
        {
          if(parse_config_line(current_line, current_variable, current_value) != -1)
          {
            if(!strcasecmp(current_variable, "game_name"))
            {
              fclose(config_file);
              return 0;
            }

            if(!strcasecmp(current_variable, "idle_loop_eliminate_target"))
            {
              if(idle_loop_targets < MAX_IDLE_LOOPS)
              {
                idle_loop_target_pc[idle_loop_targets] =
                  strtol(current_value, NULL, 16);
                idle_loop_targets++;
              }
            }

            if(!strcasecmp(current_variable, "translation_gate_target"))
            {
              if(translation_gate_targets < MAX_TRANSLATION_GATES)
              {
                translation_gate_target_pc[translation_gate_targets] =
                 strtol(current_value, NULL, 16);
                translation_gate_targets++;
              }
            }

            if(!strcasecmp(current_variable, "iwram_stack_optimize") &&
              !strcasecmp(current_value, "no"))
            {
              iwram_stack_optimize = 0;
            }

            if(!strcasecmp(current_variable, "flash_rom_type") &&
              !strcasecmp(current_value, "128KB"))
            {
              flash_device_id = FLASH_DEVICE_MACRONIX_128KB;
            }

            if(!strcasecmp(current_variable, "save_type"))
            {
              if(!strcasecmp(current_value, "sram"))
              {
                backup_type = BACKUP_SRAM;
              }
              else
              if(!strcasecmp(current_value, "flash"))
              {
                backup_type = BACKUP_FLASH;
              }
              else
              if(!strcasecmp(current_value, "eeprom"))
              {
                backup_type = BACKUP_EEPROM;
              }
            }

            if(!strcasecmp(current_variable, "bios_rom_hack_39") &&
              !strcasecmp(current_value, "yes"))
            {
              bios_rom[0x39] = 0xC0;
            }

            if(!strcasecmp(current_variable, "bios_rom_hack_2C") &&
              !strcasecmp(current_value, "yes"))
            {
               bios_rom[0x2C] = 0x02;
            }
          }
        }

        fclose(config_file);
        return 0;
      }
    }

    fclose(config_file);
  }

  return -1;
}


static s32 load_gamepak_raw(char *name)
{
  FILE_TAG_TYPE gamepak_file;

  FILE_OPEN(gamepak_file, name, READ);

  if(FILE_CHECK_VALID(gamepak_file))
  {
    u32 _gamepak_size = file_length(name);

    // If it's a big file size keep it don't close it, we'll
    // probably want to load it later
    if(_gamepak_size <= gamepak_ram_buffer_size)
    {
      FILE_READ(gamepak_file, gamepak_rom, _gamepak_size);
      FILE_CLOSE(gamepak_file);
    }
    else
    {
      // Read in just enough for the header
      FILE_READ(gamepak_file, gamepak_rom, 0x100);
      gamepak_file_large = gamepak_file;

      char *p = strrchr(name, '/');
      if(p != NULL)
      {
        strcpy(gamepak_filename_raw, name);
      }
      else
      {
        char current_dir[MAX_PATH];
        getcwd(current_dir, MAX_PATH);
        sprintf(gamepak_filename_raw, "%s/%s", current_dir, name);
      }
    }

    return _gamepak_size;
  }

  return -1;
}

s8 load_gamepak(char *name)
{
  char gamepak_title[13];
  char gamepak_code[5];
  char gamepak_maker[3];

  char *dot_position = strrchr(name, '.');
  char cheats_filename[MAX_FILE];

  s32 file_size = -1;
  gamepak_file_large = -1;

  clear_screen(0x0000);
  print_string("Loading ROM...", 0xFFFF, 0x0000, 5, 5);
  flip_screen(1);

  if(!strcasecmp(dot_position, ".zip") || !strcasecmp(dot_position, ".gbz"))
  {
    set_cpu_clock(333);
    file_size = load_file_zip(name);
  }
  else
  if(!strcasecmp(dot_position, ".gba") || !strcasecmp(dot_position, ".agb") ||
     !strcasecmp(dot_position, ".bin"))
  {
    file_size = load_gamepak_raw(name);
  }

  if(file_size != -1)
  {
    gamepak_size = (file_size + 0x7FFF) & ~0x7FFF;

    char *p = strrchr(name, '/');
    if(p != NULL)
      name = p + 1;

    strncpy(gamepak_filename, name, MAX_FILE);

    change_ext(gamepak_filename, backup_filename, ".sav");
    load_backup(backup_filename);

    memcpy(gamepak_title, gamepak_rom + 0xA0, 12);
    memcpy(gamepak_code,  gamepak_rom + 0xAC, 4);
    memcpy(gamepak_maker, gamepak_rom + 0xB0, 2);
    gamepak_title[12] = 0;
    gamepak_code[4] = 0;
    gamepak_maker[2] = 0;

    load_game_config(gamepak_title, gamepak_code, gamepak_maker);
    load_game_config_file();

    tower_hack = 0;
    if(!strcasecmp(gamepak_title, "THE TOWER") ||
       !strcasecmp(gamepak_title, "THE TOWER SP"))
    {
      tower_hack = 1;
    }

    change_ext(gamepak_filename, cheats_filename, ".cht");
    add_cheats(cheats_filename);

    return 0;
  }

  return -1;
}

s8 load_bios(char *name)
{
  FILE_TAG_TYPE bios_file;

  FILE_OPEN(bios_file, name, READ);

  if(FILE_CHECK_VALID(bios_file))
  {
    FILE_READ(bios_file, bios_rom, 0x4000);
    FILE_CLOSE(bios_file);

    u32 bios_hash = crc32(0, bios_rom, 0x4000);

    // GBA
    if(bios_hash == 0x81977335)
      return 0;

    // NDS
    if(bios_hash == 0xA6473709)
      return 0;

    return -2;
  }

  return -1;
}


// DMA memory regions can be one of the following:
// IWRAM - 32kb offset from the contiguous iwram region.
// EWRAM - like segmented but with self modifying code check.
// VRAM - 96kb offset from the contiguous vram region, should take care
// Palette RAM - Converts palette entries when written to.
// OAM RAM - Sets OAM modified flag to true.
// I/O registers - Uses the I/O register function.
// of mirroring properly.
// Segmented RAM/ROM - a region >= 32kb, the translated address has to
//  be reloaded if it wraps around the limit (cartride ROM)
// Ext - should be handled by the memory read/write function.

// The following map determines the region of each (assumes DMA access
// is not done out of bounds)

typedef enum
{
  DMA_REGION_IWRAM,
  DMA_REGION_EWRAM,
  DMA_REGION_VRAM,
  DMA_REGION_PALETTE_RAM,
  DMA_REGION_OAM_RAM,
  DMA_REGION_IO,
  DMA_REGION_GAMEPAK,
  DMA_REGION_EXT,
  DMA_REGION_BIOS,
  DMA_REGION_NULL
} DMA_REGION_TYPE;

DMA_REGION_TYPE ALIGN_DATA dma_region_map[16] =
{
  DMA_REGION_BIOS,          // 0x00 - BIOS
  DMA_REGION_NULL,          // 0x01 - Nothing
  DMA_REGION_EWRAM,         // 0x02 - EWRAM
  DMA_REGION_IWRAM,         // 0x03 - IWRAM
  DMA_REGION_IO,            // 0x04 - I/O registers
  DMA_REGION_PALETTE_RAM,   // 0x05 - palette RAM
  DMA_REGION_VRAM,          // 0x06 - VRAM
  DMA_REGION_OAM_RAM,       // 0x07 - OAM RAM
  DMA_REGION_GAMEPAK,       // 0x08 - gamepak ROM
  DMA_REGION_GAMEPAK,       // 0x09 - gamepak ROM
  DMA_REGION_GAMEPAK,       // 0x0A - gamepak ROM
  DMA_REGION_GAMEPAK,       // 0x0B - gamepak ROM
  DMA_REGION_GAMEPAK,       // 0x0C - gamepak ROM
  DMA_REGION_EXT,           // 0x0D - EEPROM
  DMA_REGION_EXT,           // 0x0E - gamepak SRAM/flash ROM
  DMA_REGION_EXT            // 0x0F - gamepak SRAM/flash ROM
};

#define dma_adjust_ptr_inc(ptr, size)                                         \
  ptr += (size / 8)                                                           \

#define dma_adjust_ptr_dec(ptr, size)                                         \
  ptr -= (size / 8)                                                           \

#define dma_adjust_ptr_fix(ptr, size)                                         \

#define dma_adjust_ptr_writeback()                                            \
  dma->dest_address = dest_ptr                                                \

#define dma_adjust_ptr_reload()                                               \

#define dma_print(src_op, dest_op, transfer_size, wb)                         \
  printf("dma from %x (%s) to %x (%s) for %x (%s) (%s) (%d) (pc %x)\n",       \
   src_ptr, #src_op, dest_ptr, #dest_op, length, #transfer_size, #wb,         \
   dma->irq, reg[15]);                                                        \

#define dma_smc_vars_src()                                                    \

#define dma_smc_vars_dest()                                                   \
  u32 smc_trigger = 0                                                         \

#define dma_vars_iwram(type)                                                  \
  dma_smc_vars_##type()                                                       \

#define dma_vars_vram(type)                                                   \

#define dma_vars_palette_ram(type)                                            \

#define dma_oam_ram_src()                                                     \

#define dma_oam_ram_dest()                                                    \
  oam_update = 1                                                              \

#define dma_vars_oam_ram(type)                                                \
  dma_oam_ram_##type()                                                        \

#define dma_vars_io(type)                                                     \

#define dma_segmented_load_src()                                              \
  memory_map_read[src_current_region]                                         \

#define dma_segmented_load_dest()                                             \
  memory_map_write[dest_current_region]                                       \

#define dma_vars_gamepak(type)                                                \
  u32 type##_new_region;                                                      \
  u32 type##_current_region = type##_ptr >> 15;                               \
  u8 *type##_address_block = dma_segmented_load_##type();                     \
  if(type##_address_block == NULL)                                            \
  {                                                                           \
    if((type##_ptr & 0x1FFFFFF) >= gamepak_size)                              \
      break;                                                                  \
    type##_address_block = load_gamepak_page(type##_current_region & 0x3FF);  \
  }                                                                           \

#define dma_vars_ewram(type)                                                  \
  dma_smc_vars_##type();                                                      \
  u32 type##_new_region;                                                      \
  u32 type##_current_region = type##_ptr >> 15;                               \
  u8 *type##_address_block = dma_segmented_load_##type()                      \

#define dma_vars_bios(type)                                                   \

#define dma_vars_ext(type)                                                    \

#define dma_ewram_check_region(type)                                          \
  type##_new_region = (type##_ptr >> 15);                                     \
  if(type##_new_region != type##_current_region)                              \
  {                                                                           \
    type##_current_region = type##_new_region;                                \
    type##_address_block = dma_segmented_load_##type();                       \
  }                                                                           \

#define dma_gamepak_check_region(type)                                        \
  type##_new_region = (type##_ptr >> 15);                                     \
  if(type##_new_region != type##_current_region)                              \
  {                                                                           \
    type##_current_region = type##_new_region;                                \
    type##_address_block = dma_segmented_load_##type();                       \
    if(type##_address_block == NULL)                                          \
    {                                                                         \
      type##_address_block =                                                  \
       load_gamepak_page(type##_current_region & 0x3FF);                      \
    }                                                                         \
  }                                                                           \

#define dma_read_iwram(type, transfer_size)                                   \
  read_value = ADDRESS##transfer_size(iwram + 0x8000, type##_ptr & 0x7FFF)    \

#define dma_read_vram(type, transfer_size)                                    \
  if(type##_ptr & 0x10000)                                                    \
    read_value = ADDRESS##transfer_size(vram, type##_ptr & 0x17FFF);          \
  else                                                                        \
    read_value = ADDRESS##transfer_size(vram, type##_ptr & 0x1FFFF);          \

#define dma_read_io(type, transfer_size)                                      \
  read_value = ADDRESS##transfer_size(io_registers, type##_ptr & 0x3FF)       \

#define dma_read_oam_ram(type, transfer_size)                                 \
  read_value = ADDRESS##transfer_size(oam_ram, type##_ptr & 0x3FF)            \

#define dma_read_palette_ram(type, transfer_size)                             \
  read_value = ADDRESS##transfer_size(palette_ram, type##_ptr & 0x3FF)        \

#define dma_read_ewram(type, transfer_size)                                   \
  dma_ewram_check_region(type);                                               \
  read_value =                                                                \
   ADDRESS##transfer_size(type##_address_block, type##_ptr & 0x7FFF)          \

#define dma_read_gamepak(type, transfer_size)                                 \
  dma_gamepak_check_region(type);                                             \
  read_value =                                                                \
   ADDRESS##transfer_size(type##_address_block, type##_ptr & 0x7FFF)          \

// DMAing from the BIOS is funny, just returns 0..

#define dma_read_bios(type, transfer_size)                                    \
  read_value = 0                                                              \

#define dma_read_ext(type, transfer_size)                                     \
  read_value = read_memory##transfer_size(type##_ptr)                         \

#define dma_write_iwram(type, transfer_size)                                  \
  ADDRESS##transfer_size(iwram + 0x8000, type##_ptr & 0x7FFF) = read_value;   \
  smc_trigger |= ADDRESS##transfer_size(iwram, type##_ptr & 0x7FFF)           \

#define dma_write_vram(type, transfer_size)                                   \
  if(type##_ptr & 0x10000)                                                    \
    ADDRESS##transfer_size(vram, type##_ptr & 0x17FFF) = read_value;          \
  else                                                                        \
    ADDRESS##transfer_size(vram, type##_ptr & 0x1FFFF) = read_value;          \

#define dma_write_io(type, transfer_size)                                     \
  return_value |=                                                             \
   write_io_register##transfer_size(type##_ptr & 0x3FF, read_value)           \

#define dma_write_oam_ram(type, transfer_size)                                \
  ADDRESS##transfer_size(oam_ram, type##_ptr & 0x3FF) = read_value            \

#define dma_write_palette_ram(type, transfer_size)                            \
  WRITE_PALETTE##transfer_size(type##_ptr & 0x3FF, read_value)                \

#define dma_write_ext(type, transfer_size)                                    \
  return_value |=                                                             \
   write_memory##transfer_size(type##_ptr, read_value)                        \

#define dma_write_ewram(type, transfer_size)                                  \
  dma_ewram_check_region(type);                                               \
                                                                              \
  ADDRESS##transfer_size(type##_address_block, type##_ptr & 0x7FFF)           \
   = read_value;                                                              \
  smc_trigger |= ADDRESS##transfer_size(type##_address_block,                 \
   (type##_ptr & 0x7FFF) - 0x8000)                                            \

#define dma_epilogue_iwram()                                                  \
  if(smc_trigger)                                                             \
  {                                                                           \
    /* Special return code indicating to retranslate to the CPU code */       \
    return_value = CPU_ALERT_SMC;                                             \
  }                                                                           \

#define dma_epilogue_ewram()                                                  \
  if(smc_trigger)                                                             \
  {                                                                           \
    /* Special return code indicating to retranslate to the CPU code */       \
    return_value = CPU_ALERT_SMC;                                             \
  }                                                                           \

#define dma_epilogue_vram()                                                   \

#define dma_epilogue_io()                                                     \

#define dma_epilogue_oam_ram()                                                \

#define dma_epilogue_palette_ram()                                            \

#define dma_epilogue_gamepak()                                                \

#define dma_epilogue_ext()                                                    \

#define print_line()                                                          \
  dma_print(src_op, dest_op, transfer_size, wb)                               \

#define dma_transfer_loop_region(src_region_type, dest_region_type, src_op,   \
 dest_op, transfer_size, wb)                                                  \
{                                                                             \
  dma_vars_##src_region_type(src);                                            \
  dma_vars_##dest_region_type(dest);                                          \
                                                                              \
  for(i = 0; i < length; i++)                                                 \
  {                                                                           \
    dma_read_##src_region_type(src, transfer_size);                           \
    dma_write_##dest_region_type(dest, transfer_size);                        \
    dma_adjust_ptr_##src_op(src_ptr, transfer_size);                          \
    dma_adjust_ptr_##dest_op(dest_ptr, transfer_size);                        \
  }                                                                           \
  dma->source_address = src_ptr;                                              \
  dma_adjust_ptr_##wb();                                                      \
  dma_epilogue_##dest_region_type();                                          \
  break;                                                                      \
}                                                                             \

#define dma_transfer_loop(src_op, dest_op, transfer_size, wb)                 \
{                                                                             \
  DMA_REGION_TYPE src_region_type = dma_region_map[src_region];               \
  DMA_REGION_TYPE dest_region_type = dma_region_map[dest_region];             \
                                                                              \
  switch(src_region_type | (dest_region_type << 4))                           \
  {                                                                           \
    case (DMA_REGION_BIOS | (DMA_REGION_IWRAM << 4)):                         \
      dma_transfer_loop_region(bios, iwram, src_op, dest_op,                  \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IWRAM | (DMA_REGION_IWRAM << 4)):                        \
      dma_transfer_loop_region(iwram, iwram, src_op, dest_op,                 \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EWRAM | (DMA_REGION_IWRAM << 4)):                        \
      dma_transfer_loop_region(ewram, iwram, src_op, dest_op,                 \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_VRAM | (DMA_REGION_IWRAM << 4)):                         \
      dma_transfer_loop_region(vram, iwram, src_op, dest_op,                  \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_PALETTE_RAM | (DMA_REGION_IWRAM << 4)):                  \
      dma_transfer_loop_region(palette_ram, iwram, src_op, dest_op,           \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_OAM_RAM | (DMA_REGION_IWRAM << 4)):                      \
      dma_transfer_loop_region(oam_ram, iwram, src_op, dest_op,               \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IO | (DMA_REGION_IWRAM << 4)):                           \
      dma_transfer_loop_region(io, iwram, src_op, dest_op,                    \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_GAMEPAK | (DMA_REGION_IWRAM << 4)):                      \
      dma_transfer_loop_region(gamepak, iwram, src_op, dest_op,               \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EXT | (DMA_REGION_IWRAM << 4)):                          \
      dma_transfer_loop_region(ext, iwram, src_op, dest_op,                   \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_BIOS | (DMA_REGION_EWRAM << 4)):                         \
      dma_transfer_loop_region(bios, ewram, src_op, dest_op,                  \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IWRAM | (DMA_REGION_EWRAM << 4)):                        \
      dma_transfer_loop_region(iwram, ewram, src_op, dest_op,                 \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EWRAM | (DMA_REGION_EWRAM << 4)):                        \
      dma_transfer_loop_region(ewram, ewram, src_op, dest_op,                 \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_VRAM | (DMA_REGION_EWRAM << 4)):                         \
      dma_transfer_loop_region(vram, ewram, src_op, dest_op,                  \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_PALETTE_RAM | (DMA_REGION_EWRAM << 4)):                  \
      dma_transfer_loop_region(palette_ram, ewram, src_op, dest_op,           \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_OAM_RAM | (DMA_REGION_EWRAM << 4)):                      \
      dma_transfer_loop_region(oam_ram, ewram, src_op, dest_op,               \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IO | (DMA_REGION_EWRAM << 4)):                           \
      dma_transfer_loop_region(io, ewram, src_op, dest_op,                    \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_GAMEPAK | (DMA_REGION_EWRAM << 4)):                      \
      dma_transfer_loop_region(gamepak, ewram, src_op, dest_op,               \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EXT | (DMA_REGION_EWRAM << 4)):                          \
      dma_transfer_loop_region(ext, ewram, src_op, dest_op,                   \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_BIOS | (DMA_REGION_VRAM << 4)):                          \
      dma_transfer_loop_region(bios, vram, src_op, dest_op,                   \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IWRAM | (DMA_REGION_VRAM << 4)):                         \
      dma_transfer_loop_region(iwram, vram, src_op, dest_op,                  \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EWRAM | (DMA_REGION_VRAM << 4)):                         \
      dma_transfer_loop_region(ewram, vram, src_op, dest_op,                  \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_VRAM | (DMA_REGION_VRAM << 4)):                          \
      dma_transfer_loop_region(vram, vram, src_op, dest_op,                   \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_PALETTE_RAM | (DMA_REGION_VRAM << 4)):                   \
      dma_transfer_loop_region(palette_ram, vram, src_op, dest_op,            \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_OAM_RAM | (DMA_REGION_VRAM << 4)):                       \
      dma_transfer_loop_region(oam_ram, vram, src_op, dest_op,                \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IO | (DMA_REGION_VRAM << 4)):                            \
      dma_transfer_loop_region(io, vram, src_op, dest_op,                     \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_GAMEPAK | (DMA_REGION_VRAM << 4)):                       \
      dma_transfer_loop_region(gamepak, vram, src_op, dest_op,                \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EXT | (DMA_REGION_VRAM << 4)):                           \
      dma_transfer_loop_region(ext, vram, src_op, dest_op,                    \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_BIOS | (DMA_REGION_PALETTE_RAM << 4)):                   \
      dma_transfer_loop_region(bios, palette_ram, src_op, dest_op,            \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IWRAM | (DMA_REGION_PALETTE_RAM << 4)):                  \
      dma_transfer_loop_region(iwram, palette_ram, src_op, dest_op,           \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EWRAM | (DMA_REGION_PALETTE_RAM << 4)):                  \
      dma_transfer_loop_region(ewram, palette_ram, src_op, dest_op,           \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_VRAM | (DMA_REGION_PALETTE_RAM << 4)):                   \
      dma_transfer_loop_region(vram, palette_ram, src_op, dest_op,            \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_PALETTE_RAM | (DMA_REGION_PALETTE_RAM << 4)):            \
      dma_transfer_loop_region(palette_ram, palette_ram, src_op, dest_op,     \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_OAM_RAM | (DMA_REGION_PALETTE_RAM << 4)):                \
      dma_transfer_loop_region(oam_ram, palette_ram, src_op, dest_op,         \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IO | (DMA_REGION_PALETTE_RAM << 4)):                     \
      dma_transfer_loop_region(io, palette_ram, src_op, dest_op,              \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_GAMEPAK | (DMA_REGION_PALETTE_RAM << 4)):                \
      dma_transfer_loop_region(gamepak, palette_ram, src_op, dest_op,         \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EXT | (DMA_REGION_PALETTE_RAM << 4)):                    \
      dma_transfer_loop_region(ext, palette_ram, src_op, dest_op,             \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_BIOS | (DMA_REGION_OAM_RAM << 4)):                       \
      dma_transfer_loop_region(bios, oam_ram, src_op, dest_op,                \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IWRAM | (DMA_REGION_OAM_RAM << 4)):                      \
      dma_transfer_loop_region(iwram, oam_ram, src_op, dest_op,               \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EWRAM | (DMA_REGION_OAM_RAM << 4)):                      \
      dma_transfer_loop_region(ewram, oam_ram, src_op, dest_op,               \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_VRAM | (DMA_REGION_OAM_RAM << 4)):                       \
      dma_transfer_loop_region(vram, oam_ram, src_op, dest_op,                \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_PALETTE_RAM | (DMA_REGION_OAM_RAM << 4)):                \
      dma_transfer_loop_region(palette_ram, oam_ram, src_op, dest_op,         \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_OAM_RAM | (DMA_REGION_OAM_RAM << 4)):                    \
      dma_transfer_loop_region(oam_ram, oam_ram, src_op, dest_op,             \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IO | (DMA_REGION_OAM_RAM << 4)):                         \
      dma_transfer_loop_region(io, oam_ram, src_op, dest_op,                  \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_GAMEPAK | (DMA_REGION_OAM_RAM << 4)):                    \
      dma_transfer_loop_region(gamepak, oam_ram, src_op, dest_op,             \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EXT | (DMA_REGION_OAM_RAM << 4)):                        \
      dma_transfer_loop_region(ext, oam_ram, src_op, dest_op,                 \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_BIOS | (DMA_REGION_IO << 4)):                            \
      dma_transfer_loop_region(bios, io, src_op, dest_op,                     \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IWRAM | (DMA_REGION_IO << 4)):                           \
      dma_transfer_loop_region(iwram, io, src_op, dest_op,                    \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EWRAM | (DMA_REGION_IO << 4)):                           \
      dma_transfer_loop_region(ewram, io, src_op, dest_op,                    \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_VRAM | (DMA_REGION_IO << 4)):                            \
      dma_transfer_loop_region(vram, io, src_op, dest_op,                     \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_PALETTE_RAM | (DMA_REGION_IO << 4)):                     \
      dma_transfer_loop_region(palette_ram, io, src_op, dest_op,              \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_OAM_RAM | (DMA_REGION_IO << 4)):                         \
      dma_transfer_loop_region(oam_ram, io, src_op, dest_op,                  \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IO | (DMA_REGION_IO << 4)):                              \
      dma_transfer_loop_region(io, io, src_op, dest_op,                       \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_GAMEPAK | (DMA_REGION_IO << 4)):                         \
      dma_transfer_loop_region(gamepak, io, src_op, dest_op,                  \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EXT | (DMA_REGION_IO << 4)):                             \
      dma_transfer_loop_region(ext, io, src_op, dest_op,                      \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_BIOS | (DMA_REGION_EXT << 4)):                           \
      dma_transfer_loop_region(bios, ext, src_op, dest_op,                    \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IWRAM | (DMA_REGION_EXT << 4)):                          \
      dma_transfer_loop_region(iwram, ext, src_op, dest_op,                   \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EWRAM | (DMA_REGION_EXT << 4)):                          \
      dma_transfer_loop_region(ewram, ext, src_op, dest_op,                   \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_VRAM | (DMA_REGION_EXT << 4)):                           \
      dma_transfer_loop_region(vram, ext, src_op, dest_op,                    \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_PALETTE_RAM | (DMA_REGION_EXT << 4)):                    \
      dma_transfer_loop_region(palette_ram, ext, src_op, dest_op,             \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_OAM_RAM | (DMA_REGION_EXT << 4)):                        \
      dma_transfer_loop_region(oam_ram, ext, src_op, dest_op,                 \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_IO | (DMA_REGION_EXT << 4)):                             \
      dma_transfer_loop_region(io, ext, src_op, dest_op,                      \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_GAMEPAK | (DMA_REGION_EXT << 4)):                        \
      dma_transfer_loop_region(gamepak, ext, src_op, dest_op,                 \
       transfer_size, wb);                                                    \
                                                                              \
    case (DMA_REGION_EXT | (DMA_REGION_EXT << 4)):                            \
      dma_transfer_loop_region(ext, ext, src_op, dest_op,                     \
       transfer_size, wb);                                                    \
  }                                                                           \
  break;                                                                      \
}                                                                             \

#define dma_transfer_expand(transfer_size)                                    \
  switch((dma->dest_direction << 2) | dma->source_direction)                  \
  {                                                                           \
    case 0x00:                                                                \
      dma_transfer_loop(inc, inc, transfer_size, writeback);                  \
                                                                              \
    case 0x01:                                                                \
      dma_transfer_loop(dec, inc, transfer_size, writeback);                  \
                                                                              \
    case 0x02:                                                                \
      dma_transfer_loop(fix, inc, transfer_size, writeback);                  \
                                                                              \
    case 0x03:                                                                \
      break;                                                                  \
                                                                              \
    case 0x04:                                                                \
      dma_transfer_loop(inc, dec, transfer_size, writeback);                  \
                                                                              \
    case 0x05:                                                                \
      dma_transfer_loop(dec, dec, transfer_size, writeback);                  \
                                                                              \
    case 0x06:                                                                \
      dma_transfer_loop(fix, dec, transfer_size, writeback);                  \
                                                                              \
    case 0x07:                                                                \
      break;                                                                  \
                                                                              \
    case 0x08:                                                                \
      dma_transfer_loop(inc, fix, transfer_size, writeback);                  \
                                                                              \
    case 0x09:                                                                \
      dma_transfer_loop(dec, fix, transfer_size, writeback);                  \
                                                                              \
    case 0x0A:                                                                \
      dma_transfer_loop(fix, fix, transfer_size, writeback);                  \
                                                                              \
    case 0x0B:                                                                \
      break;                                                                  \
                                                                              \
    case 0x0C:                                                                \
      dma_transfer_loop(inc, inc, transfer_size, reload);                     \
                                                                              \
    case 0x0D:                                                                \
      dma_transfer_loop(dec, inc, transfer_size, reload);                     \
                                                                              \
    case 0x0E:                                                                \
      dma_transfer_loop(fix, inc, transfer_size, reload);                     \
                                                                              \
    case 0x0F:                                                                \
      break;                                                                  \
  }                                                                           \

#define CYCLE_DMA16()                                                         \
{                                                                             \
  u32 dma_read_cycle =                                                        \
   data_waitstate_cycles_n[0][src_region] +                                   \
   (data_waitstate_cycles_s[0][src_region] * (length - 1));                   \
  u32 dma_write_cycle =                                                       \
   data_waitstate_cycles_n[0][dest_region] +                                  \
   (data_waitstate_cycles_s[0][dest_region] * (length - 1));                  \
                                                                              \
  cycle_count_dma += dma_read_cycle + dma_write_cycle + 2;                    \
}                                                                             \

#define CYCLE_DMA32()                                                         \
{                                                                             \
  u32 dma_read_cycle =                                                        \
   data_waitstate_cycles_n[1][src_region] +                                   \
   (data_waitstate_cycles_s[1][src_region] * (length - 1));                   \
  u32 dma_write_cycle =                                                       \
   data_waitstate_cycles_n[1][dest_region] +                                  \
   (data_waitstate_cycles_s[1][dest_region] * (length - 1));                  \
                                                                              \
  cycle_count_dma += dma_read_cycle + dma_write_cycle + 2;                    \
}                                                                             \

CPU_ALERT_TYPE dma_transfer(DMA_TRANSFER_TYPE *dma)
{
  u32 i;
  u32 length = dma->length;
  u32 read_value;
  u32 src_ptr = dma->source_address;
  u32 dest_ptr = dma->dest_address;
  u32 src_region, dest_region;
  CPU_ALERT_TYPE return_value = CPU_ALERT_NONE;

  // Technically this should be done for source and destination, but
  // chances are this is only ever used (probably mistakingly!) for dest.
  // The only game I know of that requires this is Lucky Luke.

  if((dest_ptr >> 24) != ((dest_ptr + length - 1) >> 24))
  {
    u32 first_length = ((dest_ptr & 0xFF000000) + 0x1000000) - dest_ptr;
    u32 second_length = length - first_length;
    dma->length = first_length;

    dma_transfer(dma);

    dma->length = length;

    length = second_length;
    dest_ptr += first_length;
    src_ptr += first_length;
  }

  src_region  = src_ptr  >> 24;
  dest_region = dest_ptr >> 24;

  if(dma->length_type == DMA_16BIT)
  {
    src_ptr  &= ~0x01;
    dest_ptr &= ~0x01;
//    CYCLE_DMA16();
    dma_transfer_expand(16);
  }
  else
  {
    src_ptr  &= ~0x03;
    dest_ptr &= ~0x03;
//    CYCLE_DMA32();
    dma_transfer_expand(32);
  }

  if((dma->repeat_type == DMA_NO_REPEAT) ||
     (dma->start_type == DMA_START_IMMEDIATELY))
  {
    dma->start_type = DMA_INACTIVE;
    ADDRESS16(io_registers, (dma->dma_channel * 12) + 0xBA) &= (~0x8000);
  }

  if(dma->irq)
  {
    raise_interrupt(IRQ_DMA0 << dma->dma_channel);
    return_value |= CPU_ALERT_IRQ;
  }

  return return_value;
}


// Be sure to do this after loading ROMs.

#define MAP_REGION(type, start, end, mirror_blocks, region)                   \
  for(map_offset = (start) / 0x8000; map_offset <                             \
   ((end) / 0x8000); map_offset++)                                            \
  {                                                                           \
    memory_map_##type[map_offset] =                                           \
     ((u8 *)region) + ((map_offset % mirror_blocks) * 0x8000);                \
  }                                                                           \

#define MAP_NULL(type, start, end)                                            \
  for(map_offset = start / 0x8000; map_offset < (end / 0x8000);               \
   map_offset++)                                                              \
  {                                                                           \
    memory_map_##type[map_offset] = NULL;                                     \
  }                                                                           \

#define MAP_RAM_REGION(type, start, end, mirror_blocks, region)               \
  for(map_offset = (start) / 0x8000; map_offset <                             \
   ((end) / 0x8000); map_offset++)                                            \
  {                                                                           \
    memory_map_##type[map_offset] =                                           \
     ((u8 *)region) + ((map_offset % mirror_blocks) * 0x10000) + 0x8000;      \
  }                                                                           \

#define MAP_VRAM(type)                                                        \
  for(map_offset = 0x6000000 / 0x8000; map_offset < (0x7000000 / 0x8000);     \
   map_offset += 4)                                                           \
  {                                                                           \
    memory_map_##type[map_offset] = vram;                                     \
    memory_map_##type[map_offset + 1] = vram + 0x8000;                        \
    memory_map_##type[map_offset + 2] = vram + (0x8000 * 2);                  \
    memory_map_##type[map_offset + 3] = vram + (0x8000 * 2);                  \
  }                                                                           \

#define MAP_VRAM_FIRSTPAGE(type)                                              \
  for(map_offset = 0x6000000 / 0x8000; map_offset < (0x7000000 / 0x8000);     \
   map_offset += 4)                                                           \
  {                                                                           \
    memory_map_##type[map_offset] = vram;                                     \
    memory_map_##type[map_offset + 1] = NULL;                                 \
    memory_map_##type[map_offset + 2] = NULL;                                 \
    memory_map_##type[map_offset + 3] = NULL;                                 \
  }                                                                           \


static u32 evict_gamepak_page(void)
{
  // Find the one with the smallest frame timestamp
  u32 page_index = 0;
  u32 physical_index;
  u32 smallest = gamepak_memory_map[0].page_timestamp;
  u32 i;

  for(i = 1; i < gamepak_ram_pages; i++)
  {
    if(gamepak_memory_map[i].page_timestamp <= smallest)
    {
      smallest = gamepak_memory_map[i].page_timestamp;
      page_index = i;
    }
  }

  physical_index = gamepak_memory_map[page_index].physical_index;

  memory_map_read[(0x8000000 / (32 * 1024)) + physical_index] = NULL;
  memory_map_read[(0xA000000 / (32 * 1024)) + physical_index] = NULL;
  memory_map_read[(0xC000000 / (32 * 1024)) + physical_index] = NULL;

  return page_index;
}

u8 *load_gamepak_page(u32 physical_index)
{
  if(physical_index >= (gamepak_size >> 15))
  {
    return gamepak_rom;
  }

  u32 page_index = evict_gamepak_page();
  u32 page_offset = page_index * (32 * 1024);
  u8 *swap_location = gamepak_rom + page_offset;

  gamepak_memory_map[page_index].page_timestamp = page_time;
  gamepak_memory_map[page_index].physical_index = physical_index;
  page_time++;

  FILE_SEEK(gamepak_file_large, physical_index * (32 * 1024), SEEK_SET);
  FILE_READ(gamepak_file_large, swap_location, (32 * 1024));

  memory_map_read[(0x8000000 / (32 * 1024)) + physical_index] = swap_location;
  memory_map_read[(0xA000000 / (32 * 1024)) + physical_index] = swap_location;
  memory_map_read[(0xC000000 / (32 * 1024)) + physical_index] = swap_location;

  // If RTC is active page the RTC register bytes so they can be read
  if((rtc_state != RTC_DISABLED) && (physical_index == 0))
  {
    memcpy(swap_location + 0xC4, rtc_registers, sizeof(rtc_registers));
  }

  return swap_location;
}

static void init_memory_gamepak(void)
{
  u32 map_offset = 0;

  if(gamepak_size > gamepak_ram_buffer_size)
  {
    // Large ROMs get special treatment because they
    // can't fit into the 16MB ROM buffer.
    u32 i;
    for(i = 0; i < gamepak_ram_pages; i++)
    {
      gamepak_memory_map[i].page_timestamp = 0;
      gamepak_memory_map[i].physical_index = 0;
    }

    MAP_NULL(read, 0x8000000, 0xD000000);
  }
  else
  {
    MAP_REGION(read, 0x8000000, 0x8000000 + gamepak_size, 1024, gamepak_rom);
    MAP_NULL(read, 0x8000000 + gamepak_size, 0xA000000);
    MAP_REGION(read, 0xA000000, 0xA000000 + gamepak_size, 1024, gamepak_rom);
    MAP_NULL(read, 0xA000000 + gamepak_size, 0xC000000);
    MAP_REGION(read, 0xC000000, 0xC000000 + gamepak_size, 1024, gamepak_rom);
    MAP_NULL(read, 0xC000000 + gamepak_size, 0xE000000);
  }
}

void init_gamepak_buffer(void)
{
  gamepak_rom = NULL;
  gamepak_shelter = NULL;

#ifdef BUILD_CFW_M33
  {
    // Try to initialize 32MB
    gamepak_ram_buffer_size = 32 * 1024 * 1024;
    gamepak_rom = memalign(MEM_ALIGN, gamepak_ram_buffer_size);

    if(gamepak_rom == NULL)
    {
      // Try 16MB, for PSP, then lower in 2MB increments
      gamepak_ram_buffer_size = 16 * 1024 * 1024;
      gamepak_rom = memalign(MEM_ALIGN, gamepak_ram_buffer_size);

      while(gamepak_rom == NULL)
      {
        gamepak_ram_buffer_size -= (2 * 1024 * 1024);
        gamepak_rom = memalign(MEM_ALIGN, gamepak_ram_buffer_size);
      }
    }
    memset(gamepak_rom, 0, gamepak_ram_buffer_size);
  }
#else
  {
    if(psp_model == 1)
    {
      // initialize 32MB (PSP-2000 && CFW 3.71 M33 or higher.)
      gamepak_ram_buffer_size = 32 * 1024 * 1024;
      gamepak_rom = (u8 *)0x0A000000;

      if((gamepak_shelter = malloc(0x400000)) == NULL)
        quit();

      memset(gamepak_shelter, 0, 0x400000);
    }
    else
    {
      // Try 16MB, for PSP, then lower in 2MB increments
      gamepak_ram_buffer_size = 16 * 1024 * 1024;
      gamepak_rom = memalign(MEM_ALIGN, gamepak_ram_buffer_size);

      while(gamepak_rom == NULL)
      {
        gamepak_ram_buffer_size -= (2 * 1024 * 1024);
        gamepak_rom = memalign(MEM_ALIGN, gamepak_ram_buffer_size);
      }
      memset(gamepak_rom, 0, gamepak_ram_buffer_size);
    }
  }
#endif

  // Here's assuming we'll have enough memory left over for this,
  // and that the above succeeded (if not we're in trouble all around)
  gamepak_ram_pages = gamepak_ram_buffer_size / (32 * 1024);
  u32 gamepak_ram_size = sizeof(GAMEPAK_SWAP_ENTRY_TYPE) * gamepak_ram_pages;

  gamepak_memory_map = memalign(MEM_ALIGN, gamepak_ram_size);
  memset(gamepak_memory_map, 0, gamepak_ram_size);
}


void init_memory(void)
{
  u32 map_offset = 0;

  // Fill memory map regions, areas marked as NULL must be checked directly
  MAP_REGION(read, 0x0000000, 0x1000000, 1, bios_rom);
  MAP_NULL(read, 0x1000000, 0x2000000);
  MAP_RAM_REGION(read, 0x2000000, 0x3000000, 8, ewram);
  MAP_RAM_REGION(read, 0x3000000, 0x4000000, 1, iwram);
  MAP_REGION(read, 0x4000000, 0x5000000, 1, io_registers);
  MAP_NULL(read, 0x5000000, 0x6000000);
//  MAP_NULL(read, 0x6000000, 0x7000000);
  MAP_VRAM(read);
  MAP_NULL(read, 0x7000000, 0x8000000);
  init_memory_gamepak();
  MAP_NULL(read, 0xE000000, 0x10000000);

  // Fill memory map regions, areas marked as NULL must be checked directly
  MAP_NULL(write, 0x0000000, 0x2000000);
  MAP_RAM_REGION(write, 0x2000000, 0x3000000, 8, ewram);
  MAP_RAM_REGION(write, 0x3000000, 0x4000000, 1, iwram);
  MAP_NULL(write, 0x4000000, 0x5000000);
  MAP_NULL(write, 0x5000000, 0x6000000);

  // The problem here is that the current method of handling self-modifying code
  // requires writeable memory to be proceeded by 32KB SMC data areas or be
  // indirectly writeable. It's possible to get around this if you turn off the SMC
  // check altogether, but this will make a good number of ROMs crash (perhaps most
  // of the ones that actually need it? This has yet to be determined).

  // This is because VRAM cannot be efficiently made incontiguous, and still allow
  // the renderer to work as efficiently. It would, at the very least, require a
  // lot of hacking of the renderer which I'm not prepared to do.

  // However, it IS possible to directly map the first page no matter what because
  // there's 32kb of blank stuff sitting beneath it.
  if(direct_map_vram)
  {
    MAP_VRAM(write);
  }
  else
  {
    MAP_NULL(write, 0x6000000, 0x7000000);
  }

  MAP_NULL(write, 0x7000000, 0x8000000);
  MAP_NULL(write, 0x8000000, 0xE000000);
  MAP_NULL(write, 0xE000000, 0x10000000);

  memset(io_registers, 0, 0x400);
  memset(oam_ram, 0, 0x400);
  memset(palette_ram, 0, 0x400);
  memset(iwram, 0, 0x10000);
  memset(ewram, 0, 0x80000);
  memset(vram, 0, 0x18000);

  io_registers[REG_DISPCNT]  = 0x80;
  io_registers[REG_DISPSTAT] = 0x03;
  io_registers[REG_VCOUNT]   = 0xA0;
  io_registers[REG_P1]     = 0x03FF;
  io_registers[REG_BG2PA]  = 0x0100;
  io_registers[REG_BG2PD]  = 0x0100;
  io_registers[REG_BG3PA]  = 0x0100;
  io_registers[REG_BG3PD]  = 0x0100;
  io_registers[REG_RCNT]   = 0x8000;

  waitstate_control(0x0000);

  sram_size = SRAM_SIZE_32KB;

  flash_size = FLASH_SIZE_64KB;
  flash_mode = FLASH_BASE_MODE;
  flash_bank_ptr = gamepak_backup;
  flash_command_position = 0;

  eeprom_size = EEPROM_512_BYTE;
  eeprom_mode = EEPROM_BASE_MODE;
  eeprom_address = 0;
  eeprom_counter = 0;

  rtc_state = RTC_DISABLED;
  memset(rtc_registers, 0, sizeof(rtc_registers));

  bios_read_protect = 0xe129f000;

  enable_tilt_sensor = 0;
}

void bios_region_read_allow(void)
{
  memory_map_read[0] = bios_rom;
}

/*
void bios_region_read_protect(void)
{
  memory_map_read[0] = NULL;
}
*/

void memory_term(void)
{
  if(FILE_CHECK_VALID(gamepak_file_large))
  {
    FILE_CLOSE(gamepak_file_large);
    gamepak_file_large = -1;
  }

  if(gamepak_memory_map != NULL)
  {
    free(gamepak_memory_map);
    gamepak_memory_map = NULL;
  }

#ifndef BUILD_CFW_M33
  if(psp_model == 1)
  {
    if(gamepak_shelter != NULL)
    {
      free(gamepak_shelter);
      gamepak_shelter = NULL;
    }
    gamepak_rom = NULL;
  }
  else
#endif
  {
    if(gamepak_rom != NULL)
    {
      free(gamepak_rom);
      gamepak_rom = NULL;
    }
  }
}


void memory_write_mem_savestate(FILE_TAG_TYPE savestate_file);
void memory_read_savestate(FILE_TAG_TYPE savestate_file);

#define SAVESTATE_BLOCK(type)                                                 \
  cpu_##type##_savestate(savestate_file);                                     \
  input_##type##_savestate(savestate_file);                                   \
  main_##type##_savestate(savestate_file);                                    \
  memory_##type##_savestate(savestate_file);                                  \
  sound_##type##_savestate(savestate_file);                                   \
  video_##type##_savestate(savestate_file)                                    \

void load_state(char *savestate_filename)
{
  FILE_TAG_TYPE savestate_file;
  char savestate_path[MAX_PATH];

  sprintf(savestate_path, "%s/%s", dir_save, savestate_filename);

  FILE_OPEN(savestate_file, savestate_path, READ);

  if(FILE_CHECK_VALID(savestate_file))
  {
    char current_gamepak_filename[MAX_FILE];

    FILE_SEEK(savestate_file, (240 * 160 * 2) + sizeof(u64), SEEK_SET);

    strcpy(current_gamepak_filename, gamepak_filename);

    SAVESTATE_BLOCK(read);
    FILE_CLOSE(savestate_file);

    flush_translation_cache_ram();
    flush_translation_cache_rom();
    flush_translation_cache_bios();

    oam_update = 1;
    gbc_sound_update = 1;
/*
    if(strcmp(current_gamepak_filename, gamepak_filename))
    {
      // We'll let it slide if the filenames of the savestate and
      // the gamepak are similar enough.
      u32 dot_position = strcspn(current_gamepak_filename, ".");
      if(strncmp(savestate_filename, current_gamepak_filename, dot_position))
      {
        if(load_gamepak(gamepak_filename) != -1)
        {
          reset_gba();
          // Okay, so this takes a while, but for now it works.
          load_state(savestate_filename);
        }
        else
        {
          quit();
        }
        return;
      }
    }
*/
    reg[CHANGED_PC_STATUS] = 1;
  }
}

void save_state(char *savestate_filename, u16 *screen_capture)
{
  FILE_TAG_TYPE savestate_file;
  char savestate_path[MAX_PATH];

  sprintf(savestate_path, "%s/%s", dir_save, savestate_filename);

  memset(savestate_write_buffer, 0 , sizeof(savestate_write_buffer));
  write_mem_ptr = savestate_write_buffer;

  FILE_OPEN(savestate_file, savestate_path, WRITE);

  if(FILE_CHECK_VALID(savestate_file))
  {
    u64 current_time;
    FILE_WRITE_MEM(savestate_file, screen_capture, 240 * 160 * 2);

    sceRtcGetCurrentTick(&current_time);
    FILE_WRITE_MEM_VARIABLE(savestate_file, current_time);

    SAVESTATE_BLOCK(write_mem);
    FILE_WRITE(savestate_file, savestate_write_buffer,
     sizeof(savestate_write_buffer));
    FILE_CLOSE(savestate_file);
  }
}


#define MEMORY_SAVESTATE_BODY(type)                                           \
{                                                                             \
  u8 i;                                                                       \
                                                                              \
  FILE_##type##_VARIABLE(savestate_file, backup_type);                        \
  FILE_##type##_VARIABLE(savestate_file, sram_size);                          \
  FILE_##type##_VARIABLE(savestate_file, flash_mode);                         \
  FILE_##type##_VARIABLE(savestate_file, flash_command_position);             \
  FILE_##type##_VARIABLE(savestate_file, flash_bank_ptr);                     \
  FILE_##type##_VARIABLE(savestate_file, flash_device_id);                    \
  FILE_##type##_VARIABLE(savestate_file, flash_manufacturer_id);              \
  FILE_##type##_VARIABLE(savestate_file, flash_size);                         \
  FILE_##type##_VARIABLE(savestate_file, eeprom_size);                        \
  FILE_##type##_VARIABLE(savestate_file, eeprom_mode);                        \
  FILE_##type##_VARIABLE(savestate_file, eeprom_address_length);              \
  FILE_##type##_VARIABLE(savestate_file, eeprom_address);                     \
  FILE_##type##_VARIABLE(savestate_file, eeprom_counter);                     \
  FILE_##type##_VARIABLE(savestate_file, rtc_state);                          \
  FILE_##type##_VARIABLE(savestate_file, rtc_write_mode);                     \
  FILE_##type##_ARRAY(savestate_file, rtc_registers);                         \
  FILE_##type##_VARIABLE(savestate_file, rtc_command);                        \
  FILE_##type##_ARRAY(savestate_file, rtc_data);                              \
  FILE_##type##_VARIABLE(savestate_file, rtc_status);                         \
  FILE_##type##_VARIABLE(savestate_file, rtc_data_bytes);                     \
  FILE_##type##_VARIABLE(savestate_file, rtc_bit_count);                      \
  FILE_##type##_ARRAY(savestate_file, eeprom_buffer);                         \
  FILE_##type##_ARRAY(savestate_file, gamepak_filename);                      \
  FILE_##type##_ARRAY(savestate_file, dma);                                   \
                                                                              \
  FILE_##type(savestate_file, iwram + 0x8000, 0x8000);                        \
  for(i = 0; i < 8; i++)                                                      \
  {                                                                           \
    FILE_##type(savestate_file, ewram + (i * 0x10000) + 0x8000, 0x8000);      \
  }                                                                           \
  FILE_##type(savestate_file, vram, 0x18000);                                 \
  FILE_##type(savestate_file, oam_ram, 0x400);                                \
  FILE_##type(savestate_file, palette_ram, 0x400);                            \
  FILE_##type(savestate_file, io_registers, 0x400);                           \
                                                                              \
  /* This is a hack, for now. */                                              \
  if((flash_bank_ptr < gamepak_backup) ||                                     \
   (flash_bank_ptr > (gamepak_backup + (1024 * 64))))                         \
  {                                                                           \
    flash_bank_ptr = gamepak_backup;                                          \
  }                                                                           \
}                                                                             \

void memory_read_savestate(FILE_TAG_TYPE savestate_file)
  MEMORY_SAVESTATE_BODY(READ);

void memory_write_mem_savestate(FILE_TAG_TYPE savestate_file)
  MEMORY_SAVESTATE_BODY(WRITE_MEM);

