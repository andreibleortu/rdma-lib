#ifndef COMMON_H
#define COMMON_H

/* Core System and RDMA Headers */
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

/**
 * Configuration Constants
 * MAX_BUFFER_SIZE: Maximum size for RDMA data transfer buffer, critical for memory registration
 * TCP_PORT: Port used for out-of-band connection setup and QP information exchange
 * IB_PORT: InfiniBand/RoCE port number on the NIC
 * GID_INDEX: Global Identifier index for RoCE v2 protocol (usually 1 for RoCE, 0 for IB)
 */
#define MAX_BUFFER_SIZE 4096  // Maximum size for RDMA data transfer buffer
#define TCP_PORT 18515        // TCP port used for initial connection setup
#define IB_PORT 1            // InfiniBand port number
#define GID_INDEX 1          // GID index for RoCEv2

/* Debug Configuration */
#define DEBUG 1              // Debug mode flag: 1 = enabled, 0 = disabled

/**
 * Logging Macros
 * DEBUG_LOG: Prints debug messages when DEBUG is enabled
 * ERROR_LOG: Always prints error messages with file and line information
 */
#define DEBUG_LOG(fmt, ...) \
    do { if (DEBUG) fprintf(stderr, "[DEBUG][%s:%d] " fmt "\n", \
        __FILE__, __LINE__, ##__VA_ARGS__); } while (0)

#define ERROR_LOG(fmt, ...) \
    fprintf(stderr, "[ERROR][%s:%d] " fmt "\n", \
        __FILE__, __LINE__, ##__VA_ARGS__);

// Add this macro for consistent header guards
#define DEFINE_RDMA_MODE_HEADER(prefix)                                                                                \
	int prefix##_run_client(const char *server_name);                                                                  \
	int prefix##_run_server(void);

/**
 * RDMA Queue Pair Configuration Constants
 * 
 * DEFAULT_MAX_WR: Maximum number of outstanding Work Requests
 * Limits the number of operations that can be posted without completion
 * 
 * DEFAULT_MAX_SGE: Maximum Scatter-Gather Elements per Work Request
 * Limits the number of memory segments that can be used in a single operation
 * 
 * CQ_SIZE: Size of Completion Queue - should be >= sum of Send and Receive queue sizes
 * MAX_INLINE_DATA: Maximum size of data that can be sent inline with WQE
 * MAX_SGE: Maximum number of SGE elements allowed in a single Work Request
 */
#define DEFAULT_MAX_WR 10    // Default maximum work requests
#define DEFAULT_MAX_SGE 1    // Default maximum scatter/gather elements
#define CQ_SIZE 128         // Completion Queue size
#define MAX_INLINE_DATA 256  // Maximum inline data size
#define MAX_SGE 4           // Maximum scatter/gather elements per request

/**
 * Error handling macro
 * CHECK_NULL: Validates pointer and returns error code if NULL
 */
#define CHECK_NULL(ptr, msg) do { if (!(ptr)) { fprintf(stderr, "%s\n", msg); return RDMA_ERR_RESOURCE; } } while(0)

/**
 * RDMA Operation Modes Enumeration
 * Defines the supported RDMA operation types:
 * MODE_SEND_RECV: Traditional two-sided communication
 * MODE_WRITE: One-sided RDMA write operations
 * MODE_READ: One-sided RDMA read operations
 * MODE_LAMBDA: Custom lambda operation mode for advanced operations
 */
typedef enum rdma_mode { MODE_SEND_RECV, MODE_WRITE, MODE_READ, MODE_LAMBDA } rdma_mode_t;

/**
 * RDMA Operation Types
 * Used to specify the type of RDMA operation when posting Work Requests:
 * OP_SEND: Regular send operation (requires receive on remote side)
 * OP_WRITE: RDMA write operation (one-sided)
 * OP_READ: RDMA read operation (one-sided)
 */
typedef enum rdma_op { OP_SEND, OP_WRITE, OP_READ } rdma_op_t;

/**
 * RDMA Status Codes
 * Represents various possible outcomes of RDMA operations
 */
typedef enum { RDMA_SUCCESS = 0, RDMA_ERR_DEVICE, RDMA_ERR_RESOURCE, RDMA_ERR_COMMUNICATION } rdma_status_t;

/**
 * Configuration Structure
 * Contains all RDMA resources required for communication:
 * - Device context and Protection Domain
 * - Queue Pair and Completion Queue
 * - Memory Region for RDMA operations
 * - Communication buffer and metadata
 */
struct config_t {
	struct ibv_context *context;  // Device context
	struct ibv_pd *pd;           // Protection Domain
	struct ibv_cq *cq;           // Completion Queue
	struct ibv_qp *qp;           // Queue Pair
	struct ibv_mr *mr;           // Memory Region
	void *buf;                   // Data buffer
	union ibv_gid gid;          // GID for RoCEv2
	int sock_fd;                 // Socket for control messages
};

/**
 * Queue Pair Information Structure
 * Contains metadata needed to establish RDMA connection:
 * - QP number for identifying the remote QP
 * - GID for RoCE addressing
 * - Remote buffer address and key for RDMA operations
 */
struct qp_info_t {
	uint32_t qp_num;            // Queue Pair number
	union ibv_gid gid;          // GID for RoCEv2
	uint64_t addr;              // Remote buffer address
	uint32_t rkey;              // Remote key for RDMA operations
};

/* Function Declarations */
// Error handling functions
void die(const char *message);
void die_with_cleanup(const char *message, struct config_t *config);

/* Resource Management Functions */
/**
 * Resource Management Functions
 * cleanup_resources: Properly tears down all RDMA resources
 * init_resources: Initializes RDMA resources based on operation mode
 * @param config: Configuration structure containing all RDMA resources
 * @param mode: RDMA operation mode determining resource setup
 * @return RDMA status code indicating success or specific failure
 */
void cleanup_resources(struct config_t *config);

/**
 * @brief Initializes RDMA resources
 * @param config Configuration structure to initialize
 * @param mode RDMA operation mode
 * @return RDMA status code
 */
rdma_status_t init_resources(struct config_t *config, rdma_mode_t mode);

/* Queue Pair State Management Functions */
/**
 * Queue Pair State Management Functions
 * Handle the state transitions of Queue Pairs:
 * modify_qp_to_init: Transitions QP to INIT state
 * modify_qp_to_rtr: Transitions QP to Ready to Receive state
 * modify_qp_to_rts: Transitions QP to Ready to Send state
 * Each state requires specific attributes and capabilities to be set
 */
void modify_qp_to_init(struct ibv_qp *qp, int access_flags);
void modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, union ibv_gid remote_gid);
void modify_qp_to_rts(struct ibv_qp *qp);

/* Connection Management Functions */
/**
 * Connection Management Functions
 * setup_socket: Establishes TCP connection for control messages
 * exchange_qp_info: Exchanges QP information between peers
 * connect_qps: Performs full QP connection setup
 * @param config: RDMA configuration structure
 * @param server_name: Target server hostname (NULL for server side)
 * @param remote_info: Structure to store remote QP information
 */
void setup_socket(struct config_t *config, const char *server_name);
void exchange_qp_info(
	struct config_t *config, const char *server_name, struct qp_info_t *local_info, struct qp_info_t *remote_info);
void connect_qps(struct config_t *config, const char *server_name, struct qp_info_t *remote_info, rdma_mode_t mode);

/* RDMA Operation Functions */
/**
 * RDMA Operation Functions
 * post_operation: Posts RDMA operations (send/write/read)
 * wait_completion: Waits for operation completion
 * post_receive: Posts receive work request
 * @param config: RDMA configuration
 * @param op: Operation type
 * @param data: Data buffer for operation
 * @param remote_info: Remote QP information
 * @param length: Data length
 */
void wait_completion(struct config_t *config);
void post_receive(struct config_t *config);

/**
 * @brief Posts an RDMA operation
 *
 * @param config RDMA configuration structure
 * @param op Type of operation (send/write/read)
 * @param data Data buffer to send (NULL for read operations)
 * @param remote_info Remote QP information (NULL for send operations)
 * @param length Length of data to transfer
 *
 * Posts a Work Request (WR) for the specified RDMA operation.
 * Handles both two-sided (send/recv) and one-sided (read/write) operations.
 */
void post_operation(struct config_t *config, rdma_op_t op, const char *data,
                   const struct qp_info_t *remote_info, size_t length);

// Run functions for client/server
int run_client(const char *server_name, rdma_mode_t mode);
int run_server(rdma_mode_t mode);

/* Signal Handling */
extern struct config_t *global_config;
void signal_handler(int signo);
void handle_disconnect(struct config_t *config);  // Add this declaration

// Add helper function declaration
rdma_status_t setup_rdma_connection(struct config_t *config, const char *server_name, 
                                  rdma_mode_t mode, struct qp_info_t *remote_info);

#endif
