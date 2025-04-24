#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "thread_pool.h"
#include "server.h"

// Worker thread function
static void* worker_thread(void* arg) {
    thread_pool_t* pool = (thread_pool_t*)arg;
    task_t* task;

    while (1) {
        // Lock the queue
        pthread_mutex_lock(&pool->lock);

        // Wait for a task or shutdown signal
        while (pool->count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->notify, &pool->lock);
        }

        // Check if pool is shutting down
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->lock);
            pthread_exit(NULL);
        }

        // Get a task from the queue
        task = pool->queue[pool->head];
        pool->queue[pool->head] = NULL;
        pool->head = (pool->head + 1) % pool->queue_size;
        pool->count--;

        // Unlock the queue
        pthread_mutex_unlock(&pool->lock);

        // Execute the task
        if (task) {
            handle_client(task->client_socket, task->client_addr);
            free(task);
        }
    }

    return NULL;
}

// Create a thread pool
/**
 * @brief Creates and initializes a new thread pool
 *
 * This function allocates and initializes a new thread pool with the specified number
 * of worker threads and task queue size. The thread pool is used to efficiently handle
 * concurrent client connections without creating a new thread for each connection.
 *
 * The function performs the following steps:
 * 1. Allocates memory for the thread pool structure
 * 2. Allocates memory for worker threads and the task queue
 * 3. Initializes synchronization primitives (mutex and condition variable)
 * 4. Creates the specified number of worker threads
 *
 * Each worker thread runs in a loop waiting for tasks to be added to the queue.
 * When a task is added, one of the waiting threads is awakened to handle the task.
 *
 * @param size Number of worker threads to create in the pool
 * @param queue_size Maximum number of tasks that can be queued
 * @return Pointer to the newly created thread pool, or NULL if creation failed
 * 
 * @note The caller is responsible for destroying the thread pool when it is no longer
 *       needed by calling thread_pool_destroy().
 */
thread_pool_t* thread_pool_create(int size, int queue_size) {
    thread_pool_t* pool;
    int i;

    // Allocate memory for pool
    pool = (thread_pool_t*)malloc(sizeof(thread_pool_t));
    if (!pool) {
        return NULL;
    }

    // Initialize pool
    pool->size = size;
    pool->queue_size = queue_size;
    pool->head = 0;
    pool->tail = 0;
    pool->count = 0;
    pool->shutdown = 0;

    // Allocate memory for threads and task queue
    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * size);
    pool->queue = (task_t**)malloc(sizeof(task_t*) * queue_size);

    if (!pool->threads || !pool->queue) {
        if (pool->threads) free(pool->threads);
        if (pool->queue) free(pool->queue);
        free(pool);
        return NULL;
    }

    // Initialize mutex and condition variable
    if (pthread_mutex_init(&pool->lock, NULL) != 0 ||
        pthread_cond_init(&pool->notify, NULL) != 0) {
        free(pool->threads);
        free(pool->queue);
        free(pool);
        return NULL;
    }

    // Initialize task queue
    for (i = 0; i < queue_size; i++) {
        pool->queue[i] = NULL;
    }

    // Create worker threads
    for (i = 0; i < size; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            thread_pool_destroy(pool);
            return NULL;
        }
    }

    log_message(LOG_INFO, "Thread pool created with %d threads", size);
    return pool;
}

// Add a task to the thread pool
/**
 * @brief Adds a new task to the thread pool's task queue
 *
 * This function safely adds a client connection task to the thread pool's queue for
 * processing by one of the worker threads. It ensures thread safety by using a mutex
 * to protect access to the shared task queue.
 *
 * The function performs the following operations:
 * 1. Validates the input parameters
 * 2. Acquires the queue lock to ensure thread safety
 * 3. Checks if the queue is full or if the pool is shutting down
 * 4. Adds the task to the queue and updates the queue pointers
 * 5. Signals a waiting worker thread that a task is available
 * 6. Releases the queue lock
 *
 * @param pool Pointer to the thread pool
 * @param task Pointer to the task to be added to the queue
 * @return 0 on success, -1 on failure (queue full, pool shutting down, or invalid parameters)
 * 
 * @note The task must be dynamically allocated and will be freed by the worker thread
 *       after processing. The caller should not free the task after adding it to the queue.
 */
int thread_pool_add(thread_pool_t* pool, task_t* task) {
    if (!pool || !task) {
        return -1;
    }

    // Lock the queue
    pthread_mutex_lock(&pool->lock);

    // Check if queue is full
    if (pool->count == pool->queue_size) {
        pthread_mutex_unlock(&pool->lock);
        log_message(LOG_WARN, "Thread pool queue is full");
        return -1;
    }

    // Check if pool is shutting down
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->lock);
        log_message(LOG_WARN, "Thread pool is shutting down");
        return -1;
    }

    // Add task to queue
    pool->queue[pool->tail] = task;
    pool->tail = (pool->tail + 1) % pool->queue_size;
    pool->count++;

    // Signal a worker thread
    pthread_cond_signal(&pool->notify);

    // Unlock the queue
    pthread_mutex_unlock(&pool->lock);

    return 0;
}

// Destroy the thread pool
/**
 * @brief Safely destroys a thread pool and frees all associated resources
 *
 * This function performs a graceful shutdown of the thread pool by:
 * 1. Setting the shutdown flag to signal all worker threads to terminate
 * 2. Waking up all waiting threads via pthread_cond_broadcast
 * 3. Joining all worker threads to ensure they complete cleanly
 * 4. Destroying synchronization primitives (mutex and condition variable)
 * 5. Closing any open client sockets in the task queue
 * 6. Freeing all allocated memory
 *
 * The function is thread-safe and can be called from any thread. It ensures
 * that no memory leaks occur and all system resources are properly released.
 * If the thread pool is already in the process of shutting down, the function
 * will return immediately to prevent duplicate shutdown attempts.
 *
 * @param pool Pointer to the thread pool structure to be destroyed
 * @return void
 * 
 * @note This function should be called when the server is shutting down or
 *       when the thread pool is no longer needed. Attempting to use the pool
 *       after calling this function will result in undefined behavior.
 */
void thread_pool_destroy(thread_pool_t* pool) {
    if (!pool) {
        return;
    }

    // Lock the queue
    pthread_mutex_lock(&pool->lock);

    // Check if pool is already shutting down
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->lock);
        return;
    }

    // Set shutdown flag
    pool->shutdown = 1;

    // Signal all worker threads
    pthread_cond_broadcast(&pool->notify);

    // Unlock the queue
    pthread_mutex_unlock(&pool->lock);

    // Wait for worker threads to exit
    for (int i = 0; i < pool->size; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    // Clean up resources
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->notify);

    // Free memory
    for (int i = 0; i < pool->queue_size; i++) {
        if (pool->queue[i]) {
            close(pool->queue[i]->client_socket);
            free(pool->queue[i]);
        }
    }
    free(pool->threads);
    free(pool->queue);
    free(pool);

    log_message(LOG_INFO, "Thread pool destroyed");
}
