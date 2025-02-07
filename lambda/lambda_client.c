/**
 * @file lambda_client.c
 * @brief Client-side implementation of RDMA lambda function execution
 *
 * Handles remote function execution via RDMA, including:
 * - Loading functions from shared libraries
 * - Transmitting function code to remote server
 * - Managing input/output data transfer
 * - Processing execution results
 */

#include "lambda.h"
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

// Define RTLD_DL_LINKMAP if not available
#ifndef RTLD_DL_LINKMAP
#define RTLD_DL_LINKMAP 2
#endif

struct lambda_memory_regions client_regions;

/**
 * @brief Sets up memory regions for lambda function execution on client side
 *
 * @param config RDMA configuration structure
 *
 * Allocates and registers memory for:
 * - Function code storage
 * - Input data buffer
 * - Output data buffer
 */
static void setup_lambda_regions(struct config_t *config)
{
	client_regions.code_region = malloc(LAMBDA_MAX_CODE_SIZE);
	client_regions.input_region = config->buf;
	client_regions.output_region = config->buf + LAMBDA_MAX_INPUT_SIZE;

	client_regions.code_mr
		= ibv_reg_mr(config->pd, client_regions.code_region, LAMBDA_MAX_CODE_SIZE, IBV_ACCESS_LOCAL_WRITE);
}

/**
 * @brief Determines the size of a function in memory
 *
 * @param func Pointer to the function
 * @return Size of the function in bytes
 *
 * Currently returns a fixed size of 4096 bytes (one page)
 * TODO: Implement proper function size detection using binary analysis
 */
static size_t get_function_size(void *func)
{
	(void)func; // Silence unused parameter warning
	return 4096; // One page
}

/**
 * @brief Executes a lambda function on the remote server
 *
 * @param config RDMA configuration structure
 * @param lib_path Path to shared library containing the function
 * @param func_name Name of function to execute
 * @param input Input data buffer
 * @param input_size Size of input data
 * @param output Buffer to store output data
 * @param output_size Pointer to store size of output data
 * @param remote_info Remote QP information
 * @return int 0 on success, non-zero on failure
 *
 * Process:
 * 1. Loads function from shared library
 * 2. Transmits function code to server
 * 3. Sends input data
 * 4. Waits for and processes results
 */
static int execute_lambda(struct config_t *config, const char *lib_path, const char *func_name, void *input,
	size_t input_size, void *output, size_t *output_size, struct qp_info_t *remote_info)
{
	// Load library
	void *handle = dlopen(lib_path, RTLD_NOW);
	if (!handle) {
		fprintf(stderr, "dlopen error: %s\n", dlerror());
		return -1;
	}

	// Get function
	lambda_fn func = dlsym(handle, func_name);
	if (!func) {
		fprintf(stderr, "dlsym error: %s\n", dlerror());
		dlclose(handle);
		return -1;
	}

	size_t code_size = get_function_size(func);

	// Clear buffer and prepare metadata
	memset(config->buf, 0, sizeof(struct lambda_metadata));
	struct lambda_metadata *meta = (struct lambda_metadata *)config->buf;
	meta->code_size = code_size;
	meta->input_size = input_size;
	meta->entry_offset = 0;
	strncpy(meta->function_name, func_name, LAMBDA_MAX_FUNCTION_NAME - 1);

	// Include our own QP info in the metadata message for the return path
	struct {
		struct lambda_metadata meta;
		struct qp_info_t qp_info;
	} combined_meta;

	memset(&combined_meta, 0, sizeof(combined_meta));
	combined_meta.meta = *meta;
	combined_meta.qp_info.qp_num = config->qp->qp_num;
	combined_meta.qp_info.addr = (uint64_t)config->buf;  // Where we want the result
	combined_meta.qp_info.rkey = config->mr->rkey;
	combined_meta.qp_info.gid = config->gid;

	DEBUG_LOG("Sending metadata and QP info");
	memcpy(config->buf, &combined_meta, sizeof(combined_meta));
	post_lambda_write(config, config->buf, remote_info);
	wait_completion(config);

	// Send function code
	memcpy(config->buf, (void *)func, code_size);
	DEBUG_LOG("Sending function code of size %zu", code_size);
	post_lambda_write(config, config->buf, remote_info);
	wait_completion(config);

	// Send input data
	memcpy(config->buf, input, input_size);
	DEBUG_LOG("Sending input data of size %zu", input_size);
	post_lambda_write(config, config->buf, remote_info);
	wait_completion(config);

	// Post a receive to be notified when the server's RDMA Write completes
	post_receive(config);
	DEBUG_LOG("Waiting for server's result...");
	wait_completion(config);

	int result = *(int *)config->buf;
	*output_size = *(size_t *)(config->buf + sizeof(int));
	memcpy(output, config->buf + sizeof(int) + sizeof(size_t), *output_size);

	DEBUG_LOG("Function execution completed with result=%d, output_size=%zu", result, *output_size);

	dlclose(handle);
	return result;
}

/**
 * @brief Signal handler for graceful client shutdown
 *
 * @param signum Signal number
 *
 * Ensures proper cleanup of RDMA resources when client is terminated
 */
static void local_signal_handler(int signum)
{
    (void)signum; // Silence unused parameter warning
    if (global_config) {
        exit(0);
    }
}

/**
 * @brief Main entry point for lambda client operation
 *
 * @param server_name Hostname or IP address of the server
 * @return int 0 on success, non-zero on failure
 *
 * Sets up RDMA connection and provides interactive interface for:
 * - Loading and executing remote functions
 * - Sending input data
 * - Receiving and processing results
 */
int lambda_run_client(const char *server_name)
{
    DEBUG_LOG("Starting lambda client, connecting to %s", server_name);
    struct lambda_config config = {};
    struct qp_info_t remote_info;

    // Setup data QP for RDMA Write
    DEBUG_LOG("Setting up data QP");
    if (setup_rdma_connection(&config.data_qp, server_name, MODE_WRITE, &remote_info) != RDMA_SUCCESS) {
        fprintf(stderr, "Failed to setup data QP connection\n");
        return -1;
    }

    // Set up signal handler for Ctrl+C
    signal(SIGINT, local_signal_handler);
    signal(SIGTERM, local_signal_handler);

    setup_lambda_regions(&config.data_qp);

    // Example usage
    char *lib_path = "./lambda-run.so";
    char *func_name = "process_data";
    char input[] = "Test STRING which will be Made upperCASE";
    char output[1024];
    size_t output_size;

    int result = execute_lambda(&config.data_qp, lib_path, func_name, input, strlen(input) + 1, 
                              output, &output_size, &remote_info);

    if (result == 0) {
        printf("Processed output (%zu bytes): %s\n", output_size, (char *)output);
    } else {
        printf("Execution failed with error: %d\n", result);
    }

    // Don't send disconnect here anymore, it will be sent by signal handler
    cleanup_resources(&config.data_qp);
    return result;
}
