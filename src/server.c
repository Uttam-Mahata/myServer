/**
 * @file server.c
 * @brief Main server implementation for CServer
 *
 * This file contains the implementation of the core server functionality,
 * including socket handling, client connection processing, HTTP request parsing,
 * and response generation.
 *
 * @author Your Name
 * @date April 24, 2025
 */

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
#include <sys/time.h>
#include <stdarg.h>
#include <stdint.h>

#include "server.h"
#include "thread_pool.h"
#include "http.h"

// Global variables
/**
 * @brief Global server variables
 * 
 * These static variables maintain the state of the server:
 */
static int server_socket = -1;                                  /**< Server socket file descriptor */
static thread_pool_t* thread_pool = NULL;                       /**< Thread pool for handling client connections */
static server_config_t* config = NULL;                          /**< Server configuration */
static FILE* log_file = NULL;                                   /**< Log file handle */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;   /**< Mutex for thread-safe logging */

// Rate limiting data structures
typedef struct {
    char ip[46];               // Store the IP address (IPv4 or IPv6)
    time_t timestamps[1000];   // Circular buffer of request timestamps
    int current_index;         // Current position in circular buffer
    int count;                 // Number of entries in the buffer
    pthread_mutex_t mutex;     // Mutex for thread safety
} rate_limit_entry_t;

#define RATE_LIMIT_TABLE_SIZE 1024
static rate_limit_entry_t rate_limit_table[RATE_LIMIT_TABLE_SIZE];
static int rate_limit_initialized = 0;

/**
 * @brief Initialize rate limiting data structures
 */
static void init_rate_limiting(void) {
    if (rate_limit_initialized) return;
    
    memset(rate_limit_table, 0, sizeof(rate_limit_table));
    
    // Initialize mutexes
    for (int i = 0; i < RATE_LIMIT_TABLE_SIZE; i++) {
        pthread_mutex_init(&rate_limit_table[i].mutex, NULL);
    }
    
    rate_limit_initialized = 1;
    log_message(LOG_INFO, "Rate limiting initialized with %d second window, %d max requests", 
                config->rate_limit_interval, config->rate_limit_max);
}

/**
 * @brief Simple hash function for IP addresses
 * 
 * @param ip IP address string
 * @return unsigned int Hash value
 */
static unsigned int ip_hash(const char* ip) {
    unsigned int hash = 0;
    while (*ip) {
        hash = hash * 31 + *ip++;
    }
    return hash % RATE_LIMIT_TABLE_SIZE;
}

// Initialize the server
/**
 * @brief Initializes the server with the provided configuration
 * 
 * This function initializes the server by:
 * 1. Storing the server configuration for global access
 * 2. Opening the log file for writing
 * 3. Creating a thread pool for handling client connections
 * 4. Creating and configuring the server socket (with appropriate options)
 * 5. Binding the socket to the configured port
 * 6. Setting the socket to listen mode with the configured backlog
 *
 * The server must be initialized before it can be started.
 * 
 * @param server_config Pointer to the server configuration structure
 * @return 0 on success, -1 on failure with error logged
 */
int server_init(server_config_t* server_config) {
    // Store the config
    config = server_config;

    // Open log file
    log_file = fopen(config->log_file, "a");
    if (!log_file) {
        fprintf(stderr, "Failed to open log file: %s\n", config->log_file);
        return -1;
    }

    // Create thread pool
    thread_pool = thread_pool_create(config->thread_pool_size, config->backlog);
    if (!thread_pool) {
        log_message(LOG_ERROR, "Failed to create thread pool");
        return -1;
    }

    // Create server socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        log_message(LOG_ERROR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    log_message(LOG_DEBUG, "Server socket created successfully with fd %d", server_socket);

    // Set socket options
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_message(LOG_WARN, "Failed to set SO_REUSEADDR: %s", strerror(errno));
    }

    // Bind to port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config->port);

    log_message(LOG_DEBUG, "Attempting to bind to 0.0.0.0:%d", config->port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        log_message(LOG_ERROR, "Failed to bind to port %d: %s", config->port, strerror(errno));
        return -1;
    }
    
    log_message(LOG_DEBUG, "Successfully bound to port %d", config->port);

    // Listen for connections
    log_message(LOG_DEBUG, "Setting socket to listen mode with backlog %d", config->backlog);
    
    if (listen(server_socket, config->backlog) < 0) {
        log_message(LOG_ERROR, "Failed to listen on socket: %s", strerror(errno));
        return -1;
    }
    
    log_message(LOG_DEBUG, "Socket in listen mode, ready to accept connections");

    return 0;
}

