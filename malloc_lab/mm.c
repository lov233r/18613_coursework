/**
 * @file mm.c
 * @brief A 64-bit struct-based implicit free list memory allocator
 *
 * 15-213: Introduction to Computer Systems
 *
 * TODO: insert your documentation here. :)
 *
 *************************************************************************
 *
 * ADVICE FOR STUDENTS.
 * - Step 0: Please read the writeup!
 * - Step 1: Write your heap checker.
 * - Step 2: Write contracts / debugging assert statements.
 * - Good luck, and have fun!
 *
 *************************************************************************
 *
 * @author Ziyue Huang <ziyuehua@andrew.cmu.edu>
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/* Do not change the following! */

#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* def DRIVER */

/* You can change anything from here onward */

/*
 *****************************************************************************
 * If DEBUG is defined (such as when running mdriver-dbg), these macros      *
 * are enabled. You can use them to print debugging output and to check      *
 * contracts only in debug mode.                                             *
 *                                                                           *
 * Only debugging macros with names beginning "dbg_" are allowed.            *
 * You may not define any other macros having arguments.                     *
 *****************************************************************************
 */
// #ifndef DEBUG
// #define DEBUG
// #endif

#ifdef DEBUG
/* When DEBUG is defined, these form aliases to useful functions */
#define dbg_requires(expr) assert(expr)
#define dbg_assert(expr) assert(expr)
#define dbg_ensures(expr) assert(expr)
#define dbg_printf(...) ((void)printf(__VA_ARGS__))
#define dbg_printheap(...) print_heap(__VA_ARGS__)
#else
/* When DEBUG is not defined, these should emit no code whatsoever,
 * not even from evaluation of argument expressions.  However,
 * argument expressions should still be syntax-checked and should
 * count as uses of any variables involved.  This used to use a
 * straightforward hack involving sizeof(), but that can sometimes
 * provoke warnings about misuse of sizeof().  I _hope_ that this
 * newer, less straightforward hack will be more robust.
 * Hat tip to Stack Overflow poster chqrlie (see
 * https://stackoverflow.com/questions/72647780).
 */
#define dbg_discard_expr_(...) ((void)((0) && printf(__VA_ARGS__)))
#define dbg_requires(expr) dbg_discard_expr_("%d", !(expr))
#define dbg_assert(expr) dbg_discard_expr_("%d", !(expr))
#define dbg_ensures(expr) dbg_discard_expr_("%d", !(expr))
#define dbg_printf(...) dbg_discard_expr_(__VA_ARGS__)
#define dbg_printheap(...) ((void)((0) && print_heap(__VA_ARGS__)))
#endif

/* Basic constants */
// Unsigned 64 bits integer
typedef uint64_t word_t;

#define NUM_SEGLISTS 10

/** @brief Word and header size (bytes) */
static const size_t wsize = sizeof(word_t);

/** @brief Double word size (bytes) */
static const size_t dsize = 2 * wsize;

/** @brief Minimum block size (bytes) */
static const size_t min_block_size = dsize;

/**
 * TODO: when extend the size of heap, extend chunk size
 * Reduce system call, aligned with system's page size
 * Why divisible by dsize: alignment requirement
 */
static const size_t chunksize = (1 << 12);

/**
 * TODO: To know whether the block is allocated or freed
 */
static const word_t next_alloc_mask = 0x2;
static const word_t cur_alloc_mask = 0x1;
static const word_t prev_alloc_mask = 0x4;
static const word_t tiny_mask = 0x8;

/**
 * TODO: To retrieve the size of the block
 */
static const word_t size_mask = ~(word_t)0xF;

/*
 * ---------------------------------------------------------------------------
 *                        BEGIN LINKED LIST AND MANIPULATION
 * ---------------------------------------------------------------------------
 */

/** @brief Represents the header and payload of one block in the heap */
typedef union block {
    // Regular block
    struct {
        word_t header;
        union {
            struct free_block_ele { // 16 bytes
                union block *prev_free;
                union block *next_free;
            } free_ptrs;
            char payload[0];
        } info;
    } regular;

    // Tiny block
    struct {
        word_t header; // REUSE HEADER SPACE
        union {
            union block *next_free;
            char payload[0];
        } info;
    } tiny;
    
} block_t;

/*  Start Global Variables */

/** @brief Pointer to first block in the heap */
static block_t *heap_start = NULL;

/** @brief Pointer to the last block of the heap */
static block_t *last_block = NULL;

// Array of segregated free lists
block_t *seglist[NUM_SEGLISTS] = {NULL};

/*  End Global Variables */

static word_t *find_prev_footer(block_t *block);
static size_t get_size(block_t *block);
static bool extract_tiny_alloc(word_t word);
static bool get_tiny_alloc(block_t *block);
static block_t *find_next(block_t *block);

static size_t get_seglist_index(size_t size) {
    if (size == 16) // Goes to tiny, singly linked list
        return 0;
    else if (size <= 32)
        return 1;
    else if (size <= 64)
        return 2;
    else if (size <= 128)
        return 3;
    else if (size <= 256)
        return 4;
    else if (size <= 512)
        return 5;
    else if (size <= 1024)
        return 6;
    else if (size <= 2048)
        return 7;
    else if (size <= 4096)
        return 8;
    else
        return 9;
}

