/**
 * @file http.h
 * @brief HTTP protocol handling for CServer
 *
 * This header file contains definitions for HTTP status codes, methods, MIME types,
 * and function declarations for HTTP protocol handling in the CServer web server.
 *
 * This header file defines HTTP protocol constants, utilities for MIME type
 * detection, and functions for reading file content and building HTTP responses.
 *
 * @author Your Name
 * @date April 24, 2025
 */

#ifndef HTTP_H
#define HTTP_H

#include "server.h"
#include <stdlib.h>

// HTTP status codes
/**
 * @brief HTTP status code: 200 OK
 * Standard response for successful HTTP requests
 */
#define HTTP_OK 200

/**
 * @brief HTTP status code: 400 Bad Request
 * The server cannot process the request due to client error
 */
#define HTTP_BAD_REQUEST 400

/**
 * @brief HTTP status code: 404 Not Found
 * The requested resource could not be found
 */
#define HTTP_NOT_FOUND 404

/**
 * @brief HTTP status code: 405 Method Not Allowed
 * The request method is not supported for the requested resource
 */
#define HTTP_METHOD_NOT_ALLOWED 405

/**
 * @brief HTTP status code: 500 Internal Server Error
 * Generic server error message when no specific message is suitable
 */
#define HTTP_INTERNAL_SERVER_ERROR 500

// HTTP methods
/**
 * @brief HTTP GET method
 * Request data from a specified resource
 */
#define HTTP_METHOD_GET "GET"

/**
 * @brief HTTP POST method
 * Submit data to be processed to a specified resource
 */
#define HTTP_METHOD_POST "POST"

/**
 * @brief HTTP HEAD method
 * Same as GET but returns only HTTP headers and no document body
 */
#define HTTP_METHOD_HEAD "HEAD"

// MIME types
/**
 * @brief MIME type for HTML documents
 */
#define MIME_HTML "text/html; charset=UTF-8"

/**
 * @brief MIME type for plain text documents
 */
#define MIME_TEXT "text/plain; charset=UTF-8"

/**
 * @brief MIME type for JSON data
 */
#define MIME_JSON "application/json; charset=UTF-8"

/**
 * @brief MIME type for CSS stylesheets
 */
#define MIME_CSS "text/css; charset=UTF-8"

/**
 * @brief MIME type for JavaScript files
 */
#define MIME_JS "application/javascript; charset=UTF-8"

/**
 * @brief MIME type for JPEG images
 */
#define MIME_JPEG "image/jpeg"

/**
 * @brief MIME type for PNG images
 */
#define MIME_PNG "image/png"

/**
 * @brief MIME type for GIF images
 */
#define MIME_GIF "image/gif"

/**
 * @brief MIME type for SVG images
 */
#define MIME_SVG "image/svg+xml"

/**
 * @brief MIME type for generic binary data
 */
#define MIME_BINARY "application/octet-stream"

// Function declarations
/**
 * @brief Determines the MIME type based on a file's extension
 *
 * This function examines the extension of the provided file path
 * and returns the appropriate MIME type string for HTTP Content-Type headers.
 *
 * @param file_path The path to the file
 * @return The MIME type string corresponding to the file's extension
 */
const char* get_mime_type(const char* file_path);

/**
 * @brief Gets the standard text description for an HTTP status code
 *
 * This function returns the standard text description associated with
 * an HTTP status code (e.g., "OK" for 200, "Not Found" for 404).
 *
 * @param status_code The HTTP status code
 * @return The standard text description for the status code
 */
const char* get_status_text(int status_code);

/**
 * @brief Builds an HTTP response structure
 *
 * This function populates an HTTP response structure with the specified
 * status code, content type, body, and content length.
 *
 * @param response Pointer to the response structure to populate
 * @param status_code The HTTP status code for the response
 * @param content_type The MIME type for the response content
 * @param body The response body data
 * @param content_length The length of the response body in bytes
 */
void build_http_response(http_response_t* response, int status_code, const char* content_type, const char* body, size_t content_length);

/**
 * @brief Reads the contents of a file into memory
 *
 * This function reads the entire contents of a file into dynamically allocated memory.
 * The caller is responsible for freeing the allocated memory when it is no longer needed.
 *
 * @param file_path The path to the file to read
 * @param content Pointer to a char pointer that will receive the file contents
 * @param content_length Pointer to a size_t variable that will receive the content length
 * @return 0 on success, -1 on failure
 */
int read_file_content(const char* file_path, char** content, size_t* content_length);

/**
 * @brief Generate a JSON response for API endpoints
 * 
 * This function generates a JSON response for API endpoints with the given
 * status code and JSON data.
 * 
 * @param response Pointer to the HTTP response structure to fill
 * @param status_code HTTP status code
 * @param json_data JSON data as a string
 */
void build_json_response(http_response_t* response, int status_code, const char* json_data);

/**
 * @brief Compresses data using GZIP compression
 * 
 * @param data Input data to compress
 * @param data_size Size of input data
 * @param compressed_data Output pointer to compressed data (caller must free)
 * @param compressed_size Size of compressed data
 * @return 0 on success, -1 on failure
 */
int gzip_compress_data(const char* data, size_t data_size, char** compressed_data, size_t* compressed_size);

/**
 * @brief Checks if a client accepts GZIP encoding
 * 
 * @param accept_encoding The Accept-Encoding header value
 * @return 1 if client accepts GZIP, 0 otherwise
 */
int client_accepts_gzip(const char* accept_encoding);

#endif // HTTP_H
