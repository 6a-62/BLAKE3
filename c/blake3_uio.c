#include "blake3_impl.h"
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>

#define MAP_SIZE 0x1000
volatile unsigned *uiod;

void blake3_compress_in_place_uio(uint32_t cv[8],
                                       const uint8_t block[BLAKE3_BLOCK_LEN],
                                       uint8_t block_len, uint64_t counter,
                                       uint8_t flags) {
  // Chain
  uiod[0] = cv[0];
  uiod[1] = cv[1];
  uiod[2] = cv[2];
  uiod[3] = cv[3];
  uiod[4] = cv[4];
  uiod[5] = cv[5];
  uiod[6] = cv[6];
  uiod[7] = cv[7];
  // Message Block
  uiod[8]  = load32(block + 4 * 0);
  uiod[9]  = load32(block + 4 * 1);
  uiod[10] = load32(block + 4 * 2);
  uiod[11] = load32(block + 4 * 3);
  uiod[12] = load32(block + 4 * 4);
  uiod[13] = load32(block + 4 * 5);
  uiod[14] = load32(block + 4 * 6);
  uiod[15] = load32(block + 4 * 7);
  uiod[16] = load32(block + 4 * 8);
  uiod[17] = load32(block + 4 * 9);
  uiod[18] = load32(block + 4 * 10);
  uiod[19] = load32(block + 4 * 11);
  uiod[20] = load32(block + 4 * 12);
  uiod[21] = load32(block + 4 * 13);
  uiod[22] = load32(block + 4 * 14);
  uiod[23] = load32(block + 4 * 15);
  // Block Counter
  uiod[24] = counter_low(counter);
  uiod[25] = counter_high(counter);
  // Number of Bytes
  uiod[26] = (uint32_t)block_len;
  uiod[27] = (uint32_t)flags;
  // Input Ready
  uiod[28] = 1;
  uiod[28] = 0;

  // Wait for Output Ready
  unsigned done = 0;
  while (done == 0) {
    done = uiod[46];
  }
  
  // Read Hash
  cv[0] = uiod[29];
  cv[1] = uiod[30];
  cv[2] = uiod[31];
  cv[3] = uiod[32];
  cv[4] = uiod[33];
  cv[5] = uiod[34];
  cv[6] = uiod[35];
  cv[7] = uiod[36];
}

INLINE void hash_one_uio(const uint8_t *input, size_t blocks,
                              const uint32_t key[8], uint64_t counter,
                              uint8_t flags, uint8_t flags_start,
                              uint8_t flags_end, uint8_t out[BLAKE3_OUT_LEN]) {
  uint32_t cv[8];
  memcpy(cv, key, BLAKE3_KEY_LEN);
  uint8_t block_flags = flags | flags_start;
  while (blocks > 0) {
    if (blocks == 1) {
      block_flags |= flags_end;
    }
    blake3_compress_in_place_uio(cv, input, BLAKE3_BLOCK_LEN, counter,
                                      block_flags);
    input = &input[BLAKE3_BLOCK_LEN];
    blocks -= 1;
    block_flags = flags;
  }
  store_cv_words(out, cv);
}

void blake3_hash_many_uio(const uint8_t *const *inputs, size_t num_inputs,
                               size_t blocks, const uint32_t key[8],
                               uint64_t counter, bool increment_counter,
                               uint8_t flags, uint8_t flags_start,
                               uint8_t flags_end, uint8_t *out) {
  // Open UIO device
  uiod = (volatile unsigned *) mmap(NULL, MAP_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

  while (num_inputs > 0) {
    hash_one_portable(inputs[0], blocks, key, counter, flags, flags_start,
                      flags_end, out);
    if (increment_counter) {
      counter += 1;
    }
    inputs += 1;
    num_inputs -= 1;
    out = &out[BLAKE3_OUT_LEN];
  }
  
  munmap((void*)uiod, MAP_SIZE);
}