// Add a block to the appropriate segregated list
static void add_block_to_seglist(block_t *block) {
    size_t size = get_size(block);
    size_t index = get_seglist_index(size);
    block_t *head = seglist[index];

    if (index == 0) { // Tiny block
        dbg_printf("add_block_tiny: Add tiny to seglist, address: %p\n", (void *)block);
        word_t cur_status_bits = block->tiny.header&0xF;
        if (head == NULL) { // Tiny list is empty
            dbg_printf("add_block_tiny: Current list is empty\n");
            block->tiny.header = cur_status_bits;
            block->tiny.info.next_free = (block_t *)(cur_status_bits);
            seglist[index] = block;
        } else { // Tiny list is not empty
            // Update pointers of current block
            dbg_printf("add_block_tiny: Current list NOT empty\n");
            dbg_printf("add_block_tiny: Current head address is: %p\n", (void *)head);
            block->tiny.info.next_free =
                (block_t *)(((size_t)head&size_mask) + cur_status_bits);
            block->tiny.header = cur_status_bits;

            // Update pointers of previous head's header
            word_t header_status_bit = head->tiny.header&0xF;
            head->tiny.header = (word_t)(block) + header_status_bit - wsize;

            seglist[index] = block;
            // dbg_printf("add_block_tiny: size of the next block in heap: %zu\n", get_size(find_next(block)));
            dbg_printf("add_block_tiny: End of add block\n");
        }
    } else {
        dbg_printf("add_block: Add regular to seglist, index: %zu\n", index);
        if (head == NULL) { // Regular block
            dbg_printf("add_block: Current list is empty\n");
            seglist[index] = block;
            block->regular.info.free_ptrs.next_free = NULL;
            block->regular.info.free_ptrs.prev_free = NULL;
        } else if (head != block) {
            dbg_printf("add_block: Current list is NOT empty\n");
            block->regular.info.free_ptrs.next_free = head;
            block->regular.info.free_ptrs.prev_free = NULL;
            seglist[index]->regular.info.free_ptrs.prev_free = block;
            seglist[index] = block;
        }
        dbg_printf("add_block: End of add block\n");
    }
    dbg_requires(mm_checkheap(__LINE__));
}

// Remove a block from the segregated list
static void remove_block_from_seglist(block_t *block) {
    size_t size = get_size(block);
    size_t index = get_seglist_index(size);
    // Status bits of current block
    // Initialize pointers to previous&next block
    block_t *prev_block = NULL;
    block_t *next_block = NULL;

    // Handle spurious situation
    if (block == NULL) {
        return;
    }
    
    dbg_printf("remove: Address of current block: %p\n", (void *)block);
    dbg_printf("remove: header of current block: %zu\n", block->tiny.header);
    dbg_printf("remove: size of current block: %zu\n", size);

    // Tiny block, update address only (not status bits)
    if (get_tiny_alloc(block)) {
        prev_block = (block_t *)(block->tiny.header&size_mask);
        next_block = (block_t *)((size_t)(block->tiny.info.next_free)&size_mask);
        // If not NULL, +wsize to get correct address
        if (prev_block != NULL) {
            prev_block = (block_t *)((size_t)prev_block + wsize);
        }
        if (next_block != NULL) {
            next_block = (block_t *)((size_t)next_block + wsize);
        }
        dbg_printf("remove_tiny: Address of next block: %p\n", (void *)next_block);
        dbg_printf("remove_tiny: Address of previous block: %p\n", (void *)prev_block);
        word_t next_status;
        word_t prev_status;
        //word_t status_bits = block->tiny.header&(size_t)0xF;
        
        // Delete node in between two nodes
        if (prev_block != NULL && next_block != NULL) {
            dbg_printf("remove_tiny: Delete node in between two nodes\n");
            next_status = next_block->tiny.header&0xF;
            prev_status = prev_block->tiny.header&0xF;
            prev_block->tiny.info.next_free = (block_t *)((size_t)next_block + prev_status - wsize);
            next_block->tiny.header = (size_t)(prev_block) + next_status - wsize;
        }

        // Delete only node of the list
        if (prev_block == NULL && next_block == NULL) {
            dbg_printf("remove_tiny: Delete only node of the list\n");
            seglist[index] = NULL;
        }

        // Delete the last node
        if (prev_block != NULL && next_block == NULL) {
            dbg_printf("remove_tiny: Delete the last node\n");
            prev_status = prev_block->tiny.header&0xF;
            prev_block->tiny.info.next_free = (block_t *)(prev_status);
        }

        // Delete head node
        if (prev_block == NULL && next_block != NULL) {
            dbg_printf("remove_tiny: Delete the head node\n");
            next_status = next_block->tiny.header&0xF;
            next_block->tiny.header = next_status;
            seglist[index] = next_block;
        }

        // Set current block's ptrs
        block->tiny.header &= 0xF;
        block->tiny.info.next_free = (block_t *)block->tiny.header;
        dbg_printf("remove tiny end\n");
        return;
    }

    // Regular block
    dbg_printf("remove_reg: Remove regular block from seglist\n");
    prev_block = block->regular.info.free_ptrs.prev_free;
    next_block = block->regular.info.free_ptrs.next_free;
    dbg_printf("remove_reg: regular address of previous block: %p\n", (void *)prev_block);
    dbg_printf("remove_reg: regular address of next block: %p\n", (void *)next_block);

    // Delete node in between two nodes
    if (prev_block != NULL && next_block != NULL) {
        prev_block->regular.info.free_ptrs.next_free = next_block;
        next_block->regular.info.free_ptrs.prev_free = prev_block;
        block->regular.info.free_ptrs.next_free = NULL;
        block->regular.info.free_ptrs.prev_free = NULL;
    }

    // Delete only node of the list
    if (prev_block == NULL && next_block == NULL) {
        seglist[index] = NULL;
        block->regular.info.free_ptrs.next_free = NULL;
        block->regular.info.free_ptrs.prev_free = NULL;
    }

    // Delete the last node
    if (prev_block != NULL && next_block == NULL) {
        prev_block->regular.info.free_ptrs.next_free = NULL;
        block->regular.info.free_ptrs.next_free = NULL;
        block->regular.info.free_ptrs.prev_free = NULL;
    }

    // Delete head node
    if (prev_block == NULL && next_block != NULL) {
        seglist[index] = next_block;
        next_block->regular.info.free_ptrs.prev_free = NULL;
        block->regular.info.free_ptrs.next_free = NULL;
        block->regular.info.free_ptrs.prev_free = NULL;
    }
    dbg_printf("remove regular end\n");
    dbg_requires(mm_checkheap(__LINE__));
}