// Start the server
/**
 * @brief Starts the server and begins accepting connections
 * 
 * This function starts the main server loop that:
 * 1. Accepts incoming client connections
 * 2. Sets up non-blocking mode for each client socket
 * 3. Creates a task for the thread pool
 * 4. Dispatches the task to the thread pool for processing
 *
 * The function runs until interrupted (typically by a signal handler
 * calling server_stop), or until an unrecoverable error occurs.
 * 
 * @return 0 on normal termination, -1 on error
 * 
 * @note The server must be initialized with server_init() before calling this function
 */
int server_start() {
    if (server_socket < 0 || !thread_pool || !config) {
        log_message(LOG_ERROR, "Server not initialized");
        return -1;
    }

    log_message(LOG_INFO, "Server started on port %d", config->port);
    log_message(LOG_DEBUG, "Waiting for connections on socket %d", server_socket);

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Accept connections
    while (1) {
        log_message(LOG_DEBUG, "Calling accept() to wait for a new connection...");
        
        // Accept a new client connection
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, probably shutdown
                log_message(LOG_INFO, "Accept interrupted by signal, shutting down");
                break;
            }
            log_message(LOG_ERROR, "Failed to accept connection: %s", strerror(errno));
            continue;
        }

        log_message(LOG_INFO, "New connection accepted: client_socket=%d from %s:%d", 
                   client_socket, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Set non-blocking socket
        int flags = fcntl(client_socket, F_GETFL, 0);
        fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);

        // Create a task for the thread pool
        task_t* task = (task_t*)malloc(sizeof(task_t));
        if (!task) {
            log_message(LOG_ERROR, "Failed to allocate memory for task");
            close(client_socket);
            continue;
        }

        task->client_socket = client_socket;
        task->client_addr = client_addr;

        // Add the task to the thread pool
        if (thread_pool_add(thread_pool, task) != 0) {
            log_message(LOG_ERROR, "Failed to add task to thread pool");
            close(client_socket);
            free(task);
            continue;
        }

        log_message(LOG_DEBUG, "Task created and added to thread pool for client %s:%d", 
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    }

    return 0;
}

// Stop the server
/**
 * @brief Stops the server and performs cleanup
 * 
 * This function gracefully stops the server by:
 * 1. Closing the server socket to prevent new connections
 * 2. Shutting down the thread pool, which waits for all worker threads to complete
 * 3. Closing the log file
 * 4. Logging the server shutdown
 *
 * This function is typically called from a signal handler to provide
 * a clean shutdown when the program receives a termination signal.
 */
void server_stop() {
    if (server_socket >= 0) {
        close(server_socket);
        server_socket = -1;
    }

    if (thread_pool) {
        thread_pool_destroy(thread_pool);
        thread_pool = NULL;
    }

    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }

    log_message(LOG_INFO, "Server stopped");
}

// Handle client connection
/**
 * @brief Handles a client connection
 * 
 * This function processes HTTP requests from a client by:
 * 1. Setting a timeout for receive operations based on the keep-alive configuration
 * 2. Reading and parsing HTTP requests from the client socket
 * 3. Generating appropriate HTTP responses
 * 4. Sending the responses back to the client
 * 5. Managing connection persistence (keep-alive) based on client requests
 * 6. Freeing resources and closing the connection when finished
 *
 * @param client_socket The client socket file descriptor
 * @param client_addr The client address information
 * 
 * @note This function is typically called by a worker thread from the thread pool
 */
