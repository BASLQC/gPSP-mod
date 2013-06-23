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


#define SOUND_BUFFER_SIZE (SOUND_SAMPLES * 2)
#define RING_BUFFER_SIZE  (65536)

u32 option_enable_audio = 1;
u8 gbc_sound_update = 0;

typedef enum
{
  DIRECT_SOUND_INACTIVE,
  DIRECT_SOUND_RIGHT,
  DIRECT_SOUND_LEFT,
  DIRECT_SOUND_LEFTRIGHT
} DIRECT_SOUND_STATUS_TYPE;

typedef enum
{
  DIRECT_SOUND_VOLUME_50,
  DIRECT_SOUND_VOLUME_100
} DIRECT_SOUND_VOLUME_TYPE;

typedef enum
{
  GBC_SOUND_INACTIVE,
  GBC_SOUND_RIGHT,
  GBC_SOUND_LEFT,
  GBC_SOUND_LEFTRIGHT
} GBC_SOUND_STATUS_TYPE;

typedef struct
{
  s8 fifo[32];
  u8 fifo_base;
  u8 fifo_top;
  FIXED08_24 fifo_fractional;
  // The + 1 is to give some extra room for linear interpolation
  // when wrapping around.
  u32 buffer_index;
  DIRECT_SOUND_STATUS_TYPE status;
  DIRECT_SOUND_VOLUME_TYPE volume;
} DIRECT_SOUND_STRUCT;

typedef struct
{
  u16 rate;
  FIXED08_24 frequency_step;
  FIXED08_24 sample_index;
  FIXED08_24 tick_counter;
  u8 total_volume;
  u8 envelope_initial_volume;
  u8 envelope_volume;
  u8 envelope_direction;
  u8 envelope_status;
  u8 envelope_step;
  u8 envelope_ticks;
  u8 envelope_initial_ticks;
  u8 sweep_status;
  u8 sweep_direction;
  u8 sweep_ticks;
  u8 sweep_initial_ticks;
  u8 sweep_shift;
  u8 length_status;
  u16 length_ticks;
  u8 noise_type;
  u8 wave_type;
  u8 wave_bank;
  u16 wave_volume;
  GBC_SOUND_STATUS_TYPE status;
  u8 active_flag;
  u8 master_enable;
  s8 *sample_data;
} GBC_SOUND_STRUCT;

DIRECT_SOUND_STRUCT ALIGN_DATA direct_sound_channel[2];
GBC_SOUND_STRUCT    ALIGN_DATA gbc_sound_channel[4];

SceUID sound_thread = -1;
int sound_handle = -1;
static void setup_sound_thread(void);

u8 sound_active = 0;
static void sound_callback(s16 *stream, u16 length);
static int sound_update_thread(SceSize args, void *argp);

u8 sound_sleep = 0;
static void sound_thread_wakeup(void);

static s16 __attribute__((aligned(64))) psp_sound_buffer[2][SOUND_BUFFER_SIZE];
static s16 ALIGN_DATA sound_buffer[RING_BUFFER_SIZE];

u8 sound_on = 0;

u32 sound_buffer_base = 0;
static u32 buffer_length(u32 top, u32 base, u32 length);

static void sound_reset_fifo(u8 channel);

static u64 delta_ticks(u32 ticks, u32 last_ticks);


u8 gbc_sound_wave_update = 0x00;
u8 user_wave_bank = 0x00;
u8 ALIGN_DATA wave_ram_data[32];
s8 ALIGN_DATA wave_samples[64];
const u16 ALIGN_DATA gbc_sound_wave_volume[4] = { 0, 16384, 8192, 4096 };

u32 noise_index = 0;
u32 ALIGN_DATA noise_table15[1024];
u32 ALIGN_DATA noise_table7[4];
static void init_noise_table(u32 *table, u32 period, u32 bit_length);

// Initial pattern data = 4bits (signed)
// Channel volume = 12bits
// Envelope volume = 14bits
// Master volume = 2bits

// Recalculate left and right volume as volume changes.
// To calculate the current sample, use (sample * volume) >> 16

// Square waves range from -8 (low) to 7 (high)

s8 ALIGN_DATA square_pattern_duty[4][8] =
{
  { 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0x07 },
  { 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0x07, 0x07 },
  { 0xF8, 0xF8, 0xF8, 0xF8, 0x07, 0x07, 0x07, 0x07 },
  { 0xF8, 0xF8, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }
};

const u32 ALIGN_DATA gbc_sound_master_volume_table[4] = { 1, 2, 4, 0 };

const u32 ALIGN_DATA gbc_sound_channel_volume_table[8] =
{
  FIXED_DIV(0, 7, 12),
  FIXED_DIV(1, 7, 12),
  FIXED_DIV(2, 7, 12),
  FIXED_DIV(3, 7, 12),
  FIXED_DIV(4, 7, 12),
  FIXED_DIV(5, 7, 12),
  FIXED_DIV(6, 7, 12),
  FIXED_DIV(7, 7, 12)
};