/*
 * ---------------------------------------------------------------------------
 *                        END LINKED LIST AND MANIPULATION
 * ---------------------------------------------------------------------------
 */

/*
 *****************************************************************************
 * The functions below are short wrapper functions to perform                *
 * bit manipulation, pointer arithmetic, and other helper operations.        *
 *                                                                           *
 * We've given you the function header comments for the functions below      *
 * to help you understand how this baseline code works.                      *
 *                                                                           *
 * Note that these function header comments are short since the functions    *
 * they are describing are short as well; you will need to provide           *
 * adequate details for the functions that you write yourself!               *
 *****************************************************************************
 */

/*
 * ---------------------------------------------------------------------------
 *                        BEGIN SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/**
 * @brief Returns the maximum of two integers.
 * @param[in] x
 * @param[in] y
 * @return `x` if `x > y`, and `y` otherwise.
 */
static size_t max(size_t x, size_t y) {
    return (x > y) ? x : y;
}

/**
 * @brief Rounds `size` up to next multiple of n
 * @param[in] size
 * @param[in] n
 * @return The size after rounding up
 */
static size_t round_up(size_t size, size_t n) {
    return n * ((size + (n - 1)) / n);
}

/**
 * @brief Packs the `size` and `alloc` of a block into a word suitable for
 *        use as a packed value.
 *
 * Packed values are used for both headers and footers.
 *
 * The allocation status is packed into the lowest bit of the word.
 *
 * @param[in] size The size of the block being represented
 * @param[in] cur_alloc True if the block is allocated
 * @return The packed value
 */
static word_t pack(size_t size, bool cur_alloc) {
    word_t word = size;
    if (cur_alloc) {
        word |= cur_alloc_mask;
    }
    return word;
}

/**
 * @brief Packs the `size` and three `alloc` of a block into a word.
 *
 * Packed values are used for both headers and footers.
 * The allocation status is packed into the lowest 3 bit of the word.
 *
 * @param[in] size The size of the block being represented
 * @param[in] alloc_pre True if the previous block is allocated
 * @param[in] alloc_cur True if the current block is allocated
 * @param[in] alloc_next True if the next block is allocated
 * @param[in] alloc_tiny True if the current block is tiny
 * @return The packed value
 */
static word_t pack_all(size_t size, bool alloc_pre, bool alloc_cur,
                       bool alloc_next, bool alloc_tiny) {
    word_t word = size;
    if (alloc_pre) {
        word |= prev_alloc_mask;
    }
    if (alloc_cur) {
        word |= cur_alloc_mask;
    }
    if (alloc_next) {
        word |= next_alloc_mask;
    }
    if (alloc_tiny) {
        word |= tiny_mask;
    }
    return word;
}

/**
 * @brief Extracts the size represented in a packed word.
 *
 * This function simply clears the lowest 4 bits of the word, as the heap
 * is 16-byte aligned. (The last 4 digits doesn't matter)
 *
 * @param[in] word
 * @return The size of the block represented by the word
 */
static size_t extract_size(word_t word) {
    return (word & size_mask);
}

/**
 * @brief Extracts the size of a block from its header.
 * @param[in] block
 * @return The size of the block
 */
static size_t get_size(block_t *block) {
    if (block == NULL) {
        return (size_t)NULL;
    }
    if (get_tiny_alloc(block)) {
        return dsize;
    } else {
        return extract_size(block->regular.header);
    }
    
}

/**
 * @brief Given a payload pointer, returns a pointer to the corresponding
 *        block.
 * @param[in] bp A pointer to a block's payload
 * @return The corresponding block
 */
static block_t *payload_to_header(void *bp) {
    // 'offsetof' returns the number of bytes from the start of block_t to
    // payload
    return (block_t *)((char *)bp - offsetof(block_t, regular.info.payload));
}

/**
 * @brief Given a block pointer, returns a pointer to the corresponding
 *        payload.
 * @param[in] block
 * @return A pointer to the block's payload
 * @pre The block must be a valid block, not a boundary tag.
 */
static void *header_to_payload(block_t *block) {
    dbg_requires(get_size(block) != 0);
    if (get_tiny_alloc(block)) {
        return (void *)(block->tiny.info.payload);
    } else {
        return (void *)(block->regular.info.payload);
    }
}

/**
 * @brief Given a block pointer, returns a pointer to the corresponding
 *        footer.
 * @param[in] block
 * @return A pointer to the block's footer
 * @pre The block must be a valid block, not a boundary tag.
 */
