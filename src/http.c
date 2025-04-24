#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>
#include "http.h"
#include "server.h"

// Get the MIME type for a file based on its extension
const char* get_mime_type(const char* file_path) {
    const char* ext = strrchr(file_path, '.');
    if (!ext) {
        return MIME_BINARY;
    }

    ext++; // Skip the dot

    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) {
        return MIME_HTML;
    } else if (strcasecmp(ext, "txt") == 0) {
        return MIME_TEXT;
    } else if (strcasecmp(ext, "css") == 0) {
        return MIME_CSS;
    } else if (strcasecmp(ext, "js") == 0) {
        return MIME_JS;
    } else if (strcasecmp(ext, "json") == 0) {
        return MIME_JSON;
    } else if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) {
        return MIME_JPEG;
    } else if (strcasecmp(ext, "png") == 0) {
        return MIME_PNG;
    } else if (strcasecmp(ext, "gif") == 0) {
        return MIME_GIF;
    } else if (strcasecmp(ext, "svg") == 0) {
        return MIME_SVG;
    }

    return MIME_BINARY;
}

// Get the status text for a HTTP status code
const char* get_status_text(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

// Build an HTTP response
void build_http_response(http_response_t* response, int status_code, const char* content_type, const char* body, size_t content_length) {
    response->status_code = status_code;
    response->status_text = strdup(get_status_text(status_code));
    response->content_type = strdup(content_type);
    
    if (body && content_length > 0) {
        response->body = malloc(content_length);
        if (response->body) {
            memcpy(response->body, body, content_length);
            response->content_length = content_length;
        } else {
            response->content_length = 0;
        }
    } else {
        response->body = NULL;
        response->content_length = 0;
    }
}

// Read file content
int read_file_content(const char* file_path, char** content, size_t* content_length) {
    struct stat st;
    if (stat(file_path, &st) < 0) {
        return -1;
    }

    *content_length = st.st_size;
    *content = malloc(*content_length);
    if (!*content) {
        return -1;
    }

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        free(*content);
        *content = NULL;
        *content_length = 0;
        return -1;
    }

    ssize_t bytes_read = read(fd, *content, *content_length);
    close(fd);

    if (bytes_read < 0 || (size_t)bytes_read != *content_length) {
        free(*content);
        *content = NULL;
        *content_length = 0;
        return -1;
    }

    return 0;
}

// Build a JSON response
void build_json_response(http_response_t* response, int status_code, const char* json_data) {
    build_http_response(response, status_code, MIME_JSON, json_data, strlen(json_data));
}

// Check if client accepts GZIP encoding
int client_accepts_gzip(const char* accept_encoding) {
    if (!accept_encoding) {
        return 0;
    }
    
    return (strstr(accept_encoding, "gzip") != NULL);
}

// GZIP compress data
int gzip_compress_data(const char* data, size_t data_size, char** compressed_data, size_t* compressed_size) {
    if (!data || data_size == 0 || !compressed_data || !compressed_size) {
        return -1;
    }
    
    // Allocate memory for compressed data (worst case: input size + overhead)
    *compressed_size = data_size + 32;  // Add some overhead for GZIP header/footer
    *compressed_data = (char*)malloc(*compressed_size);
    if (!*compressed_data) {
        log_message(LOG_ERROR, "Failed to allocate memory for GZIP compression");
        return -1;
    }
    
    // Initialize ZLIB stream
    z_stream z;
    memset(&z, 0, sizeof(z));
    
    // Use GZIP format
    if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        free(*compressed_data);
        *compressed_data = NULL;
        log_message(LOG_ERROR, "Failed to initialize ZLIB for GZIP compression");
        return -1;
    }
    
    // Set input data
    z.next_in = (Bytef*)data;
    z.avail_in = data_size;
    
    // Set output buffer
    z.next_out = (Bytef*)*compressed_data;
    z.avail_out = *compressed_size;
    
    // Compress data
    int result = deflate(&z, Z_FINISH);
    deflateEnd(&z);
    
    if (result != Z_STREAM_END) {
        free(*compressed_data);
        *compressed_data = NULL;
        log_message(LOG_ERROR, "GZIP compression failed");
        return -1;
    }
    
    // Set actual compressed size
    *compressed_size = z.total_out;
    
    // Optionally resize buffer to actual size (could skip for efficiency)
    char* resized = (char*)realloc(*compressed_data, *compressed_size);
    if (resized) {
        *compressed_data = resized;
    }
    
    log_message(LOG_DEBUG, "GZIP compression: %zu bytes -> %zu bytes (%.1f%%)", 
               data_size, *compressed_size, (1.0 - (*compressed_size / (float)data_size)) * 100);
    
    return 0;
}