void handle_client(int client_socket, struct sockaddr_in client_addr) {
    // Set timeout for receive
    struct timeval tv;
    tv.tv_sec = config->keep_alive_timeout;
    tv.tv_usec = 0;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    // Get client IP for logging and rate limiting
    char client_ip[50];
    strncpy(client_ip, inet_ntoa(client_addr.sin_addr), sizeof(client_ip) - 1);
    
    log_message(LOG_DEBUG, "Handling client %s:%d", 
               client_ip, ntohs(client_addr.sin_port));

    // Process requests until connection is closed or timeout
    while (1) {
        // Check rate limit before processing request
        if (check_rate_limit(client_ip)) {
            // Client is rate limited, send 429 response
            log_message(LOG_WARN, "Rate limit applied to client %s", client_ip);
            
            // Create a rate limit exceeded response
            http_response_t rate_limit_response;
            memset(&rate_limit_response, 0, sizeof(rate_limit_response));
            
            const char* rate_limit_message = "Too many requests. Please try again later.";
            rate_limit_response.status_code = HTTP_TOO_MANY_REQUESTS;
            rate_limit_response.status_text = strdup("Too Many Requests");
            rate_limit_response.content_type = strdup("text/plain");
            rate_limit_response.content_length = strlen(rate_limit_message);
            rate_limit_response.body = strdup(rate_limit_message);
            rate_limit_response.keep_alive = 0; // Close connection for rate-limited clients
            
            // Send the rate limit response
            send_http_response(client_socket, &rate_limit_response);
            
            // Free resources
            free(rate_limit_response.status_text);
            free(rate_limit_response.content_type);
            free(rate_limit_response.body);
            
            // Close connection for rate-limited clients
            break;
        }

        // Parse HTTP request
        http_request_t request;
        memset(&request, 0, sizeof(request));
        
        // Store client IP in request structure
        strncpy(request.client_ip, client_ip, sizeof(request.client_ip) - 1);

        int result = parse_http_request(client_socket, &request);
        if (result <= 0) {
            if (result < 0) {
                log_message(LOG_DEBUG, "Error parsing request from %s:%d", 
                           client_ip, ntohs(client_addr.sin_port));
            }
            break;
        }

        // Prepare HTTP response
        http_response_t response;
        memset(&response, 0, sizeof(response));
        response.keep_alive = request.keep_alive;
        response.request = &request; // Add reference to client request

        // Handle the request
        handle_http_request(&request, &response);

        // Send response
        send_http_response(client_socket, &response);

        // Free resources
        if (request.content_type) {
            free(request.content_type);
        }
        if (request.body) {
            free(request.body);
        }
        if (response.status_text) {
            free(response.status_text);
        }
        if (response.content_type) {
            free(response.content_type);
        }
        if (response.body) {
            free(response.body);
        }

        // Close connection if not keep-alive
        if (!request.keep_alive) {
            break;
        }
    }

    // Close the client socket
    close(client_socket);
}

// Parse HTTP request
/**
 * @brief Parses an HTTP request from a client socket
 * 
 * This function reads data from the client socket and parses it into an HTTP request structure by:
 * 1. Reading raw data from the socket into a buffer
 * 2. Parsing the request line to extract method, path, and HTTP version
 * 3. Parsing headers to extract key information like Host, Content-Type, and Connection
 * 4. Extracting the request body if present
 *
 * @param client_socket The client socket file descriptor to read from
 * @param request Pointer to the HTTP request structure to populate
 * @return 1 on successful parse, 0 if connection closed cleanly, -1 on error
 */
