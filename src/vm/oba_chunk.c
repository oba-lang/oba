#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "oba_chunk.h"
#include "oba_common.h"

void initChunk(Chunk* chunk) {
  memset(chunk, 0, sizeof(Chunk));
  chunk->capacity = 0;
  chunk->count = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  initValueBuffer(&chunk->constants);
}

void freeChunk(ObaVM* vm, Chunk* chunk) {
  FREE_ARRAY(vm, uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(vm, int, chunk->lines, chunk->capacity);
  freeValueBuffer(vm, &chunk->constants);
  initChunk(chunk);
}

void writeChunk(ObaVM* vm, Chunk* chunk, uint8_t byte, int line) {
  if (chunk->capacity <= chunk->count) {
    int oldCap = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(oldCap);
    chunk->code = GROW_ARRAY(vm, uint8_t, chunk->code, oldCap, chunk->capacity);
    chunk->lines = GROW_ARRAY(vm, int, chunk->lines, oldCap, chunk->capacity);
  }

  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}
