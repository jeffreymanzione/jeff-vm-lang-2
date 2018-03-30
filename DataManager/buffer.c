/*
 * buffer.c
 *
 *  Created on: Nov 10, 2017
 *      Author: Jeff
 */

#include "buffer.h"

#include <stdbool.h>
#include <string.h>

#include "error.h"
#include "memory.h"

WBuffer *buffer_init(WBuffer * const buffer, FILE *file, size_t size) {
  buffer->file = file;
  buffer->pos = 0;
  buffer->buff_size = size;
  buffer->buff = ALLOC_ARRAY(unsigned char, buffer->buff_size);
  return buffer;
}

void buffer_flush(WBuffer * const buffer) {
//  DEBUGF("FLUSHING");
  ASSERT(NOT_NULL(buffer));
  S_ASSERT(
      fwrite(buffer->buff, sizeof(char), buffer->pos, buffer->file)
          == buffer->pos);
  fflush(buffer->file);
  buffer->pos = 0;
}

void buffer_finalize(WBuffer * const buffer) {
  ASSERT(NOT_NULL(buffer));
  buffer_flush(buffer);
  DEALLOC(buffer->buff);
}

void buffer_write(WBuffer * const buffer, const char *start, int num_bytes) {
//  DEBUGF("buffer_write(buffer,...,%d)", num_bytes);
  ASSERT(NOT_NULL(buffer), NOT_NULL(start), num_bytes >= 0);
  int i = 0;
  while (i < num_bytes) {
//    DEBUGF("buffer_write\ti=%d, num_bytes=%d", i, num_bytes);
    if (buffer->buff_size == buffer->pos) {
      buffer_flush(buffer);
    }
//    DEBUGF("buffer_write\tbuffer->pos=%d", buffer->pos);
    int write_amount = min((buffer->buff_size - buffer->pos), num_bytes - i);
//    DEBUGF("buffer_write\twrite_amount=%d", write_amount);
    memcpy(buffer->buff + buffer->pos, start + i, write_amount);
    buffer->pos += write_amount;
//    DEBUGF("buffer_write\tbuffer->pos=%d", buffer->pos);
    i += write_amount;
  }
}
