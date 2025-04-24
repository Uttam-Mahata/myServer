/**
 * @file server.h
 * @brief Main header file for the CServer web server
 *
 * This header file contains the core structures and function declarations
 * for the CServer web server, including server configuration, HTTP request/response
 * handling, and logging functionality.
 *
 * @author Your Name
 * @date April 24, 2025
 */

#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

// Server configuration
/**
 * @brief Server configuration structure
 * 
 * This structure holds all configurable parameters for the server,
 * including network settings, thread pool configuration, document root,
 * and logging options.
 */
typedef struct {
    int port;               /**< Port number to listen on */
    int backlog;            /**< Connection backlog size for listen() */
    int thread_pool_size;   /**< Number of worker threads in the thread pool */
    char* doc_root;         /**< Document root directory for serving files */
    int keep_alive_timeout; /**< Keep-alive connection timeout in seconds */
    char* log_file;         /**< Path to the log file */
    int log_level;          /**< Minimum log level to record (DEBUG, INFO, WARN, ERROR) */
    
    /* Database configuration */
    char* db_host;          /**< Database server hostname */
    char* db_port;          /**< Database server port */
    char* db_name;          /**< Database name */
    char* db_user;          /**< Database username */
    char* db_password;      /**< Database password */
    int db_pool_size;       /**< Database connection pool size */
    
    /* HTTPS configuration */
    int enable_https;       /**< Whether to enable HTTPS */
    int https_port;         /**< Port to use for HTTPS */
    char* cert_file;        /**< Path to SSL certificate file */
    char* key_file;         /**< Path to SSL private key file */
    
    /* GZIP compression */
    int enable_gzip;        /**< Whether to enable GZIP compression */
    int gzip_min_size;      /**< Minimum size (in bytes) for GZIP compression */
    
    /* Rate limiting */
    int enable_rate_limit;  /**< Whether to enable rate limiting */
    int rate_limit_max;     /**< Maximum requests per interval */
    int rate_limit_interval;/**< Rate limit interval in seconds */
} server_config_t;

// HTTP request structure
/**
 * @brief HTTP request structure
 * 
 * This structure holds all the parsed data from an HTTP request,
 * including method, path, headers, and body content.
 */
typedef struct {
    char method[16];        /**< HTTP method (GET, POST, etc.) */
    char path[2048];        /**< Request URI path */
    char version[16];       /**< HTTP version (e.g., "HTTP/1.1") */
    char host[255];         /**< Host header value */
    char user_agent[512];   /**< User-Agent header value */
    size_t content_length;  /**< Content-Length header value */
    char* content_type;     /**< Content-Type header value */
    char* body;             /**< Request body content */
    int keep_alive;         /**< Whether the connection should be kept alive */
    char if_none_match[128]; /**< If-None-Match header value for conditional requests */
    char accept_encoding[128]; /**< Accept-Encoding header value for compression */
    char client_ip[50];     /**< Client IP address */
} http_request_t;

// HTTP response structure
/**
 * @brief HTTP response structure
 * 
 * This structure holds all the data needed to generate an HTTP response,
 * including status code, headers, and body content.
 */
typedef struct {
    int status_code;        /**< HTTP status code (e.g., 200, 404) */
    char* status_text;      /**< HTTP status text (e.g., "OK", "Not Found") */
    char* content_type;     /**< Content-Type header value */
    size_t content_length;  /**< Length of the response body in bytes */
    char* body;             /**< Response body content */
    int keep_alive;         /**< Whether the connection should be kept alive */
    http_request_t* request; /**< Reference to the original request for context */
} http_response_t;

// Thread pool task
/**
 * @brief Thread pool task structure
 * 
 * This structure represents a client connection task to be processed
 * by the thread pool. It contains the client socket and address information.
 */
typedef struct {
    int client_socket;               /**< Client socket file descriptor */
    struct sockaddr_in client_addr;  /**< Client address information */
} task_t;

// Function declarations
/**
 * @brief Initializes the server with the provided configuration
 * 
 * This function initializes the server by creating and configuring the server socket,
 * setting up the thread pool, and preparing the logging system.
 * 
 * @param config Pointer to the server configuration structure
 * @return 0 on success, -1 on failure
 */
int server_init(server_config_t* config);

/**
 * @brief Starts the server and begins accepting connections
 * 
 * This function starts the main server loop that accepts incoming client connections
 * and dispatches them to the thread pool for processing.
 * 
 * @return 0 on success, -1 on failure
 */
