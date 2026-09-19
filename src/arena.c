#include <stdio.h>
#include <stdlib.h>
#include "arena.h"

/**
 * 🏗️ Initializes a new arena memory pool.
 * 
 * @param capacity Total number of bytes to allocate for the pool.
 * @return Pointer to the initialized arena_t, or NULL if allocation fails.
 */
arena_t *arena_init(size_t capacity) {
    // 1. Allocate memory for the arena handle structure
    arena_t *mem_space = malloc(sizeof(arena_t));
    if (!mem_space) return NULL;

    mem_space->capacity = capacity;

    // 2. Allocate the actual contiguous byte buffer
    mem_space->buffer = malloc(capacity);
    if (!mem_space->buffer) {
        // Clean up the handle to prevent a memory leak if buffer allocation fails 🧹
        free(mem_space);
        return NULL;
    }

    // 3. Set the initial allocation cursor to the start of the buffer
    mem_space->offset = 0;
    return mem_space;
}

/**
 * 📦 Allocates a block of memory from the arena.
 * 
 * @param arena Pointer to the arena allocator.
 * @param size Number of bytes requested.
 * @return Pointer to the start of the allocated block, or NULL if insufficient space.
 */
void *arena_alloc(arena_t *arena, size_t size) {
    // Safety check for NULL arena pointer 🛡️
    if (!arena) return NULL;

    // Overflow-safe check: verifies remaining capacity without adding (offset + size)
    if (arena->capacity - arena->offset < size) {
        return NULL; // Not enough space left 🛑
    }

    // Calculate pointer to the start of unallocated space 📍
    void *return_addr = arena->buffer + arena->offset;

    // Advance the cursor forward by the allocated byte size ↗️
    arena->offset += size;

    return return_addr;
}

/**
 * 🔄 Resets the arena, making all memory available for reuse without freeing the buffer.
 * 
 * @param arena Pointer to the arena allocator.
 */
void arena_reset(arena_t *arena) {
    if (!arena) return;

    // Moving the cursor back to 0 instantly invalidates all previous allocations in O(1) time
    arena->offset = 0; 
}

/**
 * 🧹 Frees the underlying buffer and the arena handle itself.
 * 
 * @param arena Pointer to the arena allocator.
 */
void arena_free(arena_t *arena) {
    if (!arena) return;

    // Buffer must be freed first before destroying the arena structure reference
    free(arena->buffer);
    free(arena);
}