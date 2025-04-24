/**
 * @file db.c
 * @brief PostgreSQL database connection and query functions implementation
 *
 * This file implements the database connection pool and query functions
 * for working with PostgreSQL databases in the CServer web server.
 *
 * @author Your Name
 * @date April 24, 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "db.h"
#include "server.h"

// Initialize the database connection pool
db_pool_t* db_init(db_config_t* config) {
    log_message(LOG_INFO, "Initializing database connection pool");
    
    // Allocate the pool structure
    db_pool_t* pool = (db_pool_t*)malloc(sizeof(db_pool_t));
    if (!pool) {
        log_message(LOG_ERROR, "Failed to allocate memory for database pool");
        return NULL;
    }
    
    // Set the pool size
    pool->size = config->max_connections > 0 && config->max_connections <= DB_MAX_CONNECTIONS 
                ? config->max_connections 
                : DB_MAX_CONNECTIONS;
    
    // Copy the configuration
    pool->config.host = strdup(config->host ? config->host : "localhost");
    pool->config.port = strdup(config->port ? config->port : "5432");
    pool->config.dbname = strdup(config->dbname ? config->dbname : "cserver");
    pool->config.user = strdup(config->user ? config->user : "postgres");
    pool->config.password = strdup(config->password ? config->password : "postgres");
    pool->config.max_connections = pool->size;
    
    // Allocate connection arrays
    pool->connections = (PGconn**)malloc(pool->size * sizeof(PGconn*));
    pool->in_use = (int*)malloc(pool->size * sizeof(int));
    
    if (!pool->connections || !pool->in_use) {
        log_message(LOG_ERROR, "Failed to allocate memory for database connections");
        db_cleanup(pool);
        return NULL;
    }
    
    // Initialize the mutex
    if (pthread_mutex_init(&pool->lock, NULL) != 0) {
        log_message(LOG_ERROR, "Failed to initialize database pool mutex");
        db_cleanup(pool);
        return NULL;
    }
    
    // Initialize connections
    for (int i = 0; i < pool->size; i++) {
        pool->connections[i] = NULL;
        pool->in_use[i] = 0;
    }
    
    // Create the connection string
    char conninfo[1024];
    snprintf(conninfo, sizeof(conninfo), 
            "host=%s port=%s dbname=%s user=%s password=%s",
            pool->config.host, pool->config.port, 
            pool->config.dbname, pool->config.user, 
            pool->config.password);
    
    // Establish at least one connection to verify database is accessible
    PGconn* conn = PQconnectdb(conninfo);
    if (PQstatus(conn) != CONNECTION_OK) {
        log_message(LOG_ERROR, "Failed to connect to database: %s", PQerrorMessage(conn));
        PQfinish(conn);
        db_cleanup(pool);
        return NULL;
    }
    
    pool->connections[0] = conn;
    log_message(LOG_INFO, "Successfully connected to PostgreSQL database %s", pool->config.dbname);
    
    // Initialize the database schema
    if (db_init_schema(pool) != 0) {
        log_message(LOG_ERROR, "Failed to initialize database schema");
        db_cleanup(pool);
        return NULL;
    }
    
    return pool;
}

// Close the database connection pool and free resources
void db_cleanup(db_pool_t* pool) {
    if (!pool) return;
    
    log_message(LOG_INFO, "Cleaning up database connection pool");
    
    pthread_mutex_lock(&pool->lock);
    
    // Close all connections
    if (pool->connections) {
        for (int i = 0; i < pool->size; i++) {
            if (pool->connections[i]) {
                PQfinish(pool->connections[i]);
                pool->connections[i] = NULL;
            }
        }
        free(pool->connections);
    }
    
    // Free in_use array
    if (pool->in_use) {
        free(pool->in_use);
    }
    
    // Free configuration strings
    if (pool->config.host) free(pool->config.host);
    if (pool->config.port) free(pool->config.port);
    if (pool->config.dbname) free(pool->config.dbname);
    if (pool->config.user) free(pool->config.user);
    if (pool->config.password) free(pool->config.password);
    
    pthread_mutex_unlock(&pool->lock);
    pthread_mutex_destroy(&pool->lock);
    
    free(pool);
}

// Get a connection from the pool
PGconn* db_get_connection(db_pool_t* pool) {
    if (!pool) return NULL;
    
    pthread_mutex_lock(&pool->lock);
    
    // Try to find an existing, unused connection
    for (int i = 0; i < pool->size; i++) {
        if (pool->connections[i] && !pool->in_use[i]) {
            // Check if connection is still valid
            if (db_connection_valid(pool->connections[i])) {
                pool->in_use[i] = 1;
                pthread_mutex_unlock(&pool->lock);
                return pool->connections[i];
            } else {
                // Connection is broken, close it and create a new one
                PQfinish(pool->connections[i]);
                pool->connections[i] = NULL;
            }
        }
    }
    
    // Try to find a slot for a new connection
    for (int i = 0; i < pool->size; i++) {
        if (!pool->connections[i]) {
            // Create a new connection
            char conninfo[1024];
            snprintf(conninfo, sizeof(conninfo), 
                    "host=%s port=%s dbname=%s user=%s password=%s",
                    pool->config.host, pool->config.port, 
                    pool->config.dbname, pool->config.user, 
                    pool->config.password);
            
            PGconn* conn = PQconnectdb(conninfo);
            if (PQstatus(conn) == CONNECTION_OK) {
                pool->connections[i] = conn;
                pool->in_use[i] = 1;
                pthread_mutex_unlock(&pool->lock);
                return conn;
            } else {
                log_message(LOG_ERROR, "Failed to create new database connection: %s", 
                          PQerrorMessage(conn));
                PQfinish(conn);
                pthread_mutex_unlock(&pool->lock);
                return NULL;
            }
        }
    }
    
    // All connections are in use
    log_message(LOG_WARN, "All database connections are in use");
    pthread_mutex_unlock(&pool->lock);
    return NULL;
}

// Return a connection to the pool
void db_return_connection(db_pool_t* pool, PGconn* conn) {
    if (!pool || !conn) return;
    
    pthread_mutex_lock(&pool->lock);
    
    // Find the connection in the pool
    for (int i = 0; i < pool->size; i++) {
        if (pool->connections[i] == conn) {
            pool->in_use[i] = 0;
            break;
        }
    }
    
    pthread_mutex_unlock(&pool->lock);
}

// Execute a SQL query and get the result
PGresult* db_execute_query(db_pool_t* pool, const char* query, 
                          const char* const* params, int nParams) {
    if (!pool || !query) return NULL;
    
    PGconn* conn = db_get_connection(pool);
    if (!conn) {
        log_message(LOG_ERROR, "Failed to get database connection for query");
        return NULL;
    }
    
    PGresult* result;
    if (nParams > 0 && params != NULL) {
        // Execute query with parameters
        result = PQexecParams(conn, query, nParams, NULL, params, NULL, NULL, 0);
    } else {
        // Execute simple query
        result = PQexec(conn, query);
    }
    
    if (PQresultStatus(result) != PGRES_TUPLES_OK && 
        PQresultStatus(result) != PGRES_COMMAND_OK) {
        log_message(LOG_ERROR, "Database query failed: %s", PQerrorMessage(conn));
        PQclear(result);
        db_return_connection(pool, conn);
        return NULL;
    }
    
    db_return_connection(pool, conn);
    return result;
}

// Initialize the database schema
int db_init_schema(db_pool_t* pool) {
    if (!pool) return -1;
    
    log_message(LOG_INFO, "Initializing database schema");
    
    PGconn* conn = db_get_connection(pool);
    if (!conn) {
        log_message(LOG_ERROR, "Failed to get database connection for schema initialization");
        return -1;
    }
    
    // Create tasks table for the task manager application
    const char* create_tasks_table = 
        "CREATE TABLE IF NOT EXISTS tasks ("
        "id SERIAL PRIMARY KEY,"
        "title VARCHAR(255) NOT NULL,"
        "priority VARCHAR(50) NOT NULL,"
        "due_date DATE,"
        "completed BOOLEAN DEFAULT FALSE,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");";
    
    PGresult* result = PQexec(conn, create_tasks_table);
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        log_message(LOG_ERROR, "Failed to create tasks table: %s", PQerrorMessage(conn));
        PQclear(result);
        db_return_connection(pool, conn);
        return -1;
    }
    PQclear(result);
    
    // Create users table for authentication
    const char* create_users_table = 
        "CREATE TABLE IF NOT EXISTS users ("
        "id SERIAL PRIMARY KEY,"
        "username VARCHAR(50) UNIQUE NOT NULL,"
        "password_hash VARCHAR(255) NOT NULL,"
        "email VARCHAR(255) UNIQUE NOT NULL,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");";
    
    result = PQexec(conn, create_users_table);
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        log_message(LOG_ERROR, "Failed to create users table: %s", PQerrorMessage(conn));
        PQclear(result);
        db_return_connection(pool, conn);
        return -1;
    }
    PQclear(result);
    
    // Create rate limiting table
    const char* create_rate_limit_table = 
        "CREATE TABLE IF NOT EXISTS rate_limits ("
        "ip_address VARCHAR(50) PRIMARY KEY,"
        "request_count INT DEFAULT 0,"
        "last_request TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");";
    
    result = PQexec(conn, create_rate_limit_table);
    if (PQresultStatus(result) != PGRES_COMMAND_OK) {
        log_message(LOG_ERROR, "Failed to create rate_limits table: %s", PQerrorMessage(conn));
        PQclear(result);
        db_return_connection(pool, conn);
        return -1;
    }
    PQclear(result);
    
    log_message(LOG_INFO, "Database schema initialized successfully");
    db_return_connection(pool, conn);
    return 0;
}

// Check if the database connection is valid
int db_connection_valid(PGconn* conn) {
    if (!conn) return 0;
    
    // Check connection status
    if (PQstatus(conn) != CONNECTION_OK) {
        // Try to reset the connection
        PQreset(conn);
        if (PQstatus(conn) != CONNECTION_OK) {
            return 0;
        }
    }
    
    // Execute a simple query to verify connection
    PGresult* result = PQexec(conn, "SELECT 1");
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        PQclear(result);
        return 0;
    }
    
    PQclear(result);
    return 1;
}