int parse_http_request(int client_socket, http_request_t* request) {
    char buffer[8192] = {0};
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_received <= 0) {
        // Connection closed or error
        return bytes_received;
    }

    // Ensure null termination
    buffer[bytes_received] = '\0';

    // Get client IP address
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(client_socket, (struct sockaddr*)&addr, &addr_len);
    strncpy(request->client_ip, inet_ntoa(addr.sin_addr), sizeof(request->client_ip) - 1);

    // Parse the request line
    char* line = strtok(buffer, "\r\n");
    if (!line) {
        return -1;
    }

    sscanf(line, "%15s %2047s %15s", request->method, request->path, request->version);

    // Parse headers
    request->keep_alive = 0;
    request->content_length = 0;
    request->if_none_match[0] = '\0'; // Initialize If-None-Match to empty string
    request->accept_encoding[0] = '\0'; // Initialize Accept-Encoding to empty string

    while ((line = strtok(NULL, "\r\n")) != NULL && strlen(line) > 0) {
        if (strncasecmp(line, "Host: ", 6) == 0) {
            strncpy(request->host, line + 6, sizeof(request->host) - 1);
        } else if (strncasecmp(line, "User-Agent: ", 12) == 0) {
            strncpy(request->user_agent, line + 12, sizeof(request->user_agent) - 1);
        } else if (strncasecmp(line, "Content-Length: ", 16) == 0) {
            request->content_length = atoi(line + 16);
        } else if (strncasecmp(line, "Content-Type: ", 14) == 0) {
            request->content_type = strdup(line + 14);
        } else if (strncasecmp(line, "Connection: ", 12) == 0) {
            if (strcasestr(line + 12, "keep-alive")) {
                request->keep_alive = 1;
            }
        } else if (strncasecmp(line, "If-None-Match: ", 15) == 0) {
            // Extract the ETag value from If-None-Match header
            strncpy(request->if_none_match, line + 15, sizeof(request->if_none_match) - 1);
            log_message(LOG_DEBUG, "Found If-None-Match header: %s", request->if_none_match);
        } else if (strncasecmp(line, "Accept-Encoding: ", 17) == 0) {
            // Extract the Accept-Encoding header for compression support
            strncpy(request->accept_encoding, line + 17, sizeof(request->accept_encoding) - 1);
            log_message(LOG_DEBUG, "Found Accept-Encoding header: %s", request->accept_encoding);
        }
    }

    // Find the request body
    char* body = strstr(buffer, "\r\n\r\n");
    if (body && request->content_length > 0) {
        body += 4; // Skip the \r\n\r\n
        request->body = strdup(body);
    }

    log_message(LOG_DEBUG, "Parsed %s request for %s from %s", 
               request->method, request->path, request->client_ip);
    return 1;
}

// Handle HTTP request
/**
 * @brief Handles an HTTP request and generates a response
 * 
 * This function processes an HTTP request and generates an appropriate response by:
 * 1. Validating the HTTP method (only GET and HEAD are supported)
 * 2. Resolving the requested path to a file in the document root
 * 3. Checking if the file exists and is accessible
 * 4. Handling directory requests (redirecting if needed, serving index.html)
 * 5. Determining the MIME type of the requested file
 * 6. Handling conditional requests with If-None-Match header
 * 7. Reading the file content (if it exists and is needed)
 * 8. Building the appropriate HTTP response
 *
 * @param request Pointer to the HTTP request structure to process
 * @param response Pointer to the HTTP response structure to populate
 */