const u32 ALIGN_DATA gbc_sound_envelope_volume_table[16] =
{
  FIXED_DIV( 0, 15, 14),
  FIXED_DIV( 1, 15, 14),
  FIXED_DIV( 2, 15, 14),
  FIXED_DIV( 3, 15, 14),
  FIXED_DIV( 4, 15, 14),
  FIXED_DIV( 5, 15, 14),
  FIXED_DIV( 6, 15, 14),
  FIXED_DIV( 7, 15, 14),
  FIXED_DIV( 8, 15, 14),
  FIXED_DIV( 9, 15, 14),
  FIXED_DIV(10, 15, 14),
  FIXED_DIV(11, 15, 14),
  FIXED_DIV(12, 15, 14),
  FIXED_DIV(13, 15, 14),
  FIXED_DIV(14, 15, 14),
  FIXED_DIV(15, 15, 14)
};

u8 gbc_sound_master_volume_left;
u8 gbc_sound_master_volume_right;
u8 gbc_sound_master_volume;

u32 gbc_sound_buffer_index = 0;
u32 gbc_sound_last_cpu_ticks = 0;
u32 gbc_sound_partial_ticks = 0;

FIXED08_24 gbc_sound_tick_step;


void gbc_sound_tone_control_low(u8 channel, u32 value)
{
  u8 initial_volume = (value >> 12) & 0x0F;
  u8 envelope_ticks = ((value >> 8) & 0x07) * 4;

  gbc_sound_channel[channel].length_ticks = 64 - (value & 0x3F);
  gbc_sound_channel[channel].sample_data =
   square_pattern_duty[(value >> 6) & 0x03];
  gbc_sound_channel[channel].envelope_direction = (value >> 11) & 0x01;
  gbc_sound_channel[channel].envelope_initial_volume = initial_volume;
  gbc_sound_channel[channel].envelope_volume = initial_volume;
  gbc_sound_channel[channel].envelope_initial_ticks = envelope_ticks;
  gbc_sound_channel[channel].envelope_ticks = envelope_ticks;
  gbc_sound_channel[channel].envelope_status = (envelope_ticks != 0);

  gbc_sound_update = 1;
}

void gbc_sound_tone_control_high(u8 channel, u32 value)
{
  u16 rate = value & 0x7FF;

  gbc_sound_channel[channel].rate = rate;
  gbc_sound_channel[channel].frequency_step =
   FLOAT_TO_FP08_24(((131072.0 / (2048 - rate)) * 8.0) / SOUND_FREQUENCY);
  gbc_sound_channel[channel].length_status = (value >> 14) & 0x01;

  if(value & 0x8000)
  {
    gbc_sound_channel[channel].active_flag = 1;
    gbc_sound_channel[channel].sample_index -= FLOAT_TO_FP08_24(1.0 / 12.0);
    gbc_sound_channel[channel].envelope_ticks =
     gbc_sound_channel[channel].envelope_initial_ticks;
    gbc_sound_channel[channel].envelope_volume =
     gbc_sound_channel[channel].envelope_initial_volume;
    gbc_sound_channel[channel].sweep_ticks =
     gbc_sound_channel[channel].sweep_initial_ticks;
  }

  gbc_sound_update = 1;
}

void gbc_sound_tone_control_sweep(u32 value)
{
  u8 sweep_shift = value & 0x07;
  u8 sweep_ticks = ((value >> 4) & 0x07) * 2;

  gbc_sound_channel[0].sweep_status = (sweep_shift != 0) && (sweep_ticks != 0);
  gbc_sound_channel[0].sweep_shift = sweep_shift;
  gbc_sound_channel[0].sweep_direction = (value >> 3) & 0x01;
  gbc_sound_channel[0].sweep_ticks = sweep_ticks;
  gbc_sound_channel[0].sweep_initial_ticks = sweep_ticks;

  gbc_sound_update = 1;
}

void gbc_sound_wave_control(u32 value)
{
  u8 play_wave_bank = (value >> 6) & 0x01;

  if(gbc_sound_channel[2].wave_bank != play_wave_bank)
    user_wave_bank = (play_wave_bank << 4) ^ 0x10;

  gbc_sound_channel[2].wave_type = (value >> 5) & 0x01;
  gbc_sound_channel[2].wave_bank = play_wave_bank;

  if(value & 0x80)
    gbc_sound_channel[2].master_enable = 1;
  else
    gbc_sound_channel[2].master_enable = 0;

  gbc_sound_update = 1;
}

void gbc_sound_tone_control_low_wave(u32 value)
{
  gbc_sound_channel[2].length_ticks = 256 - (value & 0xFF);

  if(value & 0x8000)
  {
    gbc_sound_channel[2].wave_volume = 12288;
  }
  else
  {
    gbc_sound_channel[2].wave_volume =
     gbc_sound_wave_volume[(value >> 13) & 0x03];
  }

  gbc_sound_update = 1;
}