static word_t *header_to_footer(block_t *block) {
    dbg_requires(get_size(block) != 0 &&
                 "Called header_to_footer on the epilogue block");
    if (get_tiny_alloc(block)) {
        return (word_t *)(block->tiny.info.payload);
    } else {
        return (word_t *)(block->regular.info.payload + get_size(block) - dsize);
    }
}

/**
 * @brief Given a block footer, returns a pointer to the corresponding
 *        header.
 * @param[in] footer A pointer to the block's footer
 * @return A pointer to the start of the block
 * @pre The footer must be the footer of a valid block, not a boundary tag.
 */
static block_t *footer_to_header(word_t *footer) {
    size_t size;
    
    if (extract_tiny_alloc(*footer)) {
        size = dsize;
    } else {
        size = extract_size(*footer);
    }    
    return (block_t *)((char *)footer + wsize - size);
}

/**
 * @brief Returns the payload size of a given block.
 *
 * The payload size is equal to the entire block size minus the sizes of the
 * block's header and footer.
 *
 * @param[in] block
 * @return The size of the block's payload
 */
static size_t get_payload_size(block_t *block) {
    size_t asize = get_size(block);
    return asize - wsize;
}

/**
 * @brief Returns the allocation status of a given header value.
 *
 * This is based on the second to last bit of the header value.
 *
 * @param[in] word
 * @return The allocation status correpsonding to the word
 */
static bool extract_cur_alloc(word_t word) {
    return (word & cur_alloc_mask) == cur_alloc_mask;
}

/**
 * @brief Returns the prev allocation status of a given header value.
 *
 * This is based on the third to last bit of the header value.
 *
 * @param[in] word
 * @return The allocation status correpsonding to the word
 */
static bool extract_pre_alloc(word_t word) {
    return (word & prev_alloc_mask) == prev_alloc_mask;
}

/**
 * @brief Returns the next allocation status of a given header value.
 *
 * This is based on the last bit of the header value.
 *
 * @param[in] word
 * @return The allocation status correpsonding to the word
 */
static bool extract_next_alloc(word_t word) {
    return (word & next_alloc_mask) == next_alloc_mask;
}

/**
 * @brief Returns tiny block status of a given header value.
 *
 * This is based on the last to fourth bit of the header value.
 *
 * @param[in] word
 * @return The allocation status correpsonding to the word
 */
static bool extract_tiny_alloc(word_t word) {
    return (word & tiny_mask) == tiny_mask;
}


/**
 * @brief Returns the allocation status of a block, based on its header.
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_alloc(block_t *block) {
    return extract_cur_alloc(block->regular.header);
}

/**
 * @brief Returns the allocation status of prev block, based on
 * current block's header.
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_pre_alloc(block_t *block) {
    return extract_pre_alloc(block->regular.header);
}

/**
 * @brief Returns the allocation status of next block, based on
 * current block's header.
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_next_alloc(block_t *block) {
    return extract_next_alloc(block->regular.header);
}

/**
 * @brief Returns the tiny status of current block, based on
 * current block's header.
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_tiny_alloc(block_t *block) {
    if ((block->regular.header&tiny_mask) == tiny_mask) {
        return true;
    } else {
        return false;
    }
}

/**
 * @brief Find next consecutive block on heap based on current header.
 *
 * This function accesses the next block in the "implicit list" of the heap
 * by adding the size of the block.
 *
 * @param[in] block A block in the heap
 * @return The next consecutive block on the heap
 * @pre The block is not the epilogue
 */
static block_t *find_next(block_t *block) {
    dbg_requires(block != NULL);

    if (get_tiny_alloc(block)) {
        return (block_t *)((char *)block + dsize);
    } else {
        return (block_t *)((char *)block + get_size(block));
    }
}

/**
 * @brief Finds the previous consecutive block on the heap.
 * Based on current header
 *
 * @param[in] block A block in the heap
 * @return The previous free block in the heap, NULL if allocated
 */
static block_t *find_prev(block_t *block) {
    dbg_requires(block != NULL);
    block_t *prev_block = NULL;
    bool pre_alloc = NULL;

    pre_alloc = extract_pre_alloc(block->regular.header);

    if (!pre_alloc) {
        word_t *footer = find_prev_footer(block);
        prev_block = footer_to_header(footer);
    }

    return prev_block;
}

/*
 * ---------------------------------------------------------------------------
 *                        BEGIN WRITE FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/**
 * @brief Given a header, set its next bit
 */
static void set_next_status(size_t *header, bool status) {
    if (status) {
        *header |= next_alloc_mask; // Set next bit to 1
    } else {
        *header &= ~next_alloc_mask; // Clear next bit to 0
    }
}

/**
 * @brief Given a pointer to header, set its previous bit
 */
static void set_prev_status(size_t *header, bool status) {
    if (status) {
        *header |= prev_alloc_mask; // Set previous bit to 1
    } else {
        *header &= ~prev_alloc_mask; // Clear previous bit to 0
    }
}

/**
 * @brief Given a header, set its current bit
 */
static void set_cur_status(size_t *header, bool status) {
    if (status) {
        *header |= cur_alloc_mask; // Set current bit to 1
    } else {
        *header &= ~cur_alloc_mask; // Clear current bit to 0
    }
}

/**
 * @brief Given a header, set its tiny bit
 */
static void set_tiny_status(size_t *header, bool status) {
    if (status) {
        *header |= tiny_mask; // Set tiny bit to 1
    } else {
        *header &= ~tiny_mask; // Clear tiny bit to 0
    }
}

