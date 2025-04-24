/**
 * @file db.h
 * @brief PostgreSQL database connection and query functions
 *
 * This header file defines the database connection pool and query functions
 * for working with PostgreSQL databases in the CServer web server.
 *
 * @author Your Name
 * @date April 24, 2025
 */

#ifndef DB_H
#define DB_H

#include </usr/include/postgresql/libpq-fe.h>
#include <pthread.h>

/**
 * @brief Maximum number of connections in the database connection pool
 */
#define DB_MAX_CONNECTIONS 10

/**
 * @brief Database connection configuration
 */
typedef struct {
    char* host;         /**< Database server hostname */
    char* port;         /**< Database server port */
    char* dbname;       /**< Database name */
    char* user;         /**< Database username */
    char* password;     /**< Database password */
    int max_connections; /**< Maximum number of connections in the pool */
} db_config_t;

/**
 * @brief Database connection pool
 */
typedef struct {
    PGconn** connections;           /**< Array of database connections */
    int* in_use;                    /**< Array indicating if connection is in use */
    int size;                       /**< Size of the connection pool */
    pthread_mutex_t lock;           /**< Mutex for thread-safe access to the pool */
    db_config_t config;             /**< Database configuration */
} db_pool_t;

/**
 * @brief Initialize the database connection pool
 * 
 * @param config Pointer to database configuration
 * @return Pointer to the connection pool, or NULL on failure
 */
db_pool_t* db_init(db_config_t* config);

/**
 * @brief Close the database connection pool and free resources
 * 
 * @param pool Pointer to the database connection pool
 */
void db_cleanup(db_pool_t* pool);

/**
 * @brief Get a connection from the pool
 * 
 * @param pool Pointer to the database connection pool
 * @return A database connection, or NULL if none available
 */
PGconn* db_get_connection(db_pool_t* pool);

/**
 * @brief Return a connection to the pool
 * 
 * @param pool Pointer to the database connection pool
 * @param conn The database connection to return
 */
void db_return_connection(db_pool_t* pool, PGconn* conn);

/**
 * @brief Execute a SQL query and get the result
 * 
 * @param pool Pointer to the database connection pool
 * @param query The SQL query to execute
 * @param params Array of query parameters
 * @param nParams Number of query parameters
 * @return Query result, or NULL on error
 */
PGresult* db_execute_query(db_pool_t* pool, const char* query, const char* const* params, int nParams);

/**
 * @brief Initialize the database schema
 * 
 * This function creates the necessary tables if they don't exist.
 * 
 * @param pool Pointer to the database connection pool
 * @return 0 on success, -1 on failure
 */
int db_init_schema(db_pool_t* pool);

/**
 * @brief Check if the database connection is valid
 * 
 * @param conn The database connection to check
 * @return 1 if valid, 0 if invalid
 */
int db_connection_valid(PGconn* conn);

#endif /* DB_H */