void gbc_sound_tone_control_high_wave(u32 value)
{
  u16 rate = value & 0x7FF;

  gbc_sound_channel[2].rate = rate;
  gbc_sound_channel[2].frequency_step =
   FLOAT_TO_FP08_24((2097152.0 / (2048 - rate)) / SOUND_FREQUENCY);
  gbc_sound_channel[2].length_status = (value >> 14) & 0x01;

  if(value & 0x8000)
  {
    gbc_sound_channel[2].sample_index = 0;
    gbc_sound_channel[2].active_flag = 1;
  }

  gbc_sound_update = 1;
}

void gbc_sound_wave_pattern_ram8(u32 address, u32 value)
{
  if(user_wave_bank)
    gbc_sound_wave_update |= 0x10;
  else
    gbc_sound_wave_update |= 0x01;

  ADDRESS8(wave_ram_data, (user_wave_bank | (address & 0x0F))) = value;
}

void gbc_sound_wave_pattern_ram16(u32 address, u32 value)
{
  if(user_wave_bank)
    gbc_sound_wave_update |= 0x10;
  else
    gbc_sound_wave_update |= 0x01;

  ADDRESS16(wave_ram_data, (user_wave_bank | (address & 0x0e))) = value;
}

void gbc_sound_noise_control(u32 value)
{
  u8 dividing_ratio = value & 0x07;
  u8 frequency_shift = (value >> 4) & 0x0F;

  if(dividing_ratio == 0)
  {
    gbc_sound_channel[3].frequency_step =
     FLOAT_TO_FP08_24(1048576.0 / (1 << (frequency_shift + 1))
     / SOUND_FREQUENCY);
  }
  else
  {
    gbc_sound_channel[3].frequency_step =
     FLOAT_TO_FP08_24(524288.0 / (dividing_ratio * (1 << (frequency_shift + 1)))
     / SOUND_FREQUENCY);
  }

  gbc_sound_channel[3].noise_type = (value >> 3) & 0x01;
  gbc_sound_channel[3].length_status = (value >> 14) & 0x01;

  if(value & 0x8000)
  {
    noise_index = 0;
    gbc_sound_channel[3].sample_index = 0;
    gbc_sound_channel[3].active_flag = 1;
    gbc_sound_channel[3].envelope_ticks =
     gbc_sound_channel[3].envelope_initial_ticks;
    gbc_sound_channel[3].envelope_volume =
     gbc_sound_channel[3].envelope_initial_volume;
  }

  gbc_sound_update = 1;
}

#define GBC_SOUND_CHANNEL_STATUS(channel)                                     \
  gbc_sound_channel[channel].status =                                         \
   ((value >> (channel + 11)) & 0x02) | ((value >> (channel + 8)) & 0x01)     \

void sound_control_low(u32 value)
{
  gbc_sound_master_volume_right = value & 0x07;
  gbc_sound_master_volume_left = (value >> 4) & 0x07;

  GBC_SOUND_CHANNEL_STATUS(0);
  GBC_SOUND_CHANNEL_STATUS(1);
  GBC_SOUND_CHANNEL_STATUS(2);
  GBC_SOUND_CHANNEL_STATUS(3);
}

void sound_control_high(u32 value)
{
  gbc_sound_master_volume = value & 0x03;

  direct_sound_timer_select(value);
  direct_sound_channel[0].volume = (value >>  2) & 0x01;
  direct_sound_channel[0].status = (value >>  8) & 0x03;
  direct_sound_channel[1].volume = (value >>  3) & 0x01;
  direct_sound_channel[1].status = (value >> 12) & 0x03;

  if(value & 0x0800)
    sound_reset_fifo(0);

  if(value & 0x8000)
    sound_reset_fifo(1);
}

void sound_control_x(u32 value)
{
  if(value & 0x80)
  {
    sound_on = 1;
  }
  else
  {
    gbc_sound_channel[0].active_flag = 0;
    gbc_sound_channel[1].active_flag = 0;
    gbc_sound_channel[2].active_flag = 0;
    gbc_sound_channel[3].active_flag = 0;
    sound_on = 0;
  }
}


u64 delta_ticks(u32 now_ticks, u32 last_ticks)
{
  if(now_ticks == last_ticks)
    return 0ULL;

  if(now_ticks > last_ticks)
    return (u64)now_ticks - last_ticks;

  return (4294967296ULL - last_ticks) + now_ticks;
}

void adjust_direct_sound_buffer(u8 channel, u32 cpu_ticks)
{
  u64 count_ticks;
  u32 buffer_ticks, partial_ticks;

  count_ticks =
   delta_ticks(cpu_ticks, gbc_sound_last_cpu_ticks) * SOUND_FREQUENCY;

  buffer_ticks = count_ticks >> 24;
  partial_ticks = gbc_sound_partial_ticks + (count_ticks & 0x00FFFFFF);

  if(partial_ticks > 0x00FFFFFF)
    buffer_ticks++;

  direct_sound_channel[channel].buffer_index =
   (gbc_sound_buffer_index + (buffer_ticks * 2)) % RING_BUFFER_SIZE;
}

