#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include "server.h"
#include "thread_pool.h"
#include "db.h"

// Global variables
static int running = 1;
static server_config_t server_config;
static db_pool_t* db_pool = NULL;

// Signal handler
void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        log_message(LOG_INFO, "Shutdown signal received, stopping server...");
        running = 0;
        server_stop();
        
        // Cleanup database connections
        if (db_pool) {
            db_cleanup(db_pool);
            db_pool = NULL;
        }
    }
}

// Print usage information
void print_usage(const char* program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Options:\n");
    printf("  -p, --port PORT              Port to listen on (default: 8080)\n");
    printf("  -b, --backlog BACKLOG        Connection backlog size (default: 128)\n");
    printf("  -t, --threads THREADS        Thread pool size (default: 16)\n");
    printf("  -r, --root DIRECTORY         Document root directory (default: ./www)\n");
    printf("  -k, --keepalive SECONDS      Keep-alive timeout in seconds (default: 5)\n");
    printf("  -l, --log FILE               Log file path (default: logs/server.log)\n");
    printf("  -v, --verbose                Increase verbosity (can use multiple times)\n");
    printf("  --db-host HOST               PostgreSQL database host (default: localhost)\n");
    printf("  --db-port PORT               PostgreSQL database port (default: 5432)\n");
    printf("  --db-name NAME               PostgreSQL database name (default: cserver)\n");
    printf("  --db-user USER               PostgreSQL database user (default: postgres)\n");
    printf("  --db-password PASSWORD       PostgreSQL database password\n");
    printf("  --db-pool-size SIZE          Database connection pool size (default: 5)\n");
    printf("  --https                      Enable HTTPS (default: disabled)\n");
    printf("  --https-port PORT            HTTPS port (default: 8443)\n");
    printf("  --cert FILE                  SSL certificate file\n");
    printf("  --key FILE                   SSL private key file\n");
    printf("  --gzip                       Enable GZIP compression (default: disabled)\n");
    printf("  --gzip-min-size SIZE         Minimum size for GZIP compression in bytes (default: 1024)\n");
    printf("  --rate-limit                 Enable rate limiting (default: disabled)\n");
    printf("  --rate-limit-max COUNT       Maximum requests per interval (default: 100)\n");
    printf("  --rate-limit-interval SEC    Rate limit interval in seconds (default: 60)\n");
    printf("  -h, --help                   Display this help message\n");
}