void handle_http_request(http_request_t* request, http_response_t* response) {
    // Check if method is supported
    if (strcmp(request->method, HTTP_METHOD_GET) != 0 && 
        strcmp(request->method, HTTP_METHOD_HEAD) != 0) {
        build_http_response(response, HTTP_METHOD_NOT_ALLOWED, MIME_TEXT, 
                          "Method Not Allowed", 18);
        return;
    }

    // Default to index.html if path is "/"
    char file_path[2560];
    if (strcmp(request->path, "/") == 0) {
        snprintf(file_path, sizeof(file_path), "%s/index.html", config->doc_root);
    } else {
        snprintf(file_path, sizeof(file_path), "%s%s", config->doc_root, request->path);
    }

    // Check if file exists and is readable
    struct stat st;
    if (stat(file_path, &st) < 0) {
        build_http_response(response, HTTP_NOT_FOUND, MIME_TEXT, "Not Found", 9);
        return;
    }

    // Check if path is a directory
    if (S_ISDIR(st.st_mode)) {
        // Redirect to add trailing slash if needed
        if (request->path[strlen(request->path) - 1] != '/') {
            char redirect_path[2560];
            snprintf(redirect_path, sizeof(redirect_path), "%s/", request->path);
            
            response->status_code = 301;
            response->status_text = strdup("Moved Permanently");
            response->content_type = strdup(MIME_TEXT);
            response->content_length = 0;
            response->body = NULL;
            response->keep_alive = request->keep_alive;
            return;
        }

        // Try to serve index.html from directory
        snprintf(file_path, sizeof(file_path), "%s%s/index.html", config->doc_root, request->path);
        if (stat(file_path, &st) < 0) {
            build_http_response(response, HTTP_NOT_FOUND, MIME_TEXT, "Not Found", 9);
            return;
        }
    }

    // Get MIME type for file
    const char* mime_type = get_mime_type(file_path);
    
    // Generate ETag based on file size and modification time
    char etag[64];
    snprintf(etag, sizeof(etag), "W/\"%lx-%lx\"", (unsigned long)st.st_size, (unsigned long)st.st_mtime);
    
    // Check for conditional request (If-None-Match header)
    if (request->if_none_match[0] != '\0') {
        // Check if the ETag in the If-None-Match header matches our ETag
        // This is a simple comparison, but in reality we might want to handle multiple ETags
        // and weak vs. strong validation
        if (strstr(request->if_none_match, etag) != NULL) {
            // Resource hasn't changed, send 304 Not Modified
            log_message(LOG_DEBUG, "ETag match, sending 304 Not Modified: %s", etag);
            
            response->status_code = 304;
            response->status_text = strdup("Not Modified");
            response->content_type = strdup(mime_type);
            response->content_length = 0;
            response->body = NULL;
            response->keep_alive = request->keep_alive;
            return;
        }
        
        log_message(LOG_DEBUG, "ETag mismatch, client: %s, server: %s", 
                  request->if_none_match, etag);
    }

    // Read file content
    char* content = NULL;
    size_t content_length = 0;
    
    if (read_file_content(file_path, &content, &content_length) < 0) {
        build_http_response(response, HTTP_INTERNAL_SERVER_ERROR, MIME_TEXT, 
                          "Internal Server Error", 21);
        return;
    }

    // For HEAD requests, don't include body
    if (strcmp(request->method, HTTP_METHOD_HEAD) == 0) {
        free(content);
        content = NULL;
        content_length = 0;
    }

    // Build success response
    build_http_response(response, HTTP_OK, mime_type, content, content_length);
    
    if (content) {
        free(content);
    }
}

// Send HTTP response
/**
 * @brief Sends an HTTP response to a client
 * 
 * This function formats and sends a complete HTTP response to the client by:
 * 1. Formatting the current timestamp for the Date header
 * 2. Building the response header with status line, server name, and content info
 * 3. Adding caching headers based on content type
 * 4. Sending the headers to the client
 * 5. Sending the response body if present
 *
 * @param client_socket The client socket file descriptor to send to
 * @param response Pointer to the HTTP response structure containing response data
 */
