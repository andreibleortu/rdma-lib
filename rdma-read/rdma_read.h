/**
 * @file rdma_read.h
 * @brief RDMA read operation interface
 *
 * Defines the interface for performing one-sided RDMA read operations.
 * Supports reading arbitrary ranges of data from remote memory without
 * involving the remote CPU.
 */

#ifndef RDMA_READ_H
#define RDMA_READ_H

#include "../common.h"

// Define rd_run_server and rd_run_client function declarations
DEFINE_RDMA_MODE_HEADER(rd)

#endif // RDMA_READ_H