// Parse command line arguments
int parse_args(int argc, char* argv[]) {
    // Set default values
    server_config.port = 8080;
    server_config.backlog = 128;
    server_config.thread_pool_size = 16;
    server_config.doc_root = strdup("./www");
    server_config.keep_alive_timeout = 5;
    server_config.log_file = strdup("logs/server.log");
    server_config.log_level = LOG_DEBUG; // Changed from LOG_INFO to LOG_DEBUG for verbose logging
    
    // Database defaults
    server_config.db_host = strdup("localhost");
    server_config.db_port = strdup("5432");
    server_config.db_name = strdup("cserver");
    server_config.db_user = strdup("postgres");
    server_config.db_password = strdup("");
    server_config.db_pool_size = 5;
    
    // HTTPS defaults
    server_config.enable_https = 0;
    server_config.https_port = 8443;
    server_config.cert_file = NULL;
    server_config.key_file = NULL;
    
    // GZIP defaults
    server_config.enable_gzip = 0;
    server_config.gzip_min_size = 1024;
    
    // Rate limiting defaults
    server_config.enable_rate_limit = 0;
    server_config.rate_limit_max = 100;
    server_config.rate_limit_interval = 60;

    // Define command line options
    static struct option long_options[] = {
        {"port",              required_argument, 0, 'p'},
        {"backlog",           required_argument, 0, 'b'},
        {"threads",           required_argument, 0, 't'},
        {"root",              required_argument, 0, 'r'},
        {"keepalive",         required_argument, 0, 'k'},
        {"log",               required_argument, 0, 'l'},
        {"verbose",           no_argument,       0, 'v'},
        {"help",              no_argument,       0, 'h'},
        {"db-host",           required_argument, 0, 1},
        {"db-port",           required_argument, 0, 2},
        {"db-name",           required_argument, 0, 3},
        {"db-user",           required_argument, 0, 4},
        {"db-password",       required_argument, 0, 5},
        {"db-pool-size",      required_argument, 0, 6},
        {"https",             no_argument,       0, 7},
        {"https-port",        required_argument, 0, 8},
        {"cert",              required_argument, 0, 9},
        {"key",               required_argument, 0, 10},
        {"gzip",              no_argument,       0, 11},
        {"gzip-min-size",     required_argument, 0, 12},
        {"rate-limit",        no_argument,       0, 13},
        {"rate-limit-max",    required_argument, 0, 14},
        {"rate-limit-interval", required_argument, 0, 15},
        {0, 0, 0, 0}
    };

    int c;
    int option_index = 0;

    while ((c = getopt_long(argc, argv, "p:b:t:r:k:l:vh", long_options, &option_index)) != -1) {
        switch (c) {
            case 'p':
                server_config.port = atoi(optarg);
                break;
            case 'b':
                server_config.backlog = atoi(optarg);
                break;
            case 't':
                server_config.thread_pool_size = atoi(optarg);
                break;
            case 'r':
                free(server_config.doc_root);
                server_config.doc_root = strdup(optarg);
                break;
            case 'k':
                server_config.keep_alive_timeout = atoi(optarg);
                break;
            case 'l':
                free(server_config.log_file);
                server_config.log_file = strdup(optarg);
                break;
            case 'v':
                if (server_config.log_level > 0) {
                    server_config.log_level--;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            // Database options
            case 1: // --db-host
                free(server_config.db_host);
                server_config.db_host = strdup(optarg);
                break;
            case 2: // --db-port
                free(server_config.db_port);
                server_config.db_port = strdup(optarg);
                break;
            case 3: // --db-name
                free(server_config.db_name);
                server_config.db_name = strdup(optarg);
                break;
            case 4: // --db-user
                free(server_config.db_user);
                server_config.db_user = strdup(optarg);
                break;
            case 5: // --db-password
                free(server_config.db_password);
                server_config.db_password = strdup(optarg);
                break;
            case 6: // --db-pool-size
                server_config.db_pool_size = atoi(optarg);
                break;
            // HTTPS options
            case 7: // --https
                server_config.enable_https = 1;
                break;
            case 8: // --https-port
                server_config.https_port = atoi(optarg);
                break;
            case 9: // --cert
                if (server_config.cert_file) free(server_config.cert_file);
                server_config.cert_file = strdup(optarg);
                break;
            case 10: // --key
                if (server_config.key_file) free(server_config.key_file);
                server_config.key_file = strdup(optarg);
                break;
            // GZIP options
            case 11: // --gzip
                server_config.enable_gzip = 1;
                break;
            case 12: // --gzip-min-size
                server_config.gzip_min_size = atoi(optarg);
                break;
            // Rate limiting options
            case 13: // --rate-limit
                server_config.enable_rate_limit = 1;
                break;
            case 14: // --rate-limit-max
                server_config.rate_limit_max = atoi(optarg);
                break;
            case 15: // --rate-limit-interval
                server_config.rate_limit_interval = atoi(optarg);
                break;
            case '?':
                return -1;
            default:
                break;
        }
    }

    return 1;
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    int parse_result = parse_args(argc, argv);
    if (parse_result <= 0) {
        if (parse_result < 0) {
            print_usage(argv[0]);
        }
        return parse_result < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    // Set up signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Initialize the server
    if (server_init(&server_config) != 0) {
        log_message(LOG_ERROR, "Failed to initialize server");
        return EXIT_FAILURE;
    }

    // Create document root directory if it doesn't exist
    char command[512];
    snprintf(command, sizeof(command), "mkdir -p %s", server_config.doc_root);
    system(command);

    // Create a default index.html if it doesn't exist
    char index_path[512];
    snprintf(index_path, sizeof(index_path), "%s/index.html", server_config.doc_root);
    FILE* file = fopen(index_path, "r");
    if (!file) {
        file = fopen(index_path, "w");
        if (file) {
            fprintf(file, "<!DOCTYPE html>\n<html>\n<head>\n");
            fprintf(file, "    <title>CServer - Production C Web Server</title>\n");
            fprintf(file, "    <meta charset=\"UTF-8\">\n");
            fprintf(file, "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n");
            fprintf(file, "    <style>\n");
            fprintf(file, "        body { font-family: Arial, sans-serif; text-align: center; padding: 50px; }\n");
            fprintf(file, "        h1 { color: #333; }\n");
            fprintf(file, "        p { color: #666; }\n");
            fprintf(file, "        .container { max-width: 800px; margin: 0 auto; }\n");
            fprintf(file, "        .features { text-align: left; margin-top: 30px; }\n");
            fprintf(file, "        .feature { margin-bottom: 15px; }\n");
            fprintf(file, "    </style>\n");
            fprintf(file, "</head>\n<body>\n");
            fprintf(file, "    <div class=\"container\">\n");
            fprintf(file, "        <h1>CServer is running!</h1>\n");
            fprintf(file, "        <p>This is a production-level C web server.</p>\n");
            fprintf(file, "        <p>Server time: <span id=\"server-time\">%s</span></p>\n", "");
            fprintf(file, "        <div class=\"features\">\n");
            fprintf(file, "            <h2>Enabled Features:</h2>\n");
            fprintf(file, "            <div class=\"feature\">✅ HTTP Caching with ETags</div>\n");
            
            // Show which features are enabled
            if (server_config.enable_https)
                fprintf(file, "            <div class=\"feature\">✅ HTTPS Support</div>\n");
            else
                fprintf(file, "            <div class=\"feature\">❌ HTTPS Support (disabled)</div>\n");
                
            if (server_config.enable_gzip)
                fprintf(file, "            <div class=\"feature\">✅ GZIP Compression</div>\n");
            else
                fprintf(file, "            <div class=\"feature\">❌ GZIP Compression (disabled)</div>\n");
                
            if (server_config.enable_rate_limit)
                fprintf(file, "            <div class=\"feature\">✅ Request Rate Limiting</div>\n");
            else
                fprintf(file, "            <div class=\"feature\">❌ Request Rate Limiting (disabled)</div>\n");
                
            fprintf(file, "            <div class=\"feature\">✅ PostgreSQL Database Support</div>\n");
            fprintf(file, "            <div class=\"feature\">✅ RESTful API Support</div>\n");
            fprintf(file, "        </div>\n");
            fprintf(file, "        <div class=\"api-demo\">\n");
            fprintf(file, "            <h2>Task Manager API Demo</h2>\n");
            fprintf(file, "            <p>Try the Task Manager API at <a href=\"/api/tasks\">/api/tasks</a></p>\n");
            fprintf(file, "        </div>\n");
            fprintf(file, "    </div>\n");
            fprintf(file, "    <script>\n");
            fprintf(file, "        setInterval(function() {\n");
            fprintf(file, "            document.getElementById('server-time').textContent = new Date().toLocaleString();\n");
            fprintf(file, "        }, 1000);\n");
            fprintf(file, "    </script>\n");
            fprintf(file, "</body>\n</html>\n");
            fclose(file);
        }
    } else {
        fclose(file);
    }
    
    // Initialize database connection pool if not disabled
    if (server_config.db_host && server_config.db_name) {
        log_message(LOG_INFO, "Initializing database connection to %s:%s/%s", 
                   server_config.db_host, server_config.db_port, server_config.db_name);
        
        db_config_t db_config;
        db_config.host = server_config.db_host;
        db_config.port = server_config.db_port;
        db_config.dbname = server_config.db_name;
        db_config.user = server_config.db_user;
        db_config.password = server_config.db_password;
        db_config.max_connections = server_config.db_pool_size;
        
        db_pool = db_init(&db_config);
        if (!db_pool) {
            log_message(LOG_WARN, "Failed to initialize database connection pool. Database features will be disabled.");
        } else {
            log_message(LOG_INFO, "Database connection pool initialized successfully");
        }
    }

    // Start the server
    log_message(LOG_INFO, "Starting server on port %d with document root '%s'", 
               server_config.port, server_config.doc_root);
    
    // Log enabled features
    if (server_config.enable_https) {
        log_message(LOG_INFO, "HTTPS enabled on port %d", server_config.https_port);
    }
    if (server_config.enable_gzip) {
        log_message(LOG_INFO, "GZIP compression enabled (min size: %d bytes)", server_config.gzip_min_size);
    }
    if (server_config.enable_rate_limit) {
        log_message(LOG_INFO, "Rate limiting enabled (%d requests per %d seconds)", 
                   server_config.rate_limit_max, server_config.rate_limit_interval);
    }
    
    if (server_start() != 0) {
        log_message(LOG_ERROR, "Failed to start server");
        return EXIT_FAILURE;
    }

    // Clean up
    free(server_config.doc_root);
    free(server_config.log_file);
    free(server_config.db_host);
    free(server_config.db_port);
    free(server_config.db_name);
    free(server_config.db_user);
    free(server_config.db_password);
    if (server_config.cert_file) free(server_config.cert_file);
    if (server_config.key_file) free(server_config.key_file);
    
    // Clean up database connection pool
    if (db_pool) {
        db_cleanup(db_pool);
    }

    return EXIT_SUCCESS;
}
