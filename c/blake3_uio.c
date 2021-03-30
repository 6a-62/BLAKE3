#include "blake3_impl.h"
#include <string.h>

void blake3_compress_in_place_uio(uint32_t cv[8],
                                       const uint8_t block[BLAKE3_BLOCK_LEN],
                                       uint8_t block_len, uint64_t counter,
                                       uint8_t flags) {
  // Chain
  for (int i=0; i<8; i++) {
    uiod[i] = cv[i];
  }
  // Message Block
  for (int i=0; i<16; i++) {
    uiod[i+8] = load32(block + 4 * i);
  }
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
  for (int i=0; i<8; i++) {
    cv[i] = uiod[29+i];
  }
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
  while (num_inputs > 0) {
    hash_one_uio(inputs[0], blocks, key, counter, flags, flags_start,
                      flags_end, out);
    if (increment_counter) {
      counter += 1;
    }
    inputs += 1;
    num_inputs -= 1;
    out = &out[BLAKE3_OUT_LEN];
  }
}
