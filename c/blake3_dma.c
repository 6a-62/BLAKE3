#include "blake3_impl.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>
#include <signal.h>
#include <sched.h>

#define TX_SIZE 512/(sizeof(unsigned int))
#define RX_SIZE 256/(sizeof(unsigned int))

static volatile int tx_wait = 1;
static pthread_t tx_tid;

void *tx_thread(void *unused) {
  	unsigned int buffer_id = 0;
	
	// Wait for RX channels to start
	while (tx_wait);

	// Start TX channel transfer
	txd[buffer_id].length = TX_SIZE;
	ioctl(tx_fd, START_XFER, &buffer_id);

	// Finish TX channel transfer
	ioctl(tx_fd, FINISH_XFER, &buffer_id);
	assert(txd[buffer_id].status == PROXY_NO_ERROR);
}

void tx_setup() {
	pthread_attr_t attr;
	struct sched_param param;
	const int priority = 20;

	// Set TX thread to low priority
	pthread_attr_init (&attr);
	pthread_attr_getschedparam (&attr, &param);
	param.sched_priority = priority;
	pthread_attr_setschedparam (&attr, &param);
	// Create TX thread
	pthread_create(&tx_tid, &attr, tx_thread, NULL);

	// Set RX thread to max priority
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
}

void blake3_compress_in_place_dma(uint32_t cv[8],
                                       const uint8_t block[BLAKE3_BLOCK_LEN],
                                       uint8_t block_len, uint64_t counter,
                                       uint8_t flags) {
  int buffer_id = 0;

  // Write input data
  // Chain
  for (int i=0; i<8; i++) {
    txd[buffer_id].buffer[i] = cv[i];
  }
  // Message Block
  for (int i=0; i<16; i++) {
    txd[buffer_id].buffer[i+8] = load32(block + 4 * i);
  }
  // Block Counter
  txd[buffer_id].buffer[24] = counter_low(counter);
  txd[buffer_id].buffer[25] = counter_high(counter);
  // Number of Bytes
  txd[buffer_id].buffer[26] = (uint32_t)block_len;
  txd[buffer_id].buffer[27] = (uint32_t)flags;

  // Setup transfer thread, wait for start signal
  tx_wait = 1;
  tx_setup();

  // Start receive channel
  rxd[buffer_id].length = (256/sizeof(unsigned int));
  ioctl(rx_fd, START_XFER, &buffer_id);
  // Start transfer thread signal
  tx_wait = 0;

  // Finish RX channel transfer
  ioctl(rx_fd, FINISH_XFER, &buffer_id);
  assert(rxd[buffer_id].status == PROXY_NO_ERROR);

  // Wait for transfer thread to finish
  pthread_join(tx_tid, NULL);

  // Read Hash
  for (int i=0; i<8; i++) {
    cv[i] = rxd[buffer_id].buffer[i];
  }
}

void hash_one_dma(const uint8_t *input, size_t blocks,
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
    blake3_compress_in_place_dma(cv, input, BLAKE3_BLOCK_LEN, counter,
                                      block_flags);
    input = &input[BLAKE3_BLOCK_LEN];
    blocks -= 1;
    block_flags = flags;
  }
  store_cv_words(out, cv);
}

void blake3_hash_many_dma(const uint8_t *const *inputs, size_t num_inputs,
                               size_t blocks, const uint32_t key[8],
                               uint64_t counter, bool increment_counter,
                               uint8_t flags, uint8_t flags_start,
                               uint8_t flags_end, uint8_t *out) {
  while (num_inputs > 0) {
    hash_one_dma(inputs[0], blocks, key, counter, flags, flags_start,
                      flags_end, out);
    if (increment_counter) {
      counter += 1;
    }
    inputs += 1;
    num_inputs -= 1;
    out = &out[BLAKE3_OUT_LEN];
  }
}
