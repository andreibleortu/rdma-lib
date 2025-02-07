/**
 * @file rdma.c
 * @brief Main entry point for the RDMA communication system
 *
 * Implements the main program logic for different RDMA communication modes:
 * - Send-receive (traditional two-sided communication)
 * - RDMA write (one-sided write operations)
 * - RDMA read (one-sided read operations)
 * - Lambda (remote function execution)
 */

#include "common.h"
#include "send-receive/send_receive.h"
#include "rdma-write/rdma_write.h"
#include "rdma-read/rdma_read.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>

/**
 * @brief Displays program usage instructions
 *
 * Prints detailed help message showing supported operation modes
 * and their corresponding command-line arguments for both server
 * and client modes.
 */
void print_usage() {
    printf("Usage:\n");
    printf("  Server mode:\n");
    printf("    ./rdma send              - Run send-receive server\n");
    printf("    ./rdma write             - Run RDMA write server\n");
    printf("    ./rdma read              - Run RDMA read server\n");
    printf("    ./rdma lambda            - Run Lambda server\n");
    printf("\n");
    printf("  Client mode:\n");
    printf("    ./rdma send <host>       - Run send-receive client\n");
    printf("    ./rdma write <host>      - Run RDMA write client\n");
    printf("    ./rdma read <host>       - Run RDMA read client\n");
    printf("    ./rdma lambda <host>     - Run Lambda client\n");
}

/**
 * @brief Configures signal handlers for graceful shutdown
 *
 * Sets up handlers for SIGINT and SIGTERM to ensure proper cleanup of RDMA
 * resources when the program is terminated. Uses sigaction for reliable
 * signal handling across different UNIX implementations.
 */
static void setup_signal_handlers(void) {
    struct sigaction sa = {
        .sa_handler = signal_handler,
        .sa_flags = 0
    };
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

// External reference to global configuration pointer used by signal handlers
extern struct config_t *global_config;

/**
 * @brief Program entry point
 *
 * @param argc Number of command line arguments
 * @param argv Array of command line argument strings
 * @return int 0 on success, non-zero on error
 *
 * Initializes and runs the RDMA communication system based on command line arguments.
 * Supports multiple operation modes and handles both client and server roles.
 */
int main(int argc, char *argv[]) {
    // Validate command line arguments
    if (argc < 2 || argc > 3) {
        print_usage();
        return 1;
    }

    // Initialize signal handlers for graceful shutdown
    setup_signal_handlers();

    // Initialize global configuration
    struct config_t config = {};
    global_config = &config;  // Store config pointer for signal handlers

    // Parse command line arguments
    const char *mode = argv[1];
    const char *host = argc > 2 ? argv[2] : NULL;  // NULL for server mode
    rdma_mode_t rdma_mode;

    // Map string mode argument to internal mode enumeration
    if (strcmp(mode, "send") == 0) {
        rdma_mode = MODE_SEND_RECV;
    } else if (strcmp(mode, "write") == 0) {
        rdma_mode = MODE_WRITE;
    } else if (strcmp(mode, "read") == 0) {
        rdma_mode = MODE_READ;
    } else if (strcmp(mode, "lambda") == 0) {
        rdma_mode = MODE_LAMBDA;
    } else {
        printf("Unknown mode: %s\n", mode);
        print_usage();
        return 1;
    }

    // Print detailed configuration information
    printf("\n=== RDMA Communication Program Started ===\n");
    printf("Mode: %s (%s)\n", mode, host ? "Client" : "Server");
    printf("Configuration:\n");
    printf("  Buffer size: %d bytes\n", MAX_BUFFER_SIZE);
    printf("  IB port: %d\n", IB_PORT);
    printf("  GID index: %d\n", GID_INDEX);
    printf("  TCP port: %d\n", TCP_PORT);
    fflush(stdout);

    // Execute appropriate mode-specific implementation
    int result;
    if (host) {
        // Client mode: Connect to specified host
        printf("Connecting to %s...\n", host);
        result = run_client(host, rdma_mode);
        if (result != 0) {
            fprintf(stderr, "Client operation failed with error code: %d\n", result);
        }
    } else {
        // Server mode: Listen for incoming connections
        printf("Starting server...\n");
        result = run_server(rdma_mode);
        if (result != 0) {
            fprintf(stderr, "Server operation failed with error code: %d\n", result);
        }
    }
    
    // Cleanup global state
    global_config = NULL;
    return result;
}