/**
 * @brief Writes an epilogue header at the given address.
 *
 * The epilogue header has size 0, and is marked as allocated.
 *
 * @param[out] block The location to write the epilogue header
 */
static void write_epilogue(block_t *block) {
    dbg_requires(block != NULL);
    dbg_requires((char *)block == (char *)mem_heap_hi() - 7);
    block->regular.header = pack_all(0, false, true, true, false);
}

/**
 * @brief Writes a block starting at the given address.
 *
 * Write current block, updates next block's header/footer.
 *
 * @param[out] block The location to begin writing the block header
 * @param[in] size The size of the new block
 * @param[in] alloc The allocation status of the new block
 */
static void write_block(block_t *block, size_t size, bool pre_alloc,
                        bool cur_alloc, bool tiny_alloc) {
    dbg_requires(block != NULL);
    dbg_requires(size > 0);

    bool next_alloc = true;
    // Set tiny bit first, then find next block
    dbg_printf("WRITE_BLOCK START\n");
    set_tiny_status(&block->tiny.header, tiny_alloc);
    block_t *next_block = find_next(block);

    if (tiny_alloc) { // If the block is tiny, NO NEED TO USE SIZE
        // UPDATE ONLY STATUS BITS
        dbg_printf("write_block_tiny: write tiny block\n");
        dbg_printf("write_block_tiny: block header before write: %zu\n", block->tiny.header);
        dbg_printf("write_block_tiny: block footer before write: %zu\n", *header_to_footer(block));
        word_t alloc_status = pack_all(0, pre_alloc, cur_alloc, next_alloc, tiny_alloc);
        block->tiny.header = alloc_status;

        if (!get_alloc(next_block)) {
            next_alloc = false;
        }

        // 1. Update current block
        alloc_status = pack_all(0, pre_alloc, cur_alloc, next_alloc, tiny_alloc);
        block->tiny.header = alloc_status;
        // If current block is free, update free block struct
        if (!cur_alloc) {
            word_t *footer = header_to_footer(block);
            *footer &= alloc_status;
        }

        // 2. Update next header's previous bit only
        set_prev_status(&next_block->regular.header, cur_alloc);

        // When next block is not epilogue, proceed
        bool is_epi = false;

        if (!get_tiny_alloc(next_block) && (get_size(next_block) == 0)) {
            is_epi = true;
        }

        if (!is_epi) { // When it is not epilogue
            if (!next_alloc) { // If free, update footer
                word_t *next_footer = header_to_footer(next_block);
                set_prev_status(next_footer, cur_alloc);
            }
        }
        dbg_printf("write_block_tiny: block header after write: %zu\n", block->tiny.header);
        dbg_printf("write_block_tiny: block footer after write: %zu\n", *header_to_footer(block));


    } else {
        dbg_printf("write_block: write regular block\n");
        // take input to initialize block
        block->regular.header = pack_all(size, pre_alloc, cur_alloc, next_alloc, false);
        next_block = find_next(block);
        // Get alloc status of next block
        if (!get_alloc(next_block)) {
            next_alloc = false;
        }

        // 1. Update current block
        block->regular.header = pack_all(size, pre_alloc, cur_alloc, next_alloc, false);
        // If current block is free, update free block struct
        if (!cur_alloc) {
            word_t *footer = header_to_footer(block);
            *footer = block->regular.header;
            block->regular.info.free_ptrs.next_free = NULL;
            block->regular.info.free_ptrs.prev_free = NULL;
        }

        // 2. Update next header's previous bit only
        dbg_printf("write_block: before update next block, address of next block: %p\n", (void *)next_block);
        set_prev_status(&next_block->regular.header, cur_alloc);
        if (get_size(next_block) != 0 && next_block != NULL) {
            if (!next_alloc) { // If free, update footer
                word_t *next_footer = header_to_footer(next_block);
                set_prev_status(next_footer, cur_alloc);
            }
        }
    }

    
}

/*
 * ---------------------------------------------------------------------------
 *                        END WRITE FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/**
 * @brief Finds the footer of the previous block on the heap.
 * @param[in] block A block in the heap
 * @return The location of the previous block's footer
 */
static word_t *find_prev_footer(block_t *block) {
    // Compute previous footer position as one word before the header
    return (word_t *)((char *)block - 8);
}

/*
 * ---------------------------------------------------------------------------
 *                        END SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/******** The remaining content below are helper and debug routines ********/

/**
 * @brief
 * Input current free block, coalescing with adjacent blocks if possible.
 *
 * Input must be already added into free list
 *
 * Then add the free block to free list
 *
 * @param[in] block
 * @return coalesced block
 */
