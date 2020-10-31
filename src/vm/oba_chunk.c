#include <stdint.h>
#include <stdlib.h>

#include "oba_chunk.h"
#include "oba_common.h"

void initChunk(Chunk* chunk) {
  chunk->capacity = 0;
  chunk->count = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  initValueBuffer(&chunk->constants);
}

void freeChunk(Chunk* chunk) {
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
  freeValueBuffer(&chunk->constants);
  initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
  if (chunk->capacity <= chunk->count) {
    int oldCap = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(oldCap);
    chunk->code = GROW_ARRAY(uint8_t, chunk->code, oldCap, chunk->capacity);
    chunk->lines = GROW_ARRAY(int, chunk->lines, oldCap, chunk->capacity);
  }

  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}