void sound_timer_queue32(u8 channel)
{
  DIRECT_SOUND_STRUCT *ds = direct_sound_channel + channel;
  u8 i, offset;

  offset = channel * 4;

  for(i = 0xA0; i <= 0xA3; i++)
  {
    ds->fifo[ds->fifo_top] = ADDRESS8(io_registers, i + offset);
    ds->fifo_top = (ds->fifo_top + 1) % 32;
  }
}

static void sound_reset_fifo(u8 channel)
{
  DIRECT_SOUND_STRUCT *ds = direct_sound_channel + channel;

  ds->fifo_top = 0;
  ds->fifo_base = 0;
  memset(ds->fifo, 0, 32);
}

static u32 buffer_length(u32 top, u32 base, u32 length)
{
  if(top == base)
    return 0;

  if(top > base)
    return (top - base);

  return (length - base + top);
}


// Unqueue 1 sample from the base of the DS FIFO and place it on the audio
// buffer for as many samples as necessary. If the DS FIFO is 16 bytes or
// smaller and if DMA is enabled for the sound channel initiate a DMA transfer
// to the DS FIFO.

#define RENDER_SAMPLE_NULL()                                                  \

#define RENDER_SAMPLE_RIGHT()                                                 \
  sound_buffer[buffer_index + 1] += current_sample +                          \
   FP08_24_TO_U32((s64)(next_sample - current_sample) * fifo_fractional)      \

#define RENDER_SAMPLE_LEFT()                                                  \
  sound_buffer[buffer_index    ] += current_sample +                          \
   FP08_24_TO_U32((s64)(next_sample - current_sample) * fifo_fractional)      \

#define RENDER_SAMPLE_BOTH()                                                  \
  dest_sample = current_sample +                                              \
   FP08_24_TO_U32((s64)(next_sample - current_sample) * fifo_fractional);     \
  sound_buffer[buffer_index    ] += dest_sample;                              \
  sound_buffer[buffer_index + 1] += dest_sample                               \

#define RENDER_SAMPLES(type)                                                  \
  while(fifo_fractional <= 0x00FFFFFF)                                        \
  {                                                                           \
    RENDER_SAMPLE_##type();                                                   \
    fifo_fractional += frequency_step;                                        \
    buffer_index = (buffer_index + 2) % RING_BUFFER_SIZE;                     \
  }                                                                           \

void sound_timer(FIXED08_24 frequency_step, u8 channel)
{
  DIRECT_SOUND_STRUCT *ds = direct_sound_channel + channel;

  FIXED08_24 fifo_fractional = ds->fifo_fractional;
  u32 buffer_index = ds->buffer_index;
  s16 current_sample, next_sample, dest_sample;

  current_sample = ds->fifo[ds->fifo_base] << 4;
  ds->fifo[ds->fifo_base] = 0;

  ds->fifo_base = (ds->fifo_base + 1) % 32;
  next_sample = ds->fifo[ds->fifo_base] << 4;

  if(sound_on == 1)
  {
    if(ds->volume == DIRECT_SOUND_VOLUME_50)
    {
      current_sample >>= 1;
      next_sample >>= 1;
    }

    switch(ds->status)
    {
      case DIRECT_SOUND_INACTIVE:
        RENDER_SAMPLES(NULL);
        break;

      case DIRECT_SOUND_RIGHT:
        RENDER_SAMPLES(RIGHT);
        break;

      case DIRECT_SOUND_LEFT:
        RENDER_SAMPLES(LEFT);
        break;

      case DIRECT_SOUND_LEFTRIGHT:
        RENDER_SAMPLES(BOTH);
        break;
    }
  }
  else
  {
    RENDER_SAMPLES(NULL);
  }

  sound_thread_wakeup();

  ds->buffer_index = buffer_index;
  ds->fifo_fractional = fifo_fractional - 0x01000000;

  if(buffer_length(ds->fifo_top, ds->fifo_base, 32) <= 16)
  {
    if(dma[1].direct_sound_channel == channel)
      dma_transfer(dma + 1);

    if(dma[2].direct_sound_channel == channel)
      dma_transfer(dma + 2);
  }
}