static block_t *coalesce_block(block_t *block) {
    bool alloc_pre = false;
    bool alloc_next = false;
    block_t *next_block = find_next(block);
    word_t next_header = next_block->regular.header;
    dbg_printf("START COALESCING\n");

    // Update status of previous block based on current header
    if (get_pre_alloc(block)) {
        alloc_pre = true;
        dbg_printf("coalescing: previous block is allocated\n");
    } else {
        dbg_printf("coalescing: previous block is free\n");
    }

    // Update status of next block based on next header
    if (get_alloc(next_block)) {
        alloc_next = true;
        dbg_printf("coalescing: next block is allocated\n");
    } else {
        dbg_printf("coalescing: next block is free\n");
    }


    // Update last_block
    if ((next_block == last_block) && !alloc_next) {
        if (!alloc_pre) { // Previous block is free
            last_block = find_prev(block);
        } else {
            last_block = block;
        }
    }
    if ((block == last_block) && !alloc_pre) {
        last_block = find_prev(block);
    }

    // If no free block adjacent, return
    if (alloc_next && alloc_pre) {
        dbg_printf("coalescing: no free block adjacent\n");
        remove_block_from_seglist(block);
        write_block(block, get_size(block), true, false, get_tiny_alloc(block));
        dbg_printf("coalescing: before return, block header is: %zu\n", block->tiny.header);
        add_block_to_seglist(block);
        return block;
    }

    // Perform coalescing based on status
    if (!alloc_pre) { // If previous block is free
        word_t *pre_footer = find_prev_footer(block);
        dbg_printf("coalescing: current block is: %p\n", (void *)block);
        dbg_printf("coalescing: previous footer is %zu\n", *pre_footer);
        dbg_printf("coalescing: previous footer's address: %p\n", (void *)find_prev_footer(block));
        size_t pre_size = dsize;
        if (!extract_tiny_alloc(*pre_footer)) { // If prev block is not tiny
            pre_size = extract_size(*pre_footer);
        }
        size_t all_size = pre_size + get_size(block);
        block_t *pre_block = find_prev(block);
        dbg_printf("coalescing: previous block is free, size: %zu\n", pre_size);

        // Remove two separate nodes from free list
        remove_block_from_seglist(pre_block);
        remove_block_from_seglist(block);

        // Write the new block, add to the beginning
        write_block(pre_block, all_size, true, false, false);
        add_block_to_seglist(pre_block);
        block = pre_block;
        dbg_printf("Coalesced with previous block, new block size: %zu\n", get_size(block));
    }

    if (!alloc_next) { // If next block is free
        size_t next_size = dsize;
        next_block = find_next(block);
        next_header = next_block->regular.header;
        // If next block is not tiny
        if (!extract_tiny_alloc(next_header)) { 
            next_size = extract_size(next_header);
        }
        dbg_printf("coalescing: next block is free, size: %zu\n", next_size);

        size_t all_size = next_size + get_size(block);

        // Remove two separate nodes from free list
        remove_block_from_seglist(block);
        remove_block_from_seglist(next_block);

        // Write the new block, add to the beginning
        write_block(block, all_size, true, false, false);
        add_block_to_seglist(block);
        dbg_printf("Coalesced with next block, new block size: %zu\n", get_size(block));
    }
    return block;
}

/**
 * @brief
 * Extend heap with size, add a free block to free list
 *
 * @param[in] size
 * @return A free block already in seglist
 */
static block_t *extend_heap(size_t size) {
    void *bp;
    bool tiny_alloc = false;

    // 1. Get alloc status of last block
    bool prev_alloc = get_alloc(last_block);

    // Allocate an even number of words to maintain alignment
    // Round the size up to 16 bytes' multiple
    size = round_up(size, dsize);
    if ((bp = mem_sbrk((intptr_t)size)) == (void *)-1) {
        return NULL;
    }
    dbg_printf("extend_heap: Heap extended for %zu bytes\n", size);

    // Find start of block via bp
    block_t *block = payload_to_header(bp);
    last_block = block;
    
    // Update status of tiny block
    if (size == dsize) {
        tiny_alloc = true;
    }

    // Initialize block header in order to find epilogue
    block->regular.header = pack_all(size, prev_alloc, false, true, tiny_alloc);
    block_t *block_next = find_next(block);

    // Create new epilogue header: 0011
    write_epilogue(block_next);

    // Update block footer
    *header_to_footer(block) = block->regular.header;

    // Write block and add to seglist
    write_block(block, size, prev_alloc, false, tiny_alloc);
    add_block_to_seglist(block);

    // Coalesce in case the previous block is free
    block = coalesce_block(block);
    dbg_printf("extend_heap: Block size after heap extend and coalesing: %zu\n", get_size(block));
    return block;
}

/**
 * @brief
 * Split free block if its too large
 * Input free block not in seglist
 * @param[in] block
 * @param[in] asize
 * @return allocated block
 */
static block_t *split_block(block_t *block, size_t asize) {
    dbg_requires(!get_alloc(block));
    remove_block_from_seglist(block);
    size_t block_size = get_size(block);
    bool prev_tiny = false;
    bool next_tiny = false;

    dbg_printf("split: Size of current block: %zu \n", block_size);
    dbg_printf("split: asize is: %zu \n", asize);

    // If it is tiny block, no need to split
    if (get_tiny_alloc(block)) {
        bool pre_alloc = get_pre_alloc(block);
        write_block(block, dsize, pre_alloc, true, true);
        dbg_printf("split: Tiny, did not split\n");
        return block;
    }

    if (asize == 16) {
        prev_tiny = true;
    }

    // If remaining space >= min_block_size, split
    if ((block_size - asize) >= min_block_size) {
        // Determine whether the split block is tiny block
        dbg_printf("split: Splited\n");
        if ((block_size - asize) == 16) {
            next_tiny = true;
        }

        // Find next block
        block->regular.header = pack_all(asize, true, true, false, prev_tiny);
        block_t *block_next = find_next(block);

        // Write next block, must be free
        if (next_tiny) { // If it is tiny
            dbg_printf("split: next block is tiny\n");
            block_next->tiny.header = pack_all(0, true, false, false, true);
            write_block(block_next, dsize, true, false, true);
        } else {
            dbg_printf("split: next block is NOT tiny\n");
            block_next->regular.header = pack_all(block_size - asize, true, false, false, false);
            write_block(block_next, block_size - asize, true, false, false);
        }
        add_block_to_seglist(block_next);

        // Write previous block, must be allocated
        if (prev_tiny) { // If it is tiny
            dbg_printf("split: previous block is tiny\n");
            block->tiny.header = pack_all(0, true, true, false, true);
            write_block(block, dsize, true, true, true);
        } else {
            dbg_printf("split: previous block is NOT tiny\n");
            block->tiny.header = pack_all(asize, true, true, false, false);
            write_block(block, asize, true, true, false);
        }

        // Update last block
        if (last_block == block) {
            last_block = block_next;
        }
        return block;
    } else { // Else do not split, mark the whole block as alloc
        dbg_printf("split: Did not split\n");
        write_block(block, block_size, true, true, prev_tiny);
        return block;
    }
    dbg_ensures(get_alloc(block));
}