void send_http_response(int client_socket, http_response_t* response) {
    // Get client request details for compression check
    http_request_t* client_request = response->request;

    // Format current time
    char time_buffer[100];
    time_t now = time(NULL);
    struct tm* time_info = gmtime(&now);
    strftime(time_buffer, sizeof(time_buffer), "%a, %d %b %Y %H:%M:%S GMT", time_info);

    // Determine caching strategy based on content type
    char cache_headers[1024] = "";
    char etag[64] = "";
    
    // Only add caching headers for successful responses
    if (response->status_code == HTTP_OK) {
        // Generate a simple ETag based on content size and current time
        snprintf(etag, sizeof(etag), "W/\"%zx-%lx\"", response->content_length, (unsigned long)now);
        
        // Set different cache policies based on content type
        if (response->content_type) {
            if (strncmp(response->content_type, "text/html", 9) == 0) {
                // Short cache time for HTML (1 hour)
                snprintf(cache_headers, sizeof(cache_headers),
                        "Cache-Control: public, max-age=3600\r\n"
                        "ETag: %s\r\n", etag);
            } 
            else if (strncmp(response->content_type, "text/css", 8) == 0 ||
                    strncmp(response->content_type, "application/javascript", 22) == 0) {
                // Longer cache time for CSS and JavaScript (1 week)
                snprintf(cache_headers, sizeof(cache_headers),
                        "Cache-Control: public, max-age=604800\r\n"
                        "ETag: %s\r\n", etag);
            }
            else if (strncmp(response->content_type, "image/", 6) == 0) {
                // Long cache time for images (1 month)
                snprintf(cache_headers, sizeof(cache_headers),
                        "Cache-Control: public, max-age=2592000\r\n"
                        "ETag: %s\r\n", etag);
            }
            else {
                // Default cache policy (1 day)
                snprintf(cache_headers, sizeof(cache_headers),
                        "Cache-Control: public, max-age=86400\r\n"
                        "ETag: %s\r\n", etag);
            }
        }
    }
    else {
        // No caching for error responses
        snprintf(cache_headers, sizeof(cache_headers), "Cache-Control: no-store\r\n");
    }

    // GZIP compression if supported and enabled
    char* compressed_body = NULL;
    size_t compressed_size = 0;
    int using_compression = 0;
    
    if (config->enable_gzip && 
        response->body && 
        response->content_length > config->gzip_min_size &&
        response->status_code == HTTP_OK &&
        client_request != NULL) {
        
        // Check if content type is compressible
        int compressible = 0;
        if (response->content_type) {
            if (strncmp(response->content_type, "text/", 5) == 0 || 
                strncmp(response->content_type, "application/json", 16) == 0 ||
                strncmp(response->content_type, "application/javascript", 22) == 0 ||
                strncmp(response->content_type, "application/xml", 15) == 0 ||
                strncmp(response->content_type, "application/x-javascript", 24) == 0) {
                compressible = 1;
            }
        }
        
        // Check if client accepts gzip
        if (compressible && client_accepts_gzip(client_request->accept_encoding)) {
            if (gzip_compress_data(response->body, response->content_length, 
                                  &compressed_body, &compressed_size) == 0) {
                using_compression = 1;
                log_message(LOG_DEBUG, "Using GZIP compression for response (%zu -> %zu bytes)",
                          response->content_length, compressed_size);
            }
        }
    }

    // Build response header
    char header[4096];
    int header_len = snprintf(header, sizeof(header),
                             "HTTP/1.1 %d %s\r\n"
                             "Server: CServer/1.0\r\n"
                             "Date: %s\r\n"
                             "Content-Type: %s\r\n"
                             "Content-Length: %zu\r\n"
                             "%s"  // Include cache headers
                             "%s"  // Add Content-Encoding if compressed
                             "Connection: %s\r\n"
                             "\r\n",
                             response->status_code, response->status_text,
                             time_buffer,
                             response->content_type,
                             using_compression ? compressed_size : response->content_length,
                             cache_headers,
                             using_compression ? "Content-Encoding: gzip\r\n" : "",
                             response->keep_alive ? "keep-alive" : "close");

    // Send header
    send(client_socket, header, header_len, 0);

    // Send body if present
    if (response->body && response->content_length > 0) {
        if (using_compression) {
            send(client_socket, compressed_body, compressed_size, 0);
            free(compressed_body);
        } else {
            send(client_socket, response->body, response->content_length, 0);
        }
    }
}

