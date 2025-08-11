#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include "cachelab.h"
#define LINELEN 1024


// Define the cache struct
typedef struct {
    bool valid;
    bool dirty;  // To track dirty cache lines
    unsigned long tag;
    int lru;  // Used for tracking the least recently used line
    // unsigned long data[];
} CacheLine;

typedef struct {
    CacheLine *lines;
} CacheSet;

typedef struct {
    CacheSet *sets;
    int S;  // Number of sets
    int E;  // Lines per set (associativity)
} Cache;

// global variables being used
char op;
unsigned long hex_addr = 0;
int size = 0;
Cache cache;
int s = -1, b = -1, E = -1, verbose = 0;
char *tracefile = NULL;

csim_stats_t states = {0,0,0,0,0};

// Function prototypes
void print_usage(char *prog_name);
int is_positive_integer(const char *str);
void process_arguments(int argc, char *argv[], int *s, int *b, int *E, int *verbose, char **tracefile);
void validate_arguments(int s, int b, int E, char *tracefile);
void process_trace_file(const char *tracefile);
void accessCache(Cache *cache, int s, int b, char *instruction, csim_stats_t *stats);



// Already defined in cachelab.h
// typedef struct {
//     unsigned long hits; /* number of hits */
//     unsigned long misses; /* number of misses */
//     unsigned long evictions ; /* number of evictions */
//     unsigned long dirty_bytes ; /* number of dirty bytes in cache at 
//     end of simulation */
//     unsigned long dirty_evictions ; /* number of bytes evicted from 
//     dirty lines */
// } csim_stats_t;




// Function to print usage message
void print_usage(char *prog_name) {
    printf("Usage: %s [-v] -s <s> -E <E> -b <b> -t <trace>\n", prog_name);
    printf("       %s -h\n", prog_name);
    printf("\nOptions:\n");
    printf("  -h              Print this help message and exit\n");
    printf("  -v              Verbose mode: report effects of each memory operation\n");
    printf("  -s <s>          Number of set index bits (there are 2^s sets)\n");
    printf("  -b <b>          Number of block bits (there are 2^b blocks)\n");
    printf("  -E <E>          Number of lines per set (associativity)\n");
    printf("  -t <trace>      File name of the memory trace to process\n");
}



// Function to check if a string represents a positive integer
int is_positive_integer(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (!isdigit(str[i])) return 0;
    }
    return 1;
}



// Function to parse and process command-line arguments
void process_arguments(int argc, char *argv[], int *s, int *b, int *E, int *verbose, char **tracefile) {
    int opt;
    while ((opt = getopt(argc, argv, "hvs:b:E:t:")) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                exit(0);
            case 'v':
                *verbose = 1;
                break;
            case 's':
                if (is_positive_integer(optarg)) {
                    *s = atoi(optarg);
                } else {
                    fprintf(stderr, "Error: The value for -s must be a positive integer.\n");
                    exit(1);
                }
                break;
            case 'b':
                if (is_positive_integer(optarg)) {
                    *b = atoi(optarg);
                } else {
                    fprintf(stderr, "Error: The value for -b must be a positive integer.\n");
                    exit(1);
                }
                break;
            case 'E':
                if (is_positive_integer(optarg)) {
                    *E = atoi(optarg);
                } else {
                    fprintf(stderr, "Error: The value for -E must be a positive integer.\n");
                    exit(1);
                }
                break;
            case 't':
                *tracefile = optarg;
                break;
            default:
                print_usage(argv[0]);
                exit(1);
        }
    }
}



// Validate Command Line arguments
void validate_arguments(int s, int b, int E, char *tracefile) {
    if (s == -1 || b == -1 || E == -1 || tracefile == NULL) {
        fprintf(stderr, "Error: Missing required arguments (-s, -b, -E, -t).\n");
        exit(1);
    }

    if (s + b > 64) {
        fprintf(stderr, "Error: The sum of s and b is too large.\n");
        exit(1);
    }
}


// updates op, hex_addr, size
void validate_trace_line (char *trace_line) {
    // Validate the length of the line
    if (trace_line == NULL || strlen(trace_line)<5) {
        exit(1);
    }
    // Validate whether OP starts with L or S
    op = trace_line[0];
    if (op != 'S' && op != 'L') {
        exit(1);
    }

    // Validate address
    char *addr_start = strchr(trace_line, ' '); // find space
    if (addr_start == NULL || *(addr_start + 1) == '\0') {
        exit(1);  // No address found after the operation
    }
    addr_start++;
    // find comma
    char *comma = strchr(trace_line, ',');
    if (comma == NULL) {
        exit(1);  // No comma found, invalid format
    }
    // Validate number of address
    for (char *p = addr_start; p < comma; p++) {
        if (!isxdigit(*p)) {
            exit(1);  // Invalid hex character in the address
        }
    }
    // Allocate subaddress then
    // Extract & Convert address
    unsigned long length = (size_t)(comma - addr_start);
    char substring[LINELEN] = {0};
    strncpy(substring, addr_start, length);
    substring[length + 1] = '\0';
    hex_addr = strtoul(substring, NULL, 16);

    // Validate size
    char *size_start = comma + 1;
    if (*size_start == '\0') {
        exit(1);  // No size specified
    }
    // Ensure the size consists of digits only
    for (char *p = size_start; *p != '\0' && *p != '\n'; p++) {
        if (!isdigit(*p)) {
            exit(1);  // Non-digit character found in size
        }
    }
    size = atoi(size_start);
    if (size <= 0) {
        exit(1);  // Size must be positive
    }
}



