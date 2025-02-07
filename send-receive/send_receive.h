/**
 * @file send_receive.h
 * @brief Two-sided RDMA communication interface
 *
 * Defines the interface for traditional two-sided RDMA communication
 * using send/receive operations. Implements a request-response pattern
 * where each message is acknowledged by the receiver.
 */

#ifndef SEND_RECEIVE_H
#define SEND_RECEIVE_H

#include "../common.h"

/**
 * @brief Function declarations for send-receive mode
 */
DEFINE_RDMA_MODE_HEADER(sr)

/**
 * @brief Posts a send operation
 *
 * @param config RDMA configuration structure
 * @param message Null-terminated message to send
 *
 * Process:
 * 1. Copies message to registered memory region
 * 2. Posts send work request
 * 3. Sets up completion notification
 */
void sr_post_send(struct config_t *config, const char *message);

/**
 * @brief Initializes and runs the send-receive server
 *
 * @return int 0 on success, -1 on failure
 *
 * Server operation:
 * 1. Sets up RDMA resources
 * 2. Waits for client connection
 * 3. Enters message processing loop:
 *    - Receives messages
 *    - Sends acknowledgments
 * 4. Handles cleanup on shutdown
 */
int sr_run_server(void);

/**
 * @brief Initializes and runs the send-receive client
 *
 * @param server_name Hostname or IP address of the server
 * @return int 0 on success, -1 on failure
 *
 * Client operation:
 * 1. Connects to specified server
 * 2. Enters interactive loop:
 *    - Gets user input
 *    - Sends messages
 *    - Waits for acknowledgments
 * 3. Handles cleanup on exit
 */
int sr_run_client(const char *server_name);

#endif // SEND_RECEIVE_H
