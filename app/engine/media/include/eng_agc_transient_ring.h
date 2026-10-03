#ifndef ENG_AGC_TRANSIENT_RING_H
#define ENG_AGC_TRANSIENT_RING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ENG_AGC_TRANSIENT_MAX_SLOTS = 3,
};

enum eng_agc_transient_slot_state {
    ENG_AGC_TRANSIENT_EMPTY = 0,
    ENG_AGC_TRANSIENT_OPEN = 1,
    ENG_AGC_TRANSIENT_SEALED = 2,
};

typedef struct eng_agc_transient_slice {
    void    *cpu;
    uint64_t gpu_addr;
    size_t   offset;
    size_t   bytes;
} eng_agc_transient_slice_t;

typedef struct eng_agc_transient_slot {
    size_t   offset;
    size_t   bytes;
    size_t   used;
    uint64_t retire_token;
    uint8_t  state;
} eng_agc_transient_slot_t;

typedef struct eng_agc_transient_ring {
    uint8_t                  *base;
    uint64_t                  gpu_base;
    size_t                    bytes;
    uint32_t                  slot_count;
    eng_agc_transient_slot_t  slots[ENG_AGC_TRANSIENT_MAX_SLOTS];
} eng_agc_transient_ring_t;

enum eng_agc_transient_result {
    ENG_AGC_TRANSIENT_OK = 0,
    ENG_AGC_TRANSIENT_PRECONDITION = -1,
    ENG_AGC_TRANSIENT_SLOT_BUSY = -2,
    ENG_AGC_TRANSIENT_TOKEN_MISMATCH = -3,
    ENG_AGC_TRANSIENT_EXHAUSTED = -4,
};

int eng_agc_transient_ring_init(eng_agc_transient_ring_t *ring, void *base,
                                uint64_t gpu_base, size_t bytes,
                                uint32_t slot_count, size_t slot_alignment);

int eng_agc_transient_ring_begin(eng_agc_transient_ring_t *ring, uint32_t slot_index,
                                 uint64_t completed_token, int completion_proven);

int eng_agc_transient_ring_alloc(eng_agc_transient_ring_t *ring, uint32_t slot_index,
                                 size_t bytes, size_t alignment,
                                 eng_agc_transient_slice_t *slice);

int eng_agc_transient_ring_seal(eng_agc_transient_ring_t *ring, uint32_t slot_index,
                                uint64_t retire_token);

int eng_agc_transient_ring_abort(eng_agc_transient_ring_t *ring, uint32_t slot_index);

size_t eng_agc_transient_ring_used(const eng_agc_transient_ring_t *ring, uint32_t slot_index);

#ifdef __cplusplus
}
#endif

#endif /* ENG_AGC_TRANSIENT_RING_H */
