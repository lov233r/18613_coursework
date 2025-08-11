#include <pthread.h>

typedef struct cache_node {
    char *key;               // Key (e.g., URL)
    void *data;              // Cached data (e.g., HTML content)
    int size;                // Size of the data
    struct cache_node *prev; // Pointer to previous node in linked list
    struct cache_node *next; // Pointer to next node in linked list
} cache_node_t;

typedef struct {
    cache_node_t *head; // Pointer to the head of the doubly linked list (most
                        // recently used)
    cache_node_t *tail; // Pointer to the tail of the doubly linked list (least
                        // recently used)
    int current_size;   // Current total size of all cached objects
    pthread_mutex_t lock; // Mutex lock for thread-safe operations
} cache_t;

unsigned int hash(const char *str);
void remove_cache_node(cache_node_t *node);
void add_cache_node(const char *key, const void *data, int size);
int get_cache_node(const char *key, void **data, int *size);
void init_cache();
void free_cache();
