# CServer - Production-Level Web Server in C

CServer is a high-performance, multithreaded HTTP web server implemented in C, designed for production environments. It features a thread pool architecture for handling concurrent connections, robust error handling, and comprehensive logging capabilities.

## Features

- **Performance-oriented architecture**: Non-blocking I/O with thread pool for concurrent connection handling
- **HTTP/1.1 compliant**: Supports standard HTTP methods and features like keep-alive connections
- **Static file serving**: Serves files with proper MIME type detection
- **Configurable**: Command-line options for port, thread count, document root, etc.
- **Robust logging**: Multiple severity levels with timestamped entries
- **Resource management**: Proper cleanup of resources to prevent memory leaks
- **Signal handling**: Graceful shutdown on SIGINT/SIGTERM

## Building the Server

```bash
# Clone the repository
git clone https://github.com/your-username/cserver.git
cd cserver

# Build the server
make
```

## Running the Server

```bash
# Basic usage with default options
./bin/cserver

# Custom configuration
./bin/cserver --port 8080 --threads 32 --root ./www
```

## Command-Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--port`, `-p` | Port to listen on | 8080 |
| `--backlog`, `-b` | Connection backlog size | 128 |
| `--threads`, `-t` | Thread pool size | 16 |
| `--root`, `-r` | Document root directory | ./www |
| `--keepalive`, `-k` | Keep-alive timeout (seconds) | 5 |
| `--log`, `-l` | Log file path | logs/server.log |
| `--verbose`, `-v` | Increase logging verbosity | - |
| `--help`, `-h` | Display help message | - |

## Project Structure

```
cserver/
├── bin/             # Compiled binaries
├── conf/            # Configuration files
├── include/         # Header files
│   ├── http.h       # HTTP protocol definitions
│   ├── server.h     # Server API definitions
│   └── thread_pool.h # Thread pool implementation
├── lib/             # External libraries
├── logs/            # Server logs
├── obj/             # Object files (created during build)
├── src/             # Source code
│   ├── http.c       # HTTP implementation
│   ├── main.c       # Entry point
│   ├── server.c     # Server implementation
│   └── thread_pool.c # Thread pool implementation
├── Makefile         # Build configuration
└── README.md        # This file
```

## Architecture

The server uses a thread pool architecture where:

1. The main thread accepts new connections
2. Connections are passed to a worker thread from the pool
3. The worker thread handles the HTTP request/response cycle
4. Results are sent back to the client

This design allows for efficient handling of multiple concurrent connections without the overhead of creating a new thread for each client.

## Performance Considerations

- Thread pool size should be adjusted based on the number of CPU cores and expected load
- The server uses non-blocking I/O where appropriate to maximize throughput
- Keep-alive connections reduce the overhead of establishing new TCP connections

## Security Notes

This server implements basic security measures but may need additional hardening for production use:

- Add input validation for all client-provided data
- Implement rate limiting to prevent DoS attacks
- Consider adding TLS/SSL support
- Set up proper file permissions for served content

## License

This project is licensed under the MIT License - see the LICENSE file for details.
