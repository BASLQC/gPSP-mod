/* unofficial gameplaySP kai
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 * Copyright (C) 2006 SiberianSTAR
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

#define ZIP_BUFFER_SIZE (128 * 1024)

void print_unzip_status(u32 output_size, u32 total_size);

struct SZIPFileDataDescriptor
{
  s32 CRC32;
  s32 CompressedSize;
  s32 UncompressedSize;
} __attribute__((packed));

struct SZIPFileHeader
{
  char Sig[4];
  s16 VersionToExtract;
  s16 GeneralBitFlag;
  s16 CompressionMethod;
  s16 LastModFileTime;
  s16 LastModFileDate;
  struct SZIPFileDataDescriptor DataDescriptor;
  s16 FilenameLength;
  s16 ExtraFieldLength;
}  __attribute__((packed));

s32 load_file_zip(char *filename)
{
  FILE_TAG_TYPE fd;
  struct SZIPFileHeader data;
  char tmp[1024];
  s32 retval = -1;
  u8 *buffer = NULL;
  u8 *cbuffer;
  char *ext;

  FILE_OPEN(fd, filename, READ);

  if(!FILE_CHECK_VALID(fd))
  {
    return -1;
  }

  FILE_READ(fd, &data, sizeof(struct SZIPFileHeader));

  if( data.Sig[0] != 0x50 || data.Sig[1] != 0x4B ||
      data.Sig[2] != 0x03 || data.Sig[3] != 0x04 )
  {
    goto outcode;
  }

  FILE_READ(fd, tmp, data.FilenameLength);
  tmp[data.FilenameLength] = 0; // end string

  if(data.ExtraFieldLength)
  {
    FILE_SEEK(fd, data.ExtraFieldLength, SEEK_CUR);
  }

  if(data.GeneralBitFlag & 0x0008)
  {
    FILE_READ(fd, &data.DataDescriptor, sizeof(struct SZIPFileDataDescriptor));
  }

  ext = strrchr(tmp, '.') + 1;

  // file is too big
  if(data.DataDescriptor.UncompressedSize > gamepak_ram_buffer_size)
  {
    goto outcode;
  }

  if(!strcasecmp(ext, "bin") || !strcasecmp(ext, "gba") ||
     !strcasecmp(ext, "agb"))
  {
    buffer = gamepak_rom;

    // ok, found
    switch(data.CompressionMethod)
    {
      case 0: // Z_NO_COMPRESSION
        retval = data.DataDescriptor.UncompressedSize;
        FILE_READ(fd, buffer, retval);
        goto outcode;

      case 8: // Z_DEFLATED
      {
        z_stream stream;
        s32 err;

        cbuffer = malloc(ZIP_BUFFER_SIZE);

        if(cbuffer == NULL)
          goto outcode;

        memset(cbuffer, 0, ZIP_BUFFER_SIZE);

        stream.next_in = (Bytef*)cbuffer;
        stream.avail_in = (u32)ZIP_BUFFER_SIZE;

        stream.next_out = (Bytef*)buffer;
        stream.avail_out = data.DataDescriptor.UncompressedSize;

        stream.zalloc = Z_NULL;
        stream.zfree  = Z_NULL;
        stream.opaque = Z_NULL;

        err = inflateInit2(&stream, -MAX_WBITS);

        FILE_READ(fd, cbuffer, ZIP_BUFFER_SIZE);

        if(err == Z_OK)
        {
          retval = (u32)data.DataDescriptor.UncompressedSize;

          while(err != Z_STREAM_END)
          {
            err = inflate(&stream, Z_SYNC_FLUSH);

            if(err == Z_BUF_ERROR)
            {
              stream.avail_in = ZIP_BUFFER_SIZE;
              stream.next_in = (Bytef*)cbuffer;
              FILE_READ(fd, cbuffer, ZIP_BUFFER_SIZE);
            }

            print_unzip_status((u32)stream.total_out, (u32)retval);
          }
          err = Z_OK;
          inflateEnd(&stream);
        }
        free(cbuffer);
        goto outcode;
      }
    }
  }

outcode:
  FILE_CLOSE(fd);

  return retval;
}


void print_unzip_status(u32 output_size, u32 total_size)
{
  char pbuffer[40];

  clear_screen(0x0000);
  sprintf(pbuffer, "Loading ROM... %3d%%", (int)(output_size * 100 / total_size));
  print_string(pbuffer, 0xFFFF, 0x0000, 5, 5);
  flip_screen(0);
}