/**
 * @brief
 * Find the fitted block in seglist
 *
 * @param[in] asize
 * @return A suitable block or NULL if not found
 */
static block_t *find_fit(size_t asize) {
    size_t index = get_seglist_index(asize);
    size_t cur_check = 0;
    size_t max_check = 90;
    size_t min_diff = ~(size_t)0;
    block_t *min_block = NULL;

    for (size_t i = index; i < NUM_SEGLISTS; i++) {
        block_t *current = seglist[i];
        while (current != NULL) {
            if (get_size(current) >= asize) {
                if (get_size(current) == asize) {
                    dbg_printf("find_fit: Best fit found, size: %zu; Address: %p\n", get_size(current), (void *)current);
                    return current;
                } else if (get_size(current) - asize < min_diff) {
                    min_diff = get_size(current);
                    min_block = current;
                }
            }
            cur_check++;
            if (cur_check > max_check) {
                dbg_printf("find_fit: limit reached, size: %zu; Address: %p\n", get_size(min_block), (void *)min_block);
                return min_block;
            }
            if (i == 0) {
                current = (block_t *)((size_t)(current->tiny.info.next_free)&size_mask + wsize);
            } else {
                current = current->regular.info.free_ptrs.next_free;
            }
        }
    }
    dbg_printf("find_fit: whole list searched, size: %zu; Address: %p\n", get_size(min_block), (void *)min_block);
    return min_block;
}

/**
 * @brief
 * Check the heap status, break at the line goes wrong
 *
 * @param[in] line
 * @return
 */
bool mm_checkheap(int line) {
    // Check if the segregated list is consistent, and have no allocated blocks

    // Check if all allocation is inside the heap
    block_t *low_ptr = (block_t *)mem_heap_lo();
    block_t *high_ptr = (block_t *)mem_heap_hi();

    // Check if the heap allocation status is correct
    block_t *block;

    for (block = heap_start; get_size(block) > 0; block = find_next(block)) {
        // Ensure block is within heapbound
        if (block < low_ptr || block > high_ptr) {
            dbg_printf("Block %p is outside heap\n", (void *)block);
            return false;
        }

        if (find_next(block) < low_ptr || find_next(block) > high_ptr) {
            dbg_printf("Block %p is outside heap\n", (void *)find_next(block));
            dbg_printf("Previous address of the outbound block is: %p; size: %zu\n", (void *)block, get_size(block));
            return false;
        }

        // Check for address alignment
        if ((uintptr_t)(block) % dsize != 8) {
            dbg_printf("Block at %p is not 16-byte aligned\n", (void *)block);
            return false;
        }

        // Check if block size is multiple of 16
        size_t size = get_size(block);
        if (size % dsize != 0) {
            dbg_printf("Block size %zu is not a multiple of 16\n", size);
            return false;
        }
        // if (!get_alloc(block) && get_tiny_alloc(block)) {
        //     if ((block->tiny.header == (size_t)block->tiny.info.next_free) && ((block->tiny.header&size_mask) != 0)) {
        //         dbg_printf("Free tiny %p has 2 identical pointers\n", (void *)block);
        //         return false;
        //     }
        // }
    }
    return true;
}

/**
 * @brief
 * Create the initial empty heap
 *
 * @param[in] void
 * @return bool indicates whether initialization success or not
 */
bool mm_init(void) {
    dbg_printf("INIT\n");
    // Initialize all global variables
    heap_start = NULL;
    last_block = NULL;
    for (size_t i = 0; i < NUM_SEGLISTS; i++) {
        seglist[i] = NULL;
    }

    // Create the initial empty heap
    // 'start' points to the beginning of this new heap
    word_t *start = (word_t *)(mem_sbrk(2 * wsize));

    if (start == (void *)-1) {
        return false;
    }

    start[0] = pack_all(0, true, true, true, false); // Heap prologue (block footer)
    start[1] = pack_all(0, true, true, true, false); // Heap epilogue (block header)

    // Heap starts with first "block header", currently the epilogue
    heap_start = (block_t *)&(start[1]);
    last_block = (block_t *)&(start[0]);

    // Extend the empty heap with a free block of chunksize bytes
    last_block = extend_heap(chunksize);
    if (last_block == NULL) {
        return false;
    }

    return true;
}

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @param[in] size
 * @return
 */