#define UPDATE_VOLUME_CHANNEL_ENVELOPE(channel)                               \
  volume_##channel = gbc_sound_envelope_volume_table[envelope_volume] *       \
   gbc_sound_channel_volume_table[gbc_sound_master_volume_##channel] *        \
   gbc_sound_master_volume_table[gbc_sound_master_volume]                     \

#define UPDATE_VOLUME_CHANNEL_NOENVELOPE(channel)                             \
  volume_##channel = gs->wave_volume *                                        \
   gbc_sound_channel_volume_table[gbc_sound_master_volume_##channel] *        \
   gbc_sound_master_volume_table[gbc_sound_master_volume]                     \

#define UPDATE_VOLUME(type)                                                   \
  UPDATE_VOLUME_CHANNEL_##type(left);                                         \
  UPDATE_VOLUME_CHANNEL_##type(right)                                         \

#define UPDATE_TONE_SWEEP()                                                   \
  if(gs->sweep_status)                                                        \
  {                                                                           \
    u8 sweep_ticks = gs->sweep_ticks - 1;                                     \
                                                                              \
    if(sweep_ticks == 0)                                                      \
    {                                                                         \
      u16 rate = gs->rate;                                                    \
                                                                              \
      if(gs->sweep_direction)                                                 \
        rate = rate - (rate >> gs->sweep_shift);                              \
      else                                                                    \
        rate = rate + (rate >> gs->sweep_shift);                              \
                                                                              \
      if(rate > 2047)                                                         \
      {                                                                       \
        gs->active_flag = 0;                                                  \
        break;                                                                \
      }                                                                       \
                                                                              \
      frequency_step =                                                        \
       FLOAT_TO_FP08_24(((131072.0 / (2048 - rate)) * 8.0) / SOUND_FREQUENCY);\
                                                                              \
      gs->frequency_step = frequency_step;                                    \
      gs->rate = rate;                                                        \
                                                                              \
      sweep_ticks = gs->sweep_initial_ticks;                                  \
    }                                                                         \
    gs->sweep_ticks = sweep_ticks;                                            \
  }                                                                           \

#define UPDATE_TONE_NOSWEEP()                                                 \

#define UPDATE_TONE_ENVELOPE()                                                \
  if(gs->envelope_status)                                                     \
  {                                                                           \
    u8 envelope_ticks = gs->envelope_ticks - 1;                               \
    envelope_volume = gs->envelope_volume;                                    \
                                                                              \
    if(envelope_ticks == 0)                                                   \
    {                                                                         \
      if(gs->envelope_direction)                                              \
      {                                                                       \
        if(envelope_volume != 15)                                             \
          envelope_volume++;                                                  \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        if(envelope_volume != 0)                                              \
          envelope_volume--;                                                  \
      }                                                                       \
                                                                              \
      UPDATE_VOLUME(ENVELOPE);                                                \
                                                                              \
      gs->envelope_volume = envelope_volume;                                  \
      gs->envelope_ticks = gs->envelope_initial_ticks;                        \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      gs->envelope_ticks = envelope_ticks;                                    \
    }                                                                         \
  }                                                                           \

#define UPDATE_TONE_NOENVELOPE()                                              \

#define UPDATE_TONE_COUNTERS(envelope_op, sweep_op)                           \
  tick_counter += gbc_sound_tick_step;                                        \
  if(tick_counter > 0x00FFFFFF)                                               \
  {                                                                           \
    tick_counter &= 0x00FFFFFF;                                               \
                                                                              \
    if(gs->length_status)                                                     \
    {                                                                         \
      u16 length_ticks = gs->length_ticks - 1;                                \
      gs->length_ticks = length_ticks;                                        \
                                                                              \
      if(length_ticks == 0)                                                   \
      {                                                                       \
        gs->active_flag = 0;                                                  \
        break;                                                                \
      }                                                                       \
    }                                                                         \
                                                                              \
    UPDATE_TONE_##envelope_op();                                              \
    UPDATE_TONE_##sweep_op();                                                 \
  }                                                                           \

#define GBC_SOUND_RENDER_SAMPLE_RIGHT()                                       \
  sound_buffer[buffer_index + 1] += (current_sample * volume_right) >> 22     \

#define GBC_SOUND_RENDER_SAMPLE_LEFT()                                        \
  sound_buffer[buffer_index    ] += (current_sample * volume_left ) >> 22     \

#define GBC_SOUND_RENDER_SAMPLE_BOTH()                                        \
  GBC_SOUND_RENDER_SAMPLE_RIGHT();                                            \
  GBC_SOUND_RENDER_SAMPLE_LEFT()                                              \

#define GBC_SOUND_RENDER_SAMPLES(type, sample_length, envelope_op, sweep_op)  \
  for(i = 0; i < buffer_ticks; i++)                                           \
  {                                                                           \
    current_sample =                                                          \
     sample_data[FP08_24_TO_U32(sample_index) % sample_length];               \
    GBC_SOUND_RENDER_SAMPLE_##type();                                         \
                                                                              \
    sample_index += frequency_step;                                           \
    buffer_index = (buffer_index + 2) % RING_BUFFER_SIZE;                     \
                                                                              \
    UPDATE_TONE_COUNTERS(envelope_op, sweep_op);                              \
  }                                                                           \

#define GBC_NOISE_WRAP_FULL 32767

#define GBC_NOISE_WRAP_HALF 127

#define GET_NOISE_SAMPLE_FULL()                                               \
  current_sample =                                                            \
   ((s32)(noise_table15[noise_index >> 5] << (noise_index & 0x1F)) >> 31)     \
   ^ 0x07                                                                     \

#define GET_NOISE_SAMPLE_HALF()                                               \
  current_sample =                                                            \
   ((s32)(noise_table7[noise_index >> 5] << (noise_index & 0x1F)) >> 31)      \
   ^ 0x07                                                                     \

#define GBC_SOUND_RENDER_NOISE(type, noise_type, envelope_op, sweep_op)       \
  for(i = 0; i < buffer_ticks; i++)                                           \
  {                                                                           \
    GET_NOISE_SAMPLE_##noise_type();                                          \
    GBC_SOUND_RENDER_SAMPLE_##type();                                         \
                                                                              \
    sample_index += frequency_step;                                           \
                                                                              \
    if(sample_index > 0x00FFFFFF)                                             \
    {                                                                         \
      noise_index = (noise_index + 1) % GBC_NOISE_WRAP_##noise_type;          \
      sample_index = FP08_24_FRACTIONAL_PART(sample_index);                   \
    }                                                                         \
                                                                              \
    buffer_index = (buffer_index + 2) % RING_BUFFER_SIZE;                     \
                                                                              \
    UPDATE_TONE_COUNTERS(envelope_op, sweep_op);                              \
  }                                                                           \

#define GBC_SOUND_RENDER_CHANNEL(type, sample_length, envelope_op, sweep_op)  \
  buffer_index = gbc_sound_buffer_index;                                      \
  sample_index = gs->sample_index;                                            \
  frequency_step = gs->frequency_step;                                        \
  tick_counter = gs->tick_counter;                                            \
                                                                              \
  UPDATE_VOLUME(envelope_op);                                                 \
                                                                              \
  switch(gs->status)                                                          \
  {                                                                           \
    case GBC_SOUND_INACTIVE:                                                  \
      break;                                                                  \
                                                                              \
    case GBC_SOUND_RIGHT:                                                     \
      GBC_SOUND_RENDER_##type(RIGHT, sample_length, envelope_op, sweep_op);   \
      break;                                                                  \
                                                                              \
    case GBC_SOUND_LEFT:                                                      \
      GBC_SOUND_RENDER_##type(LEFT, sample_length, envelope_op, sweep_op);    \
      break;                                                                  \
                                                                              \
    case GBC_SOUND_LEFTRIGHT:                                                 \
      GBC_SOUND_RENDER_##type(BOTH, sample_length, envelope_op, sweep_op);    \
      break;                                                                  \
  }                                                                           \
                                                                              \
  gs->sample_index = sample_index;                                            \
  gs->tick_counter = tick_counter                                             \


#define GBC_SOUND_LOAD_WAVE_RAM()                                             \
  for(i = 0, i2 = 0; i < 16; i++, i2 += 2)                                    \
  {                                                                           \
    current_sample = wave_ram[i];                                             \
    wave_bank[i2 + 0] = ((current_sample >> 4) & 0x0F) - 8;                   \
    wave_bank[i2 + 1] = ( current_sample       & 0x0F) - 8;                   \
  }                                                                           \

#define GBC_SOUND_UPDATE_WAVE_RAM()                                           \
  u8 *wave_ram = wave_ram_data;                                               \
  s8 *wave_bank = wave_samples;                                               \
                                                                              \
  /* Wave RAM Bank 0 */                                                       \
  if(gbc_sound_wave_update & 0x01)                                            \
  {                                                                           \
    GBC_SOUND_LOAD_WAVE_RAM();                                                \
  }                                                                           \
                                                                              \
  /* Wave RAM Bank 1 */                                                       \
  if(gbc_sound_wave_update & 0x10)                                            \
  {                                                                           \
    wave_ram += 16;                                                           \
    wave_bank += 32;                                                          \
    GBC_SOUND_LOAD_WAVE_RAM();                                                \
  }                                                                           \
                                                                              \
  gbc_sound_wave_update = 0x00                                                \


#define SOUND_BUFFER_LENGTH                                                   \
  buffer_length(gbc_sound_buffer_index, sound_buffer_base, RING_BUFFER_SIZE)  \

void synchronize_sound(void)
{
  if(synchronize_flag)
  {
    if(SOUND_BUFFER_LENGTH > (SOUND_BUFFER_SIZE * 4))
    {
      while(SOUND_BUFFER_LENGTH > (SOUND_BUFFER_SIZE * 4))
        sound_thread_wakeup();

      if(option_frameskip_type == AUTO_FRAMESKIP)
      {
        sceDisplayWaitVblankStart();
        real_frame_count = 0;
        virtual_frame_count = 0;
      }
    }
  }
}

void update_gbc_sound(u32 cpu_ticks)
{
  u32 i, i2;
  GBC_SOUND_STRUCT *gs = gbc_sound_channel;
  FIXED08_24 sample_index, frequency_step;
  FIXED08_24 tick_counter;
  u32 buffer_index, buffer_ticks;
  s32 volume_left, volume_right;
  u8 envelope_volume;
  s32 current_sample;
  s8 *sample_data;

  u64 count_ticks =
   delta_ticks(cpu_ticks, gbc_sound_last_cpu_ticks) * SOUND_FREQUENCY;

  buffer_ticks = count_ticks >> 24;
  gbc_sound_partial_ticks += count_ticks & 0x00FFFFFF;

  if(gbc_sound_partial_ticks > 0x00FFFFFF)
  {
    buffer_ticks++;
    gbc_sound_partial_ticks &= 0x00FFFFFF;
  }

  u16 sound_status = ADDRESS16(io_registers, 0x84) & 0xFFF0;

  if(sound_on == 1)
  {
    // Sound Channel 1 - Tone & Sweep
    gs = gbc_sound_channel + 0;

    if(gs->active_flag)
    {
      sound_status |= 0x01;
      sample_data = gs->sample_data;
      envelope_volume = gs->envelope_volume;

      GBC_SOUND_RENDER_CHANNEL(SAMPLES, 8, ENVELOPE, SWEEP);

      if(gs->active_flag == 0)
        sound_status &= ~0x01;
    }

    // Sound Channel 2 - Tone
    gs = gbc_sound_channel + 1;

    if(gs->active_flag)
    {
      sound_status |= 0x02;
      sample_data = gs->sample_data;
      envelope_volume = gs->envelope_volume;

      GBC_SOUND_RENDER_CHANNEL(SAMPLES, 8, ENVELOPE, NOSWEEP);

      if(gs->active_flag == 0)
        sound_status &= ~0x02;
    }

    // Sound Channel 3 - Wave Output
    gs = gbc_sound_channel + 2;

    GBC_SOUND_UPDATE_WAVE_RAM();

    if((gs->active_flag) && (gs->master_enable))
    {
      sound_status |= 0x04;
      sample_data = wave_samples;

      if(gs->wave_type)
      {
        GBC_SOUND_RENDER_CHANNEL(SAMPLES, 64, NOENVELOPE, NOSWEEP);
      }
      else
      {
        if(gs->wave_bank)
          sample_data += 32;

        GBC_SOUND_RENDER_CHANNEL(SAMPLES, 32, NOENVELOPE, NOSWEEP);
      }

      if(gs->active_flag == 0)
        sound_status &= ~0x04;
    }

    // Sound Channel 4 - Noise
    gs = gbc_sound_channel + 3;

    if(gs->active_flag)
    {
      sound_status |= 0x08;
      envelope_volume = gs->envelope_volume;

      if(gs->noise_type)
      {
        GBC_SOUND_RENDER_CHANNEL(NOISE, HALF, ENVELOPE, NOSWEEP);
      }
      else
      {
        GBC_SOUND_RENDER_CHANNEL(NOISE, FULL, ENVELOPE, NOSWEEP);
      }

      if(gs->active_flag == 0)
        sound_status &= ~0x08;
    }
  }

  ADDRESS16(io_registers, 0x84) = sound_status;

  gbc_sound_last_cpu_ticks = cpu_ticks;
  gbc_sound_buffer_index =
   (gbc_sound_buffer_index + (buffer_ticks * 2)) % RING_BUFFER_SIZE;

  sound_thread_wakeup();
}


// Special thanks to blarrg for the LSFR frequency used in Meridian, as posted
// on the forum at http://meridian.overclocked.org:
// http://meridian.overclocked.org/cgi-bin/wwwthreads/showpost.pl?Board=merid
// angeneraldiscussion&Number=2069&page=0&view=expanded&mode=threaded&sb=4
// Hope you don't mind me borrowing it ^_-

static void init_noise_table(u32 *table, u32 period, u32 bit_length)
{
  u32 shift_register = 0xFF;
  u32 mask = ~(1 << bit_length);
  s32 table_pos, bit_pos;
  u32 current_entry;
  u32 table_period = (period + 31) / 32;

  // Bits are stored in reverse order so they can be more easily moved to
  // bit 31, for sign extended shift down.

  for(table_pos = 0; table_pos < table_period; table_pos++)
  {
    current_entry = 0;
    for(bit_pos = 31; bit_pos >= 0; bit_pos--)
    {
      current_entry |= (shift_register & 0x01) << bit_pos;

      shift_register =
       ((1 & (shift_register ^ (shift_register >> 1))) << bit_length) |
       ((shift_register >> 1) & mask);
    }

    table[table_pos] = current_entry;
  }
}


static void sound_callback(s16 *stream, u16 length)
{
  u16 i;
  s16 current_sample;

  if(option_enable_audio)
  {
    for(i = 0; i < length; i++)
    {
      current_sample = sound_buffer[sound_buffer_base];

      if(current_sample >  2047) current_sample =  2047;
      if(current_sample < -2048) current_sample = -2048;

      stream[i] = current_sample << 4;
      sound_buffer[sound_buffer_base] = 0;

      sound_buffer_base = (sound_buffer_base + 1) % RING_BUFFER_SIZE;
    }
  }
  else
  {
    for(i = 0; i < length; i++)
    {
      stream[i] = 0;
      sound_buffer[sound_buffer_base] = 0;

      sound_buffer_base = (sound_buffer_base + 1) % RING_BUFFER_SIZE;
    }
  }
}

static void sound_thread_wakeup(void)
{
  if(sound_sleep)
    sceKernelWakeupThread(sound_thread);
}

static int sound_update_thread(SceSize args, void *argp)
{
  u8 bufidx = 0;

  while(sound_active)
  {
    if(sleep_flag)
    {
      do {
        sceKernelDelayThread(500000);
      } while(sleep_flag);
    }

    if(SOUND_BUFFER_LENGTH < SOUND_BUFFER_SIZE)
    {
      sound_sleep = 1;
      sceKernelSleepThread();

      sound_sleep = 0;
      sceKernelDelayThread(1);
      continue;
    }

    sound_callback(psp_sound_buffer[bufidx], SOUND_BUFFER_SIZE);
    sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, psp_sound_buffer[bufidx]);

    bufidx ^= 1;
  }

  sceAudioChRelease(sound_handle);
  sound_handle = -1;

  sceKernelExitThread(0);

  return 0;
}

static void setup_sound_thread(void)
{
  sceAudioSRCChRelease();

  if(sceAudioSRCChReserve(SOUND_SAMPLES, SOUND_FREQUENCY, 2) < 0)
  {
    quit();
  }

  sound_thread = sceKernelCreateThread("Sound thread", sound_update_thread,
                                        0x08, 0x400, PSP_MODULE_USER, NULL);
  if(sound_thread < 0)
  {
    sceAudioSRCChRelease();
    sound_handle = -1;
    quit();
  }

  sound_active = 1;
  sceKernelStartThread(sound_thread, 0, 0);
}

void init_sound(void)
{
  setup_sound_thread();

  gbc_sound_tick_step = FLOAT_TO_FP08_24(256.0 / SOUND_FREQUENCY);

  init_noise_table(noise_table15, 32767, 14);
  init_noise_table(noise_table7, 127, 6);

  reset_sound();
}

void reset_sound(void)
{
  u8 i;

  DIRECT_SOUND_STRUCT *ds = direct_sound_channel;
  GBC_SOUND_STRUCT    *gs = gbc_sound_channel;

  sound_on = 0;
  sound_buffer_base = 0;

  memset(sound_buffer, 0, RING_BUFFER_SIZE);

  memset(psp_sound_buffer[0], 0, SOUND_BUFFER_SIZE);
  memset(psp_sound_buffer[1], 0, SOUND_BUFFER_SIZE);

  for(i = 0; i < 2; i++, ds++)
  {
    ds->buffer_index = 0;
    ds->status = DIRECT_SOUND_INACTIVE;
    ds->fifo_top = 0;
    ds->fifo_base = 0;
    ds->fifo_fractional = 0;
    memset(ds->fifo, 0, 32);
  }

  gbc_sound_buffer_index = 0;
  gbc_sound_last_cpu_ticks = 0;
  gbc_sound_partial_ticks = 0;

  gbc_sound_master_volume_left = 0;
  gbc_sound_master_volume_right = 0;
  gbc_sound_master_volume = 0;

  for(i = 0; i < 4; i++, gs++)
  {
    gs->status = GBC_SOUND_INACTIVE;
    gs->sample_data = square_pattern_duty[2];
    gs->active_flag = 0;
  }

  memset(wave_ram_data, 0x88, 32);
  memset(wave_samples, 0, 64);
  gbc_sound_channel[2].wave_bank = 0;
  user_wave_bank = 0x00;
}

void sound_term(void)
{
  sound_active = 0;

  if(sound_thread >= 0)
  {
    sound_thread_wakeup();

    sceKernelWaitThreadEnd(sound_thread, NULL);
    sceKernelDeleteThread(sound_thread);
    sound_thread = -1;

    sceAudioSRCChRelease();
  }
}


#define SOUND_SAVESTATE_BODY(type)                                          \
{                                                                           \
  FILE_##type##_VARIABLE(savestate_file, sound_on);                         \
  FILE_##type##_VARIABLE(savestate_file, sound_buffer_base);                \
  FILE_##type##_VARIABLE(savestate_file, gbc_sound_buffer_index);           \
  FILE_##type##_VARIABLE(savestate_file, gbc_sound_last_cpu_ticks);         \
  FILE_##type##_VARIABLE(savestate_file, gbc_sound_partial_ticks);          \
  FILE_##type##_VARIABLE(savestate_file, gbc_sound_master_volume_left);     \
  FILE_##type##_VARIABLE(savestate_file, gbc_sound_master_volume_right);    \
  FILE_##type##_VARIABLE(savestate_file, gbc_sound_master_volume);          \
  FILE_##type##_VARIABLE(savestate_file, user_wave_bank);                   \
  FILE_##type##_VARIABLE(savestate_file, noise_index);                      \
  FILE_##type##_ARRAY(savestate_file, wave_samples);                        \
  FILE_##type##_ARRAY(savestate_file, wave_ram_data);                       \
  FILE_##type##_ARRAY(savestate_file, direct_sound_channel);                \
  FILE_##type##_ARRAY(savestate_file, gbc_sound_channel);                   \
}                                                                           \

void sound_read_savestate(FILE_TAG_TYPE savestate_file)
{
  u8 i;

  SOUND_SAVESTATE_BODY(READ);

  for(i = 0; i < 4; i++)
    gbc_sound_channel[i].sample_data = square_pattern_duty[2];
}

void sound_write_mem_savestate(FILE_TAG_TYPE savestate_file)
{
  SOUND_SAVESTATE_BODY(WRITE_MEM);
}

