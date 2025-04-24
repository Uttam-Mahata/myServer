/**
 * @file thread_pool.h
 * @brief Thread pool implementation for the CServer web server
 *
 * This header file defines the thread pool structure and functions used
 * by the CServer web server to efficiently handle concurrent client connections.
 *
 * @author 
 * @date April 24, 2025
 */

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include "server.h"

/**
 * @brief Thread pool structure
 * 
 * This structure represents a pool of worker threads that can be used to 
 * process tasks concurrently. It maintains a queue of tasks and a set of
 * worker threads that consume these tasks.
 */
typedef struct {
    pthread_t* threads;     /**< Array of worker thread IDs */
    int size;               /**< Number of worker threads in the pool */
    task_t** queue;         /**< Queue of tasks to be processed */
    int queue_size;         /**< Maximum size of the task queue */
    int head;               /**< Index of the next task to be processed */
    int tail;               /**< Index where the next task will be added */
    int count;              /**< Current number of tasks in the queue */
    pthread_mutex_t lock;   /**< Mutex for thread-safe access to the queue */
    pthread_cond_t notify;  /**< Condition variable for signaling worker threads */
    int shutdown;           /**< Flag indicating if the pool is shutting down */
} thread_pool_t;

/**
 * @brief Creates and initializes a new thread pool
 *
 * @param size Number of worker threads to create in the pool
 * @param queue_size Maximum number of tasks that can be queued
 * @return Pointer to the newly created thread pool, or NULL if creation failed
 */
thread_pool_t* thread_pool_create(int size, int queue_size);

/**
 * @brief Adds a new task to the thread pool's task queue
 *
 * @param pool Pointer to the thread pool
 * @param task Pointer to the task to be added to the queue
 * @return 0 on success, -1 on failure (queue full, pool shutting down, or invalid parameters)
 */
int thread_pool_add(thread_pool_t* pool, task_t* task);

/**
 * @brief Safely destroys a thread pool and frees all associated resources
 *
 * @param pool Pointer to the thread pool structure to be destroyed
 */
void thread_pool_destroy(thread_pool_t* pool);

#endif // THREAD_POOL_H