void *malloc(size_t size) {
    dbg_requires(mm_checkheap(__LINE__));

    size_t asize;      // Adjusted block size
    size_t extendsize; // Amount to extend heap if no fit is found
    block_t *block = NULL;
    void *bp = NULL;

    // Initialize heap if it isn't initialized
    if (heap_start == NULL) {
        if (!(mm_init())) {
            dbg_printf("Problem initializing heap. Likely due to sbrk");
            return NULL;
        }
    }

    // Ignore spurious request
    if (size == 0) {
        dbg_ensures(mm_checkheap(__LINE__));
        return bp;
    }

    // Adjust block size to include overhead and meet alignment requirements
    asize = round_up(size + dsize, dsize);
    // Deal with tiny block case
    if (size <= 8) {
        asize = dsize; // Round up to 16 bytes, tiny block
    }
    dbg_printf("\nMALLOC START\n");
    dbg_printf("malloc: request size: %zu\n", asize);

    // Search the free list for a fit
    block = find_fit(asize);

    // If no fit is found, request more memory, and then place the block
    if (block == NULL) {
        // This can be reaching search limit
        if (!get_alloc(last_block)) {
            // If the last block size is larger than asize
            if (get_size(last_block) >= asize) {
                block = last_block;
                block = split_block(block, asize);
                bp = header_to_payload(block);
                return bp;
            }
            extendsize = asize - get_size(last_block);
        } else {
            extendsize = max(asize, chunksize);
        }
        block = extend_heap(extendsize);
        // extend_heap returns an error
        if (block == NULL) {
            return bp;
        }
    }

    // The block should be marked as free
    dbg_assert(!get_alloc(block));

    // If found block is too large, split; Else do nothing
    // After split, both block are possible to be tiny block
    
    block = split_block(block, asize);
    dbg_printf("malloc: Block address returned to user(not bp): %p\n", (void *)block);
    dbg_printf("malloc: Block size after malloc and returned to user: %zu\n", get_size(block));

    bp = header_to_payload(block);

    dbg_ensures(mm_checkheap(__LINE__));
    return bp;
}

/**
 * @brief
 * Free the given block and try coalesce
 *
 * @param[in] bp
 */
void free(void *bp) {
    dbg_requires(mm_checkheap(__LINE__));

    if (bp == NULL) {
        return;
    }

    block_t *block = payload_to_header(bp);
    dbg_printf("\nFREE START\n");
    dbg_printf("free: block require free size is: %zu\n", get_size(block));
    dbg_printf("free: block require free address is: %p\n", (void *)block);

    // The block should be marked as allocated
    dbg_assert(get_alloc(block));

    // Mark the block as free
    // Update only current status of header and footer
    set_cur_status(&block->regular.header, false);

    if (get_tiny_alloc(block)) {
        // Clear cur_alloc bit of tiny block footer
        block->tiny.info.next_free = (block_t *)block->tiny.header;
    } else {
        word_t *footer = header_to_footer(block);
        *footer = block->regular.header;
    }
    add_block_to_seglist(block);

    // Try to coalesce the block with its neighbors
    coalesce_block(block);
    dbg_printf("free: address of block after coalescing: %p\n", (void *)block);

    dbg_ensures(mm_checkheap(__LINE__));
}

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @param[in] ptr
 * @param[in] size
 * @return
 */
void *realloc(void *ptr, size_t size) {
    block_t *block = payload_to_header(ptr);
    size_t copysize;
    void *newptr;

    // If size == 0, then free block and return NULL
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    // If ptr is NULL, then equivalent to malloc
    if (ptr == NULL) {
        return malloc(size);
    }

    // Otherwise, proceed with reallocation
    newptr = malloc(size);

    // If malloc fails, the original block is left untouched
    if (newptr == NULL) {
        return NULL;
    }

    // Copy the old data
    copysize = get_payload_size(block); // gets size of old payload
    if (size < copysize) {
        copysize = size;
    }
    memcpy(newptr, ptr, copysize);

    // Free the old block
    free(ptr);

    return newptr;
}

/**
 * @brief
 *
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 *
 * @param[in] elements
 * @param[in] size
 * @return
 */
void *calloc(size_t elements, size_t size) {
    void *bp;
    size_t asize = elements * size;

    if (elements == 0) {
        return NULL;
    }
    if (asize / elements != size) {
        // Multiplication overflowed
        return NULL;
    }

    bp = malloc(asize);
    if (bp == NULL) {
        return NULL;
    }

    // Initialize all bits to 0
    memset(bp, 0, asize);

    return bp;
}

/*
 *****************************************************************************
 * Do not delete the following super-secret(tm) lines!                       *
 *                                                                           *
 * 53 6f 20 79 6f 75 27 72 65 20 74 72 79 69 6e 67 20 74 6f 20               *
 *                                                                           *
 * 66 69 67 75 72 65 20 6f 75 74 20 77 68 61 74 20 74 68 65 20               *
 * 68 65 78 61 64 65 63 69 6d 61 6c 20 64 69 67 69 74 73 20 64               *
 * 6f 2e 2e 2e 20 68 61 68 61 68 61 21 20 41 53 43 49 49 20 69               *
 *                                                                           *
 * 73 6e 27 74 20 74 68 65 20 72 69 67 68 74 20 65 6e 63 6f 64               *
 * 69 6e 67 21 20 4e 69 63 65 20 74 72 79 2c 20 74 68 6f 75 67               *
 * 68 21 20 2d 44 72 2e 20 45 76 69 6c 0a c5 7c fc 80 6e 57 0a               *
 *                                                                           *
 *****************************************************************************
 */
