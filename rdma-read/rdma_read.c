/**
 * @file rdma_read.c
 * @brief RDMA read operation implementation
 *
 * Implements one-sided RDMA read operations allowing direct memory access
 * to remote buffers without involving the remote CPU. Supports:
 * - Reading arbitrary ranges from remote memory
 * - Interactive selection of data ranges
 * - Efficient memory transfer using RDMA hardware
 */

#include "../common.h"

/**
 * @brief Posts an RDMA read operation
 *
 * @param config RDMA configuration structure
 * @param remote_offset Offset in the remote buffer to read from
 * @param length Number of bytes to read
 * @param remote_info Remote QP information containing address and keys
 *
 * Adjusts the remote address based on the offset and posts a read operation
 * to transfer data directly from remote memory to local buffer.
 */
static void rd_post_read(struct config_t *config, uint64_t remote_offset, size_t length, struct qp_info_t *remote_info)
{
    remote_info->addr += remote_offset;
    post_operation(config, OP_READ, NULL, remote_info, length);
    remote_info->addr -= remote_offset;  // Reset the address for future operations
}

/**
 * @brief Main server loop for read operations
 *
 * @param config RDMA configuration structure
 *
 * Server operation sequence:
 * 1. Prompts for input text to store in buffer
 * 2. Stores text in registered memory region
 * 3. Makes memory available for remote read operations
 * 4. Waits indefinitely for client read requests
 */
static void rd_server_loop(struct config_t *config)
{
    printf("Enter text to store: ");
    fflush(stdout);
    
    char input[MAX_BUFFER_SIZE];
    if (fgets(input, MAX_BUFFER_SIZE, stdin)) {
        size_t len = strlen(input);
        if (input[len - 1] == '\n')
            input[--len] = '\0';
        memcpy(config->buf, input, len + 1);
        printf("Waiting for client read requests...\n");
        while (1)
            sleep(1);
    }
}

/**
 * @brief Initializes and runs the RDMA read server
 *
 * @return int 0 on success, -1 on failure
 *
 * Server initialization sequence:
 * 1. Sets up RDMA resources with read permissions
 * 2. Initializes memory buffer with user input
 * 3. Enters server loop to handle read requests
 */
int rd_run_server(void)
{
    struct config_t config = {};
    
    if (setup_rdma_connection(&config, NULL, MODE_READ, NULL) != RDMA_SUCCESS) {
        return -1;
    }
    
    printf("Read Server ready.\n");
    rd_server_loop(&config);
    
    cleanup_resources(&config);
    return 0;
}

/**
 * @brief Initializes and runs the RDMA read client
 *
 * @param server_name Hostname or IP address of the server
 * @return int 0 on success, -1 on failure
 *
 * Client operation sequence:
 * 1. Establishes connection to server
 * 2. Enters interactive loop where user can:
 *    - Specify start and end positions to read
 *    - Read specified range using RDMA read
 *    - View retrieved data
 * 3. Validates input ranges
 * 4. Performs RDMA read operations
 */
int rd_run_client(const char *server_name)
{
    struct config_t config = {};
    struct qp_info_t remote_info;
    
    if (setup_rdma_connection(&config, server_name, MODE_READ, &remote_info) != RDMA_SUCCESS) {
        return -1;
    }
    
    printf("Connected to server.\n");
    printf("Enter character range to read (format: start_pos end_pos):\n");
    printf("Example: 0 5 to read first 6 characters\n");
    
    char input[MAX_BUFFER_SIZE];
    while (fgets(input, MAX_BUFFER_SIZE, stdin)) {
        int start, end;
        if (sscanf(input, "%d %d", &start, &end) == 2) {
            if (start < 0 || end < start || end >= MAX_BUFFER_SIZE) {
                printf("Invalid range. start must be >= 0, end must be >= start and < %d\n", MAX_BUFFER_SIZE);
                continue;
            }
            size_t read_len = end - start + 1;
            rd_post_read(&config, start, read_len, &remote_info);
            wait_completion(&config);
            printf("Read data (%zu bytes from position %d): %.*s\n", 
                   read_len, start, (int)read_len, (char *)config.buf);
        } else {
            printf("Invalid input. Please enter two numbers: start_pos end_pos\n");
        }
    }

    cleanup_resources(&config);
    return 0;
}
