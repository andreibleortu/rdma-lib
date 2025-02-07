/**
 * @file send_receive.c
 * @brief Two-sided RDMA communication implementation
 *
 * Implements traditional two-sided RDMA communication using send/receive operations.
 * Features:
 * - Interactive message exchange
 * - Request-response pattern
 * - Acknowledgment of received messages
 */

#include "../common.h"

/**
 * @brief Posts a send operation
 *
 * @param config RDMA configuration structure
 * @param message Null-terminated message to send
 *
 * Posts a send operation after copying the message to the registered
 * memory region. Includes message length in the operation.
 */
void sr_post_send(struct config_t *config, const char *message)
{
    post_operation(config, OP_SEND, message, NULL, strlen(message) + 1);
}

/**
 * @brief Main server event loop
 *
 * @param config RDMA configuration structure
 *
 * Server operation sequence:
 * 1. Posts receive request for next message
 * 2. Waits for receive completion
 * 3. Processes received message
 * 4. Sends acknowledgment
 * 5. Waits for send completion
 * 6. Repeats from step 1
 */
static void sr_server_loop(struct config_t *config)
{
    while (1) {
        // Post receive operation to prepare for incoming message
        post_receive(config);
        wait_completion(config);
        printf("Received: %s\n", (char *)config->buf);
        fflush(stdout);
        
        // Send acknowledgment back to client
        sr_post_send(config, "ACK");
        wait_completion(config);
    }
}

/**
 * @brief Initializes and runs the send-receive server
 *
 * @return int 0 on success, -1 on failure
 *
 * Server initialization sequence:
 * 1. Sets up RDMA resources in send-receive mode
 * 2. Waits for client connection
 * 3. Enters message handling loop
 * 4. Performs cleanup on exit
 */
int sr_run_server(void)
{
    struct config_t config = {};
    
    // Initialize RDMA resources and wait for client connection
    if (setup_rdma_connection(&config, NULL, MODE_SEND_RECV, NULL) != RDMA_SUCCESS) {
        return -1;
    }
    
    printf("Send-Receive Server ready.\n");
    sr_server_loop(&config);
    
    cleanup_resources(&config);
    return 0;
}

/**
 * @brief Initializes and runs the send-receive client
 *
 * @param server_name Hostname or IP address of the server
 * @return int 0 on success, -1 on failure
 *
 * Client operation sequence:
 * 1. Establishes connection to server
 * 2. Enters interactive loop:
 *    - Reads user input
 *    - Sends message
 *    - Waits for acknowledgment
 * 3. Handles EOF and cleanup
 *
 * Note: Client exits on EOF (Ctrl+D) or error
 */
int sr_run_client(const char *server_name)
{
    struct config_t config = {};
    struct qp_info_t remote_info;
    
    // Setup RDMA connection to server
    if (setup_rdma_connection(&config, server_name, MODE_SEND_RECV, &remote_info) != RDMA_SUCCESS) {
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
        
        // Send message and wait for completion
        sr_post_send(&config, input);
        wait_completion(&config);
        
        // Wait for server acknowledgment
        post_receive(&config);
        wait_completion(&config);
        printf("Server acknowledged\n");
    }

    cleanup_resources(&config);
    return 0;
}
