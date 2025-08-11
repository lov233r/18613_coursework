#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "proxy.h"

typedef struct cache_node {
    char *key;  // The key (e.g., URL)
    void *data; // The data associated with the key (e.g., HTML content)
    int size;   // Size of the data
    struct cache_node *prev; // Pointer to the previous node in the linked list
    struct cache_node *next; // Pointer to the next node in the linked list
} cache_node_t;

typedef struct {
    cache_node_t *head; // Pointer to the head of the doubly linked list (most
                        // recently used)
    cache_node_t *tail; // Pointer to the tail of the doubly linked list (least
                        // recently used)
    int current_size;   // Current total size of all cached objects
    pthread_mutex_t lock; // Mutex lock for thread-safe operations
} cache_t;

// Hash Table (Simple Array for Example)
#define HASH_TABLE_SIZE 997
cache_node_t *hash_table[HASH_TABLE_SIZE];

// Cache
cache_t cache;

// Simple hash function for demo purposes
unsigned int hash(const char *str) {
    unsigned int hash = 0;
    while (*str) {
        hash = (hash * 31 + *str++) % HASH_TABLE_SIZE;
    }
    return hash;
}

// Function to remove a cache node
void remove_cache_node(cache_node_t *node) {
    if (node == NULL)
        return;

    // Remove node from linked list
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        cache.head = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        cache.tail = node->prev;
    }

    // Remove node from hash table
    unsigned int index = hash(node->key);
    if (hash_table[index] == node) {
        hash_table[index] = NULL;
    }

    // Update current cache size
    cache.current_size -= node->size;

    // Free memory
    free(node->key);
    free(node->data);
    free(node);
}

// Function to add a new cache node
void add_cache_node(const char *key, const void *data, int size) {
    pthread_mutex_lock(&cache.lock);

    if (size > MAX_OBJECT_SIZE) {
        // Object too large to be cached
        pthread_mutex_unlock(&cache.lock);
        return;
    }

    // If cache is full, remove least recently used nodes until there's enough
    // space
    while (cache.current_size + size > MAX_CACHE_SIZE) {
        remove_cache_node(cache.tail);
    }

    // Create new cache node
    cache_node_t *new_node = (cache_node_t *)malloc(sizeof(cache_node_t));
    new_node->key = strdup(key);
    new_node->data = malloc(size);
    memcpy(new_node->data, data, size);
    new_node->size = size;
    new_node->prev = NULL;
    new_node->next = cache.head;

    // Insert new node at the head of the list (most recently used)
    if (cache.head) {
        cache.head->prev = new_node;
    }
    cache.head = new_node;
    if (!cache.tail) {
        cache.tail = new_node;
    }

    // Add node to hash table
    unsigned int index = hash(key);
    hash_table[index] = new_node;

    // Update current cache size
    cache.current_size += size;

    // Unlock the cache
    pthread_mutex_unlock(&cache.lock);
}

// Function to get a cache node by key
int get_cache_node(const char *key, void **data, int *size) {
    // Lock the cache for thread safety
    pthread_mutex_lock(&cache.lock);

    unsigned int index = hash(key);
    cache_node_t *node = hash_table[index];
    if (node == NULL || strcmp(node->key, key) != 0) {
        // Key not found
        pthread_mutex_unlock(&cache.lock);
        return -1;
    }

    // Move accessed node to the head of the list (most recently used)
    if (node != cache.head) {
        // Remove node from its current position
        if (node->prev) {
            node->prev->next = node->next;
        }
        if (node->next) {
            node->next->prev = node->prev;
        }
        if (node == cache.tail) {
            cache.tail = node->prev;
        }

        // Move node to the head of the list
        node->next = cache.head;
        node->prev = NULL;
        if (cache.head) {
            cache.head->prev = node;
        }
        cache.head = node;
    }

    // Unlock the cache
    *size = node->size;
    *data = node->data;
    pthread_mutex_unlock(&cache.lock);

    return 0;
}

// Initialize cache
void init_cache() {
    cache.head = NULL;
    cache.tail = NULL;
    cache.current_size = 0;
    pthread_mutex_init(&cache.lock, NULL);
    memset(hash_table, 0, sizeof(hash_table));
}

// Free all cache nodes
void free_cache() {
    pthread_mutex_lock(&cache.lock);
    cache_node_t *current = cache.head;
    while (current != NULL) {
        cache_node_t *next = current->next;
        free(current->key);
        free(current->data);
        free(current);
        current = next;
    }
    pthread_mutex_unlock(&cache.lock);
    pthread_mutex_destroy(&cache.lock);
}
