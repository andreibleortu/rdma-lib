/**
 * @file lambda.h
 * @brief Remote function execution over RDMA (Lambda) interface
 *
 * Defines structures and functions for remote code execution using RDMA.
 * Enables dynamic loading and remote execution of functions with data transfer.
 */

#ifndef LAMBDA_H
#define LAMBDA_H

#include "../common.h"
#include "../rdma-write/rdma_write.h"
#include "../rdma-read/rdma_read.h"
#include "../send-receive/send_receive.h"
#include <fcntl.h>
#include <dlfcn.h>

/**
 * @brief Maximum size constants for lambda operations
 */
#define LAMBDA_MAX_FUNCTION_NAME 128           // Maximum length of function name
#define LAMBDA_MAX_CODE_SIZE (1024 * 1024 * 3) // Maximum size of executable code (3MB)
#define LAMBDA_MAX_INPUT_SIZE (MAX_BUFFER_SIZE) // Maximum size of input data
#define LAMBDA_MAX_OUTPUT_SIZE (MAX_BUFFER_SIZE) // Maximum size of output data

/**
 * @brief Function prototype for remotely executable lambda functions
 *
 * @param input Pointer to input data buffer
 * @param input_size Size of input data in bytes
 * @param output Pointer to output data buffer
 * @param output_size Pointer to store size of output data
 * @return int 0 on success, non-zero on failure
 *
 * This is the required signature for any function that will be executed remotely.
 * The function must handle its own serialization/deserialization of input/output data.
 */
typedef int (*lambda_fn)(void* input, size_t input_size, void* output, size_t* output_size);

/**
 * @brief Metadata structure for lambda function execution
 *
 * Contains information needed to execute the function remotely:
 * - Function identification and properties
 * - Code and data size information
 * - Entry point location
 */
struct lambda_metadata {
    char function_name[LAMBDA_MAX_FUNCTION_NAME];  // Name of function to execute
    size_t code_size;                             // Size of function code in bytes
    size_t input_size;                           // Size of input data in bytes
    uint64_t entry_offset;                       // Offset to function entry point
};

/**
 * @brief Memory regions used for lambda execution
 *
 * Manages both local and RDMA-registered memory regions for:
 * - Executable code storage
 * - Input data buffering
 * - Output data storage
 */
struct lambda_memory_regions {
    void* code_region;           // Region for executable code
    void* input_region;         // Region for input data
    void* output_region;        // Region for output data
    struct ibv_mr* code_mr;    // RDMA memory region for code
    struct ibv_mr* input_mr;   // RDMA memory region for input
    struct ibv_mr* output_mr;  // RDMA memory region for output
};

/**
 * @brief Configuration structure for lambda mode
 *
 * Contains Queue Pair configurations for:
 * - Code transfer
 * - Data exchange
 * - Result transmission
 */
struct lambda_config {
    struct config_t data_qp;   // QP for code and data transfer
};

/**
 * @brief Posts an RDMA Send operation for lambda mode
 *
 * @param config RDMA configuration structure
 * @param buf Buffer containing data to send
 * @return int 0 on success, non-zero on failure
 */
int post_lambda_send(struct config_t* config, void* buf);

/**
 * @brief Posts an RDMA Receive operation for lambda mode
 *
 * @param config RDMA configuration structure
 * @return int 0 on success, non-zero on failure
 */
int post_lambda_receive(struct config_t* config);

/**
 * @brief Posts an RDMA Write operation for lambda results
 *
 * @param config RDMA configuration structure
 * @param buf Buffer containing result data
 * @param remote_info Remote QP information for the write operation
 * @return int 0 on success, non-zero on failure
 */
int post_lambda_write(struct config_t* config, void* buf, struct qp_info_t* remote_info);

/**
 * @brief Starts the lambda server
 *
 * @return int 0 on success, non-zero on failure
 *
 * Server initialization sequence:
 * 1. Sets up RDMA resources
 * 2. Configures memory regions
 * 3. Enters main processing loop
 * 4. Handles cleanup on shutdown
 */
int lambda_run_server(void);

/**
 * @brief Starts the lambda client
 *
 * @param server_name Hostname or IP address of the server
 * @return int 0 on success, non-zero on failure
 *
 * Client initialization sequence:
 * 1. Connects to specified server
 * 2. Sets up RDMA resources
 * 3. Enables function execution interface
 * 4. Handles cleanup on shutdown
 */
int lambda_run_client(const char* server_name);

// Add mode-specific header definitions
DEFINE_RDMA_MODE_HEADER(lambda)

#endif // LAMBDA_H