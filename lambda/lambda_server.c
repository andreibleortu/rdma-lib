/**
 * @file lambda_server.c
 * @brief Server-side implementation of RDMA lambda function execution
 *
 * Implements server functionality for remote function execution:
 * - Memory management for received function code
 * - Safe execution environment setup
 * - Input/output data handling
 * - Result transmission
 */

#include "lambda.h"
#include <sys/mman.h>

static struct lambda_memory_regions server_regions;

/**
 * @brief Sets up memory regions for lambda execution on server side
 *
 * @param config RDMA configuration structure
 *
 * Allocates and registers memory regions for:
 * - Executable code space (with appropriate permissions)
 * - Input data buffer
 * - Output data buffer
 *
 * @note Uses mmap to allocate executable memory with proper protections
 */
static void setup_lambda_regions(struct config_t *config)
{
	DEBUG_LOG("Setting up lambda regions");

	if (!config) {
		ERROR_LOG("Null config passed to setup_lambda_regions");
		exit(1);
	}

	// Allocate executable memory for code
	server_regions.code_region
		= mmap(NULL, LAMBDA_MAX_CODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (server_regions.code_region == MAP_FAILED) {
		ERROR_LOG("Failed to mmap code region: %s", strerror(errno));
		exit(1);
	}
	DEBUG_LOG("Code region mapped at %p", server_regions.code_region);

	// Register with RDMA
	server_regions.code_mr = ibv_reg_mr(
		config->pd, server_regions.code_region, LAMBDA_MAX_CODE_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

	if (!server_regions.code_mr) {
		ERROR_LOG("Failed to register code memory region: %s", strerror(errno));
		exit(1);
	}
	DEBUG_LOG("Code MR registered successfully");

	if (!config->buf) {
		ERROR_LOG("Config buffer is NULL");
		exit(1);
	}

	// Setup input/output regions using existing buffer
	server_regions.input_region = config->buf;
	server_regions.output_region = config->buf + LAMBDA_MAX_INPUT_SIZE;
	server_regions.input_mr = config->mr;
	server_regions.output_mr = config->mr;

	DEBUG_LOG("Lambda regions setup complete");
}

/**
 * @brief Configures Queue Pairs for lambda mode operation
 *
 * @param config Lambda configuration structure
 *
 * Sets up QPs with appropriate attributes for:
 * - Code transfer
 * - Data exchange
 * - Result transmission
 */
static void setup_lambda_qps(struct lambda_config *config) {
    // Setup data QP for RDMA Write
    if (setup_rdma_connection(&config->data_qp, NULL, MODE_WRITE, NULL) != RDMA_SUCCESS) {
        ERROR_LOG("Failed to setup data QP");
        exit(1);
    }
}

/**
 * @brief Main server loop for handling lambda function requests
 *
 * @param config RDMA configuration structure
 *
 * Server operation sequence:
 * 1. Receives function metadata
 * 2. Allocates appropriate memory regions
 * 3. Receives function code
 * 4. Sets up execution environment
 * 5. Receives input data
 * 6. Executes function
 * 7. Returns results via RDMA Write
 */
static void lambda_server_loop(struct config_t *config)
{
    DEBUG_LOG("Entering lambda server loop");

    // Add persistent metadata storage
    struct lambda_metadata meta_storage;
    struct qp_info_t client_info;

    while (1) {
        DEBUG_LOG("Waiting for function code...");

        // First receive metadata
        if (post_lambda_receive(config) != 0) {
            ERROR_LOG("Failed to post receive for metadata");
            break;
        }

        DEBUG_LOG("Waiting for metadata");
        wait_completion(config);

        // Copy metadata and client QP info
        memcpy(&meta_storage, config->buf, sizeof(struct lambda_metadata));
        memcpy(&client_info, config->buf + sizeof(struct lambda_metadata), sizeof(struct qp_info_t));
        struct lambda_metadata *meta = &meta_storage;

        // Validate metadata before proceeding
        if (meta->code_size == 0 || meta->code_size > LAMBDA_MAX_CODE_SIZE) {
            ERROR_LOG("Invalid metadata received: code_size=%zu", meta->code_size);
            break;
        }

        DEBUG_LOG("Received metadata for function '%s', code_size: %zu, entry_offset: %lu",
            meta->function_name[0] ? meta->function_name : "<unnamed>", meta->code_size, meta->entry_offset);

        // Wait for function code
        if (post_lambda_receive(config) != 0) {
            ERROR_LOG("Failed to post receive for function code");
            break;
        }

        DEBUG_LOG("Waiting for function code");
        wait_completion(config);

        // Copy received code to executable region using stored metadata
        memcpy(server_regions.code_region, config->buf, meta->code_size);
        DEBUG_LOG("Copied %zu bytes of code to executable region", meta->code_size);

        // Post receive for input data
        if (post_lambda_receive(config) != 0) {
            ERROR_LOG("Failed to post receive for input data");
            break;
        }

        DEBUG_LOG("Waiting for input data");
        wait_completion(config);

        // Validate entry offset using stored metadata
        if (meta->entry_offset >= meta->code_size) {
            ERROR_LOG("Invalid entry offset: %lu (code size: %zu)", meta->entry_offset, meta->code_size);
            break;
        }

        // Execute function using stored metadata
        lambda_fn func = (lambda_fn)(server_regions.code_region + meta->entry_offset);
        DEBUG_LOG("Function address: %p", func);

        size_t output_size;
        DEBUG_LOG("Executing function...");

        int result = func(server_regions.input_region, meta->input_size, server_regions.output_region, &output_size);

        DEBUG_LOG("Function execution complete. Result: %d, output_size: %zu", result, output_size);

         // Instead of sending, we'll write the result directly to client's memory
        char result_buf[MAX_BUFFER_SIZE];
        *(int *)result_buf = result;
        *(size_t *)(result_buf + sizeof(int)) = output_size;

        if (output_size > 0) {
            size_t data_offset = sizeof(int) + sizeof(size_t);
            size_t max_copy = MAX_BUFFER_SIZE - data_offset;
            size_t bytes_to_copy = output_size < max_copy ? output_size : max_copy;

            memcpy(result_buf + data_offset, server_regions.output_region, bytes_to_copy);
        }

        DEBUG_LOG("Writing result back to client memory at address %lu", client_info.addr);
        post_lambda_write(config, result_buf, &client_info);
        wait_completion(config);
    }
}

/**
 * @brief Posts an RDMA Send operation for lambda mode
 *
 * @param config RDMA configuration structure
 * @param buf Buffer containing data to send
 * @return int 0 on success, non-zero on failure
 */
int post_lambda_send(struct config_t *config, void *buf)
{
	post_operation(config, OP_SEND, (const char *)buf, NULL, MAX_BUFFER_SIZE);
	return 0;
}

/**
 * @brief Posts an RDMA Receive operation for lambda mode
 * @param config RDMA configuration structure
 * @return int 0 on success, non-zero on failure
 */
int post_lambda_receive(struct config_t *config)
{
	post_receive(config);
	return 0;
}

/**
 * @brief Posts an RDMA Write operation for lambda results
 *
 * @param config RDMA configuration structure
 * @param buf Buffer containing result data
 * @param remote_info Remote QP information for the write operation
 * @return int 0 on success, non-zero on failure
 */
int post_lambda_write(struct config_t *config, void *buf, struct qp_info_t *remote_info)
{
	post_operation(config, OP_WRITE, (const char *)buf, remote_info, MAX_BUFFER_SIZE);
	return 0;
}

/**
 * @brief Main entry point for lambda server operation
 *
 * @return int 0 on success, non-zero on failure
 *
 * Server initialization sequence:
 * 1. Sets up RDMA resources
 * 2. Configures memory regions
 * 3. Enters main processing loop
 * 4. Handles cleanup on shutdown
 */
int lambda_run_server(void)
{
    DEBUG_LOG("Starting lambda server");
    struct lambda_config config = {};

    setup_lambda_qps(&config);
    setup_lambda_regions(&config.data_qp);

    printf("Lambda Server ready.\n");
    lambda_server_loop(&config.data_qp);

    DEBUG_LOG("Cleaning up resources");
    cleanup_resources(&config.data_qp);
    return 0;
}