// input: trace file
void process_trace_file (const char *tracefile) {
    FILE *tfp = fopen(tracefile, "rt");
    if (!tfp) {
        fprintf(stderr , "Error opening '%s ': %s\n", tracefile, strerror(errno));
    exit(1);
    }
    char linebuf[LINELEN];
    while (fgets(linebuf, LINELEN, tfp)) { 
        validate_trace_line(linebuf); // read/validate the file by line
        accessCache(&cache, s, b, linebuf, &states);
    }
    fclose(tfp);
}



Cache createCache(int s, int E) {
    cache.S = 1 << s;  // S = 2^s
    cache.E = E;
    cache.sets = (CacheSet *)malloc((unsigned long)cache.S * sizeof(CacheSet));
    if (cache.sets == NULL) {
        printf("Memory allocation failed\n");
        exit(1);
    }
    // initialize block status
    for (int i = 0; i < cache.S; i++) {
        cache.sets[i].lines = (CacheLine *)malloc((unsigned long)cache.E * sizeof(CacheLine));
        if (cache.sets[i].lines == NULL) {
            printf("Memory allocation failed\n");
            exit(1);
        }
        for (int j = 0; j < E; j++) {
            cache.sets[i].lines[j].valid = false;
            cache.sets[i].lines[j].dirty = false;
            cache.sets[i].lines[j].lru = 0;
        }
    }
    return cache;
}


// When reading one line of instruction,
// access the cache
// update the stat
void accessCache(Cache *cache, int s, int b, char *instruction, csim_stats_t *stats) {
    // determine which set & tag this address associate with
    unsigned long setIndex = (hex_addr >> b) & ((1 << s) - 1);
    unsigned long tag = hex_addr >> (s + b);
    // Fetch the needed set
    CacheSet set = cache->sets[setIndex];

    // Check for hit, miss, or eviction
    // for each instruction
    bool hit = false;
    int emptyIndex = -1;
    int lruIndex = 0;

    //printf("set index of this one is %lx \n", setIndex);
    //printf("tag of this one is %lx \n", tag);

    for (int i = 0; i < E; i++) {
        if (set.lines[i].valid) {
            if (set.lines[i].tag == tag) {
                hit = true;
                set.lines[i].lru = 0;  // Reset LRU counter for the accessed line
                if (op == 'S') { // write hit: write-back
                    set.lines[i].dirty = true;  // Mark the line as dirty if it's a store
                }
            } else { // not match, LRU++
                set.lines[i].lru++;
            }
        } else if (emptyIndex == -1) {
            emptyIndex = i;  // Record the first empty line if available
        }

        // Keep track of the line with the highest LRU value
        if (set.lines[i].lru > set.lines[lruIndex].lru) {
            lruIndex = i;
        }
    }

    //printf("LRU Index is: %d \n", lruIndex);

    if (hit) {  // when hit
        if (verbose) {
            printf("%s hit\n", instruction);
        }
        stats->hits++;  // Increment the hit counter
        cache->sets[setIndex] = set;
    } else {  // when miss
        if (verbose) {
            printf("%s miss", instruction);
        }
        stats->misses++;  // Increment the miss counter

        if (emptyIndex != -1) { // Fill the empty line
            set.lines[emptyIndex].valid = true;
            set.lines[emptyIndex].tag = tag;
            set.lines[emptyIndex].lru = 0;
            if (op == 'S') { // write miss: write-allocate
                set.lines[emptyIndex].dirty = true;
            }
            if (verbose) {
                printf("\n");
            }
            cache->sets[setIndex] = set;
        } else { // Evict LRU line
            stats->evictions++;  // Increase eviction counter
            if (set.lines[lruIndex].dirty) {
                // Add block size (2^b) to dirty evictions
                stats->dirty_evictions += (1 << b);
            }
            set.lines[lruIndex].valid = true;
            set.lines[lruIndex].tag = tag;
            set.lines[lruIndex].lru = 0;
            set.lines[lruIndex].dirty = (op == 'S');
            if (verbose) {
                printf(" eviction\n");
            }
            cache->sets[setIndex] = set;
        }
    }
}



void freeCache(Cache *cache, csim_stats_t *stats) { // free the whole cache after use
    for (int i = 0; i < (1<<s); i++) {
        for (int j = 0; j < E; j++) {
            // Record dirty bytes still in cache
            if (cache->sets[i].lines[j].dirty == true) {
                stats->dirty_bytes += (1 << b);
            }
        }
        free(cache->sets[i].lines);
    }
    free(cache->sets);
}



/*
Rough roadmap of this program: 
Parse and process arguments
Validate the arguments
Parse the trace file
Validate the trace file:
    Read by line and go thourgh the function
    Exit if the input is incorrect
use queue (FIFO) to simulate load and eviction
Verbose mode: print after each decision,
    print a newline after each loop
*/
int main(int argc, char *argv[]) {
    // Parse and process arguments
    // Return s, b, E, v
    process_arguments(argc, argv, &s, &b, &E, &verbose, &tracefile);

    // Validate the arguments after parsing
    validate_arguments(s, b, E, tracefile);

    // Print verbose information if enabled
    if (verbose) {
        printf("Verbose mode enabled.\n");
        printf("Set index bits: %d\n", s);
        printf("Block bits: %d\n", b);
        printf("Lines per set: %d\n", E);
        printf("Trace file: %s\n", tracefile);
    }

    createCache(s, E);

    // Process the trace file
    process_trace_file(tracefile);

    // free cache after use
    // meanwhile update dirty bytes inside cache
    freeCache(&cache, &states);

    //print the result
    printSummary(&states);

    return 0;
}