// Log message with level and timestamp
/**
 * @brief Logs a message with the specified severity level
 * 
 * This function logs a message with a timestamp and severity level.
 * It's thread-safe and writes to both the log file and stderr.
 *
 * @param level The severity level (LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR)
 * @param format A printf-style format string
 * @param ... Variable arguments for the format string
 */
void log_message(int level, const char* format, ...) {
    if (level < config->log_level) {
        return;
    }

    // Format current time
    char time_buffer[100];
    time_t now = time(NULL);
    struct tm* time_info = localtime(&now);
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", time_info);

    // Format log level
    const char* level_str;
    switch (level) {
        case LOG_DEBUG:
            level_str = "DEBUG";
            break;
        case LOG_INFO:
            level_str = "INFO";
            break;
        case LOG_WARN:
            level_str = "WARN";
            break;
        case LOG_ERROR:
            level_str = "ERROR";
            break;
        default:
            level_str = "UNKNOWN";
    }

    // Format message
    va_list args;
    va_start(args, format);
    char message[4096];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Lock for thread safety
    pthread_mutex_lock(&log_mutex);

    // Write to log file
    if (log_file) {
        fprintf(log_file, "[%s] [%s] %s\n", time_buffer, level_str, message);
        fflush(log_file);
    }

    // Write to stderr
    fprintf(stderr, "[%s] [%s] %s\n", time_buffer, level_str, message);

    // Unlock
    pthread_mutex_unlock(&log_mutex);
}

/**
 * @brief Checks if a client is rate limited
 * 
 * This function checks if a client IP address has exceeded the configured
 * rate limit threshold using a sliding window approach for more accurate rate limiting.
 * 
 * The function maintains a hash table of client IPs with a circular buffer of request timestamps.
 * For each new request, it:
 * 1. Counts requests within the configured time window
 * 2. Compares against the maximum allowed requests
 * 3. If below limit, records the new request timestamp
 * 4. If at or above limit, rejects the request
 *
 * @param client_ip The client IP address to check
 * @return 1 if client is rate limited (exceeds limit), 0 if client is within rate limit
 */
int check_rate_limit(const char* client_ip) {
    // If rate limiting is disabled, always allow
    if (!config->enable_rate_limit) {
        return 0;
    }
    
    // Initialize rate limiting on first use
    if (!rate_limit_initialized) {
        init_rate_limiting();
    }
    
    // Find the client's entry in the rate limit table
    unsigned int hash = ip_hash(client_ip);
    rate_limit_entry_t* entry = &rate_limit_table[hash];
    
    // Lock the entry for thread safety
    pthread_mutex_lock(&entry->mutex);
    
    // If this is a new client or hash collision with different IP, initialize the entry
    if (entry->count == 0 || strcmp(entry->ip, client_ip) != 0) {
        strncpy(entry->ip, client_ip, sizeof(entry->ip) - 1);
        entry->count = 0;
        entry->current_index = 0;
    }
    
    // Get current time
    time_t now = time(NULL);
    
    // Remove timestamps older than the configured interval
    time_t cutoff = now - config->rate_limit_interval;
    int i, valid_count = 0;
    
    // Count recent requests within the time window
    for (i = 0; i < entry->count; i++) {
        if (entry->timestamps[i] >= cutoff) {
            valid_count++;
        }
    }
    
    // If we're at or over the limit, rate limit this client
    int is_limited = (valid_count >= config->rate_limit_max);
    
    // If not limited, record this request
    if (!is_limited) {
        // Add the current timestamp
        entry->timestamps[entry->current_index] = now;
        
        // Update indices
        entry->current_index = (entry->current_index + 1) % 1000;
        if (entry->count < 1000) {
            entry->count++;
        }
    }
    
    // Unlock the entry
    pthread_mutex_unlock(&entry->mutex);
    
    if (is_limited) {
        log_message(LOG_INFO, "Rate limit applied to client %s: %d requests in %d seconds", 
                  client_ip, valid_count, config->rate_limit_interval);
    }
    
    return is_limited;
}
