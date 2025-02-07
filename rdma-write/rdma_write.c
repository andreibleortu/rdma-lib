/**
 * @file rdma_write.c
 * @brief RDMA write operation implementation
 *
 * Implements one-sided RDMA write operations allowing direct memory writes
 * to remote buffers. Features:
 * - Message passing using RDMA write with immediate data
 * - Automatic notification of write completion
 * - Length information passed via immediate data
 */

#include "../common.h"

/**
 * @brief Posts an RDMA write operation with immediate data
 *
 * @param config RDMA configuration structure
 * @param message Message to write to remote buffer
 * @param remote_info Remote QP information containing address and keys
 *
 * The immediate data contains the length of the message being written,
 * allowing the receiver to know how many bytes were received without
 * additional communication.
 */
static void rw_post_write(struct config_t *config, const char *message, struct qp_info_t *remote_info)
{
    size_t length = strlen(message) + 1;  // Include null terminator
    post_operation(config, OP_WRITE, message, remote_info, length);
}

/**
 * @brief Main server event loop for write operations
 *
 * @param config RDMA configuration structure
 *
 * Server operation sequence:
 * 1. Posts receive request for immediate data
 * 2. Waits for completion event
 * 3. Extracts message length from immediate data
 * 4. Processes received message
 * 5. Repeats for next message
 */
static void rw_server_loop(struct config_t *config)
{
    while (1) {
        // Post a receive work request to get immediate data
        // This is necessary because RDMA Write with immediate requires a receive queue entry
        post_receive(config);
        
        // Wait for completion by polling the completion queue
        struct ibv_wc wc;
        while (ibv_poll_cq(config->cq, 1, &wc) == 0)
            ;
            
        // Check completion status
        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "Completion error: %s\n", ibv_wc_status_str(wc.status));
            continue;
        }

        // Extract message length from immediate data (sent in network byte order)
        uint32_t received_len = ntohl(wc.imm_data);
        printf("Received (%u bytes): %s\n", received_len, (char *)config->buf);
        fflush(stdout);
    }
}

/**
 * @brief Initializes and runs the RDMA write server
 *
 * @return int 0 on success, -1 on failure
 *
 * Server initialization sequence:
 * 1. Sets up RDMA resources with write permissions
 * 2. Enters server loop for handling write requests
 * 3. Performs cleanup on exit
 */
int rw_run_server(void)
{
    struct config_t config = {};
    
    // Setup RDMA connection in Write mode
    if (setup_rdma_connection(&config, NULL, MODE_WRITE, NULL) != RDMA_SUCCESS) {
        return -1;
    }
    
    printf("Write Server ready.\n");
    rw_server_loop(&config);
    
    cleanup_resources(&config);
    return 0;
}

/**
 * @brief Initializes and runs the RDMA write client
 *
 * @param server_name Hostname or IP address of the server
 * @return int 0 on success, -1 on failure
 *
 * Client operation sequence:
 * 1. Establishes connection to server
 * 2. Enters interactive loop where user can:
 *    - Input messages to send
 *    - Perform RDMA write operations
 *    - Receive completion notifications
 * 3. Handles cleanup on exit
 */
int rw_run_client(const char *server_name)
{
    struct config_t config = {};
    struct qp_info_t remote_info;
    
    // Setup RDMA connection to server in Write mode
    if (setup_rdma_connection(&config, server_name, MODE_WRITE, &remote_info) != RDMA_SUCCESS) {
        return -1;
    }
    
    printf("Connected to server. Enter messages (Ctrl+D to stop):\n");
    
    // Main input loop
    char input[MAX_BUFFER_SIZE];
    while (fgets(input, MAX_BUFFER_SIZE, stdin)) {
        // Remove trailing newline if present
        size_t len = strlen(input);
        if (input[len - 1] == '\n') {
            input[--len] = '\0';
        }
        
        if (len == 0) continue;
        
        // Perform RDMA Write with immediate data
        rw_post_write(&config, input, &remote_info);
        
        // Wait for the write operation to complete
        wait_completion(&config);
        
        printf("Message sent successfully\n");
    }

    cleanup_resources(&config);
    return 0;
}