int server_start();

/**
 * @brief Stops the server and performs cleanup
 * 
 * This function stops the server, closes the server socket, shuts down the thread pool,
 * and performs other cleanup operations.
 */
void server_stop();

/**
 * @brief Handles a client connection
 * 
 * This function processes HTTP requests from a client, generates responses,
 * and sends them back to the client. It handles keep-alive connections and
 * continues processing requests until the connection is closed.
 * 
 * @param client_socket The client socket file descriptor
 * @param client_addr The client address information
 */
void handle_client(int client_socket, struct sockaddr_in client_addr);

/**
 * @brief Parses an HTTP request from a client socket
 * 
 * This function reads data from the client socket and parses it into
 * an HTTP request structure, extracting the method, path, headers, and body.
 * 
 * @param client_socket The client socket file descriptor
 * @param request Pointer to the HTTP request structure to fill
 * @return 1 on success, 0 if connection closed normally, -1 on error
 */
int parse_http_request(int client_socket, http_request_t* request);

/**
 * @brief Handles an HTTP request and generates a response
 * 
 * This function processes an HTTP request and generates an appropriate response,
 * such as serving a file, returning an error, or handling other HTTP operations.
 * 
 * @param request Pointer to the HTTP request structure
 * @param response Pointer to the HTTP response structure to fill
 */
void handle_http_request(http_request_t* request, http_response_t* response);

/**
 * @brief Sends an HTTP response to a client
 * 
 * This function formats and sends an HTTP response to the client,
 * including status line, headers, and body.
 * 
 * @param client_socket The client socket file descriptor
 * @param response Pointer to the HTTP response structure
 */
void send_http_response(int client_socket, http_response_t* response);

/**
 * @brief Logs a message with the specified severity level
 * 
 * This function logs a message with a timestamp and severity level
 * to both the log file and stderr. Messages below the configured
 * minimum log level are not logged.
 * 
 * @param level The severity level of the message (LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR)
 * @param format A printf-style format string
 * @param ... Variable arguments for the format string
 */
void log_message(int level, const char* format, ...);

/**
 * @brief Checks if a client is rate limited
 * 
 * This function checks if a client IP address is rate limited based on
 * the configured rate limiting parameters.
 * 
 * @param client_ip Client IP address
 * @return 1 if rate limited, 0 if not rate limited
 */
int check_rate_limit(const char* client_ip);

/**
 * @brief Compresses data using GZIP
 * 
 * This function compresses data using GZIP compression if the client
 * supports it and the data size exceeds the minimum threshold.
 * 
 * @param data Input data to compress
 * @param size Size of input data
 * @param compressed_data Output compressed data
 * @param compressed_size Size of compressed data
 * @return 0 on success, -1 on failure
 */
int gzip_compress(const char* data, size_t size, char** compressed_data, size_t* compressed_size);

/**
 * @brief Handles an API request
 * 
 * This function processes API requests for the task manager application
 * and generates appropriate JSON responses.
 * 
 * @param request Pointer to the HTTP request structure
 * @param response Pointer to the HTTP response structure to fill
 * @return 1 if request was handled as API request, 0 otherwise
 */
int handle_api_request(http_request_t* request, http_response_t* response);

// Logging levels
/**
 * @brief Debug log level - detailed information for debugging
 */
#define LOG_DEBUG 0

/**
 * @brief Info log level - general informational messages
 */
#define LOG_INFO 1

/**
 * @brief Warning log level - potential issues that aren't errors
 */
#define LOG_WARN 2

/**
 * @brief Error log level - runtime errors that require attention
 */
#define LOG_ERROR 3

// HTTP Status Codes
#define HTTP_OK                  200
#define HTTP_NOT_MODIFIED        304
#define HTTP_BAD_REQUEST         400
#define HTTP_UNAUTHORIZED        401
#define HTTP_FORBIDDEN           403
#define HTTP_NOT_FOUND           404
#define HTTP_METHOD_NOT_ALLOWED  405
#define HTTP_TOO_MANY_REQUESTS   429
#define HTTP_INTERNAL_SERVER_ERROR 500

// HTTP Methods
#define HTTP_METHOD_GET     "GET"
#define HTTP_METHOD_POST    "POST"
#define HTTP_METHOD_PUT     "PUT"
#define HTTP_METHOD_DELETE  "DELETE"
#define HTTP_METHOD_HEAD    "HEAD"
#define HTTP_METHOD_OPTIONS "OPTIONS"

#endif // SERVER_H
