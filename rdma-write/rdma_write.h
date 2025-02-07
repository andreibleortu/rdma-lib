/**
 * @file rdma_write.h
 * @brief RDMA write operation interface
 *
 * Defines the interface for one-sided RDMA write operations.
 * Implements message passing using RDMA write with immediate data
 * to notify the receiver of message arrival.
 */

#ifndef RDMA_WRITE_H
#define RDMA_WRITE_H

#include "../common.h"

/**
 * @brief Function declarations for write mode client and server
 */
DEFINE_RDMA_MODE_HEADER(rw)

/**
 * @brief Posts an RDMA write operation
 *
 * @param config RDMA configuration structure
 * @param message Message to write to remote buffer
 * @param remote_info Remote QP information containing keys and address
 *
 * Uses RDMA write with immediate data to:
 * 1. Write message to remote buffer
 * 2. Signal completion using immediate data
 * 3. Include message length in immediate data
 */
void rw_post_write(struct config_t *config, const char *message, struct qp_info_t *remote_info);

/**
 * @brief Initializes and runs the RDMA write server
 *
 * @return int 0 on success, -1 on failure
 *
 * Server operation:
 * 1. Sets up RDMA resources with write permissions
 * 2. Waits for and processes incoming write requests
 * 3. Handles cleanup on shutdown
 */
int rw_run_server(void);

/**
 * @brief Initializes and runs the RDMA write client
 *
 * @param server_name Hostname or IP address of the server
 * @return int 0 on success, -1 on failure
 *
 * Client operation:
 * 1. Connects to specified server
 * 2. Provides interactive interface for message input
 * 3. Performs RDMA write operations
 * 4. Handles cleanup on exit
 */
int rw_run_client(const char *server_name);

#endif // RDMA_WRITE_H
