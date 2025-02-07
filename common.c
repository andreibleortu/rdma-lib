/**
 * @file common.c
 * @brief Core RDMA functionality implementation
 *
 * Implements the common RDMA operations including:
 * - Resource management (device, PD, CQ, QP)
 * - Connection establishment
 * - Memory registration and buffer management
 * - Queue Pair state transitions
 * - RDMA operation posting and completion handling
 */

#include "common.h"
#include "send-receive/send_receive.h"
#include "rdma-write/rdma_write.h"
#include "rdma-read/rdma_read.h"
#include "lambda/lambda.h"

/*******************************************************************************
 * Constants and Configuration
 ******************************************************************************/
#define MAX_CQ_ENTRIES 10    // Maximum Completion Queue entries
#define TIMEOUT 14           // QP timeout value (4.096us * 2^timeout)
#define RETRY_COUNT 7        // Number of retry attempts for RC QP operations
#define RNR_RETRY 7         // RNR (Receiver Not Ready) retry count

/*******************************************************************************
 * Error Handling and Utilities
 ******************************************************************************/

/**
 * @brief Converts RDMA status code to human-readable string
 * @param err RDMA status code to convert
 * @return Corresponding error message string
 */
static const char *rdma_err_to_str(rdma_status_t err)
{
    static const char *err_strings[] = {
        [RDMA_SUCCESS] = "Success",
        [RDMA_ERR_DEVICE] = "Device error",
        [RDMA_ERR_RESOURCE] = "Resource error",
        [RDMA_ERR_COMMUNICATION] = "Communication error"
    };
    return err_strings[err];
}

/**
 * @brief Fatal error handler
 * @param message Error message to display before exiting
 */
void die(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(1);
}

/**
 * @brief Fatal error handler with resource cleanup
 * @param message Error message to display
 * @param config RDMA configuration to cleanup
 */
void die_with_cleanup(const char *message, struct config_t *config)
{
    fprintf(stderr, "%s\n", message);
    cleanup_resources(config);
    exit(1);
}

/*******************************************************************************
 * Resource Management
 ******************************************************************************/

/**
 * @brief Cleans up Queue Pair resources
 * @param config Configuration containing QP to cleanup
 */
static void cleanup_qp(struct config_t *config)
{
    if (config->qp) {
        ibv_destroy_qp(config->qp);
        config->qp = NULL;
    }
}

/**
 * @brief Main resource cleanup function
 * @param config Configuration containing all resources to cleanup
 *
 * Cleanup sequence:
 * 1. Queue Pair
 * 2. Memory Region
 * 3. Memory Buffer
 * 4. Completion Queue
 * 5. Protection Domain
 * 6. Device Context
 * 7. Socket
 */
void cleanup_resources(struct config_t *config)
{
    if (!config) return;
    
    if (config->qp)
        cleanup_qp(config);  // Use the function here
    if (config->mr)
        ibv_dereg_mr(config->mr);
    if (config->buf)
        free(config->buf);
    if (config->cq)
        ibv_destroy_cq(config->cq);
    if (config->pd)
        ibv_dealloc_pd(config->pd);
    if (config->context)
        ibv_close_device(config->context);
    if (config->sock_fd)
        close(config->sock_fd);
}

/**
 * @brief Initialize RDMA resources
 * @param config Configuration to initialize
 * @param mode RDMA operation mode
 * @return RDMA_SUCCESS on success, error code on failure
 *
 * Initialization sequence:
 * 1. Device discovery and context creation
 * 2. Protection Domain allocation
 * 3. Completion Queue creation
 * 4. Queue Pair creation and configuration
 * 5. Memory buffer allocation and registration
 * 6. GID query for RoCE
 */
rdma_status_t init_resources(struct config_t *config, rdma_mode_t mode)
{
    if (!config) {
        return RDMA_ERR_RESOURCE;
    }

    // Get list of IB devices
    int num_devices;
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list) {
        return RDMA_ERR_DEVICE;
    }

    // Get first device
    struct ibv_device *device = dev_list[0];
    if (!device) {
        ibv_free_device_list(dev_list);
        return RDMA_ERR_DEVICE;
    }

    // Get device context
    config->context = ibv_open_device(device);
    if (!config->context) {
        ibv_free_device_list(dev_list);
        return RDMA_ERR_DEVICE;
    }

    // Allocate Protection Domain
    config->pd = ibv_alloc_pd(config->context);
    if (!config->pd) {
        cleanup_resources(config);
        ibv_free_device_list(dev_list);
        return RDMA_ERR_RESOURCE;
    }

    // Create Completion Queue
    config->cq = ibv_create_cq(config->context, MAX_CQ_ENTRIES, NULL, NULL, 0);
    if (!config->cq) {
        cleanup_resources(config);
        ibv_free_device_list(dev_list);
        return RDMA_ERR_RESOURCE;
    }

    // Create Queue Pair
    struct ibv_qp_init_attr qp_init_attr = { .send_cq = config->cq,
        .recv_cq = config->cq,
        .qp_type = IBV_QPT_RC,
        .cap = { .max_send_wr = MAX_CQ_ENTRIES, .max_recv_wr = MAX_CQ_ENTRIES, .max_send_sge = 1, .max_recv_sge = 1 } };

    config->qp = ibv_create_qp(config->pd, &qp_init_attr);
    if (!config->qp) {
        cleanup_resources(config);
        ibv_free_device_list(dev_list);
        return RDMA_ERR_RESOURCE;
    }

    // Allocate memory buffer
    config->buf = malloc(MAX_BUFFER_SIZE);
    if (!config->buf) {
        cleanup_resources(config);
        ibv_free_device_list(dev_list);
        return RDMA_ERR_RESOURCE;
    }

    // Determine access flags based on mode
    int access_flags = IBV_ACCESS_LOCAL_WRITE;
    switch (mode) {
    case MODE_WRITE: access_flags |= IBV_ACCESS_REMOTE_WRITE; break;
    case MODE_READ: access_flags |= IBV_ACCESS_REMOTE_READ; break;
    case MODE_SEND_RECV: break;
    case MODE_LAMBDA: access_flags |= IBV_ACCESS_REMOTE_WRITE; break;
    }

    // Register Memory Region
    config->mr = ibv_reg_mr(config->pd, config->buf, MAX_BUFFER_SIZE, access_flags);
    if (!config->mr) {
        cleanup_resources(config);
        ibv_free_device_list(dev_list);
        return RDMA_ERR_RESOURCE;
    }

    // Query GID for RoCE
    if (ibv_query_gid(config->context, IB_PORT, GID_INDEX, &config->gid)) {
        cleanup_resources(config);
        ibv_free_device_list(dev_list);
        return RDMA_ERR_DEVICE;
    }

    ibv_free_device_list(dev_list);
    return RDMA_SUCCESS;
}

/*******************************************************************************
 * Queue Pair State Management
 ******************************************************************************/

/**
 * @brief Transition QP to INIT state
 * @param qp Queue Pair to modify
 * @param access_flags Access permissions for the QP
 */
void modify_qp_to_init(struct ibv_qp *qp, int access_flags)
{
    struct ibv_qp_attr attr
        = { .qp_state = IBV_QPS_INIT, .pkey_index = 0, .port_num = IB_PORT, .qp_access_flags = access_flags };

    if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
        die("Failed to modify QP to INIT");
    }
}

/**
 * @brief Transition QP to Ready to Receive state
 * @param qp Queue Pair to modify
 * @param remote_qpn Remote QP number
 * @param remote_gid Remote GID for addressing
 */
void modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, union ibv_gid remote_gid)
{
    struct ibv_qp_attr attr = { 
        .qp_state = IBV_QPS_RTR,
        .path_mtu = IBV_MTU_1024,
        .dest_qp_num = remote_qpn,
        .rq_psn = 0,
        .max_dest_rd_atomic = 1,
        .min_rnr_timer = 12,
        .ah_attr = { 
            .is_global = 1,
            .sl = 0,
            .src_path_bits = 0,
            .port_num = IB_PORT,
            .grh = { 
                .hop_limit = 1,
                .dgid = remote_gid,
                .sgid_index = GID_INDEX 
            } 
        }
    };

    if (ibv_modify_qp(qp, &attr,
            IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | 
            IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER)) {
        die("Failed to modify QP to RTR");
    }
}

/**
 * @brief Transition QP to Ready to Send state
 * @param qp Queue Pair to modify
 */
void modify_qp_to_rts(struct ibv_qp *qp)
{
    struct ibv_qp_attr attr = { .qp_state = IBV_QPS_RTS,
        .timeout = TIMEOUT,
        .retry_cnt = RETRY_COUNT,
        .rnr_retry = RNR_RETRY,
        .sq_psn = 0,
        .max_rd_atomic = 1 };

    if (ibv_modify_qp(qp, &attr,
            IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN
                | IBV_QP_MAX_QP_RD_ATOMIC)) {
        die("Failed to modify QP to RTS");
    }
}

/*******************************************************************************
 * Connection Management
 ******************************************************************************/

/**
 * @brief Setup network socket for control path
 * @param config RDMA configuration
 * @param server_name Server hostname (NULL for server mode)
 *
 * Handles both client and server socket setup:
 * - Client: Connects with retry logic
 * - Server: Creates listening socket and accepts connection
 */
void setup_socket(struct config_t *config, const char *server_name)
{
    struct sockaddr_in server_addr;
    if (server_name) { // Client
        config->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (config->sock_fd < 0) {
            die("Failed to create socket");
        }

        struct hostent *server = gethostbyname(server_name);
        if (!server) {
            die("No such host");
        }

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        memcpy(&server_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);
        server_addr.sin_port = htons(TCP_PORT);

        // Add connection timeout
        struct timeval timeout;
        timeout.tv_sec = 5;  // 5 seconds timeout
        timeout.tv_usec = 0;
        
        if (setsockopt(config->sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
            close(config->sock_fd);
            die("Failed to set socket timeout");
        }
        
        if (setsockopt(config->sock_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
            close(config->sock_fd);
            die("Failed to set socket timeout");
        }

        // Try to connect with retry
        int retry_count = 3;
        int connected = 0;
        while (retry_count-- > 0 && !connected) {
            if (connect(config->sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
                connected = 1;
                break;
            }
            if (retry_count > 0) {
                fprintf(stderr, "Connection failed, retrying in 1 second... (%d attempts left)\n", retry_count);
                sleep(1);
            }
        }
        
        if (!connected) {
            close(config->sock_fd);
            die("Failed to connect after multiple attempts");
        }
    } else { // Server
        int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) {
            die("Failed to create socket");
        }

        int optval = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(TCP_PORT);

        if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            close(listen_fd);
            die("Failed to bind");
        }

        listen(listen_fd, 1);
        config->sock_fd = accept(listen_fd, NULL, NULL);
        if (config->sock_fd < 0) {
            close(listen_fd);
            die("Failed to accept");
        }

        close(listen_fd);
    }
}

/**
 * @brief Exchange QP information between peers
 * @param config RDMA configuration
 * @param server_name Server hostname (NULL for server)
 * @param local_info Local QP info to send
 * @param remote_info Remote QP info to receive
 */
void exchange_qp_info(
    struct config_t *config, const char *server_name, struct qp_info_t *local_info, struct qp_info_t *remote_info)
{
    if (server_name) { // Client
        if (write(config->sock_fd, local_info, sizeof(*local_info)) != sizeof(*local_info)) {
            close(config->sock_fd);
            die("Failed to send local QP info");
        }
        if (read(config->sock_fd, remote_info, sizeof(*remote_info)) != sizeof(*remote_info)) {
            close(config->sock_fd);
            die("Failed to receive remote QP info");
        }
    } else { // Server
        if (read(config->sock_fd, remote_info, sizeof(*remote_info)) != sizeof(*remote_info)) {
            close(config->sock_fd);
            die("Failed to receive remote QP info");
        }
        if (write(config->sock_fd, local_info, sizeof(*local_info)) != sizeof(*local_info)) {
            close(config->sock_fd);
            die("Failed to send local QP info");
        }
    }
}

/**
 * @brief Establish full RDMA connection
 * @param config RDMA configuration
 * @param server_name Server hostname (NULL for server)
 * @param remote_info Remote QP info to receive
 * @param mode RDMA operation mode
 */
void connect_qps(struct config_t *config, const char *server_name, struct qp_info_t *remote_info, rdma_mode_t mode)
{
    struct qp_info_t local_qp_info = { 
        .qp_num = config->qp->qp_num,
        .gid = config->gid,
        .addr = (uint64_t)config->buf,
        .rkey = config->mr->rkey 
    };

    struct qp_info_t remote_qp_info;

    // Use common connection setup
    setup_socket(config, server_name);
    exchange_qp_info(config, server_name, &local_qp_info, &remote_qp_info);

    if (remote_info) {
        memcpy(remote_info, &remote_qp_info, sizeof(struct qp_info_t));
    }

    // Set appropriate access flags based on mode
    int access_flags = IBV_ACCESS_LOCAL_WRITE;
    switch (mode) {
    case MODE_WRITE: access_flags |= IBV_ACCESS_REMOTE_WRITE; break;
    case MODE_READ: access_flags |= IBV_ACCESS_REMOTE_READ; break;
    default: break;
    }

    // Transition QP states
    modify_qp_to_init(config->qp, access_flags);
    modify_qp_to_rtr(config->qp, remote_qp_info.qp_num, remote_qp_info.gid);
    modify_qp_to_rts(config->qp);
}

/*******************************************************************************
 * RDMA Operations
 ******************************************************************************/

/**
 * @brief Post an RDMA operation
 * @param config RDMA configuration
 * @param op Operation type (send/write/read)
 * @param data Data buffer (NULL for read)
 * @param remote_info Remote QP info (NULL for send)
 * @param length Data length in bytes
 */
void post_operation(
    struct config_t *config, rdma_op_t op, const char *data, const struct qp_info_t *remote_info, size_t length)
{
    if (!config || length > MAX_BUFFER_SIZE) {
        return;
    }

    struct ibv_sge sg = { .addr = (uint64_t)config->buf, .length = length, .lkey = config->mr->lkey };

    struct ibv_send_wr wr = { .wr_id = 0, .sg_list = &sg, .num_sge = 1, .send_flags = IBV_SEND_SIGNALED };

    // Configure operation-specific parameters
    switch (op) {
    case OP_SEND:
        if (data)
            memcpy(config->buf, data, length);
        wr.opcode = IBV_WR_SEND;
        break;

    case OP_WRITE:
        if (data)
            memcpy(config->buf, data, length);
        wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        wr.imm_data = htonl(length);
        wr.wr.rdma.remote_addr = remote_info->addr;
        wr.wr.rdma.rkey = remote_info->rkey;
        break;

    case OP_READ:
        wr.opcode = IBV_WR_RDMA_READ;
        wr.wr.rdma.remote_addr = remote_info->addr;
        wr.wr.rdma.rkey = remote_info->rkey;
        break;
    }

    struct ibv_send_wr *bad_wr;
    if (ibv_post_send(config->qp, &wr, &bad_wr)) {
        die("Failed to post operation");
    }
}

/**
 * @brief Post receive work request
 * @param config RDMA configuration
 */
void post_receive(struct config_t *config)
{
    struct ibv_sge sg = { .addr = (uint64_t)config->buf, .length = MAX_BUFFER_SIZE, .lkey = config->mr->lkey };

    struct ibv_recv_wr wr = { .wr_id = 0, .sg_list = &sg, .num_sge = 1 };

    struct ibv_recv_wr *bad_wr;
    if (ibv_post_recv(config->qp, &wr, &bad_wr)) {
        die("Failed to post RR");
    }
}

/**
 * @brief Wait for operation completion
 * @param config RDMA configuration
 */
void wait_completion(struct config_t *config)
{
    struct ibv_wc wc;
    while (ibv_poll_cq(config->cq, 1, &wc) == 0)
        ;
    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Completion error: %s\n", ibv_wc_status_str(wc.status));
        die("RDMA operation failed");
    }
}

/*******************************************************************************
 * Signal and Error Handling
 ******************************************************************************/

/**
 * @brief Signal handler for graceful shutdown
 * @param signo Signal number
 */
void signal_handler(int signo)
{
    printf("\nCaught signal %d, cleaning up...\n", signo);
    if (global_config) {
        //handle_disconnect(global_config);
        cleanup_resources(global_config);
        global_config = NULL;
    }
    exit(0);
}

/**
 * @brief Handle connection disconnect
 * @param config RDMA configuration
 */
void handle_disconnect(struct config_t *config)
{
    if (!config) return;
    
    // Send a zero-length message to indicate disconnect
    char empty_msg = 0;
    if (config->sock_fd) {
        write(config->sock_fd, &empty_msg, 1);
    }
}

/*******************************************************************************
 * Mode Processing and Entry Points
 ******************************************************************************/

/**
 * @brief Setup complete RDMA connection
 * @param config RDMA configuration
 * @param server_name Server hostname (NULL for server)
 * @param mode RDMA operation mode
 * @param remote_info Remote QP info to receive
 * @return RDMA_SUCCESS on success, error code on failure
 */
rdma_status_t setup_rdma_connection(struct config_t *config, const char *server_name, 
                                  rdma_mode_t mode, struct qp_info_t *remote_info)
{
    rdma_status_t status = init_resources(config, mode);
    if (status != RDMA_SUCCESS) {
        fprintf(stderr, "Failed to initialize resources: %s\n", rdma_err_to_str(status));
        return status;
    }
    
    connect_qps(config, server_name, remote_info, mode);
    return RDMA_SUCCESS;
}

/**
 * @brief Run RDMA server in specified mode
 * @param mode RDMA operation mode
 * @return 0 on success, -1 on failure
 */
int run_server(rdma_mode_t mode)
{
    int result;
    struct config_t config = {};
    
    if (setup_rdma_connection(&config, NULL, mode, NULL) != RDMA_SUCCESS) {
        return -1;
    }
    
    switch (mode) {
        case MODE_WRITE:
            result = rw_run_server();
            break;
        case MODE_READ:
            result = rd_run_server();
            break;
        case MODE_SEND_RECV:
            result = sr_run_server();
            break;
        case MODE_LAMBDA:
            result = lambda_run_server();
            break;
        default:
            fprintf(stderr, "Invalid mode\n");
            result = -1;
    }
    
    cleanup_resources(&config);
    return result;
}

/**
 * @brief Run RDMA client in specified mode
 * @param server_name Server hostname
 * @param mode RDMA operation mode
 * @return 0 on success, -1 on failure
 */
int run_client(const char *server_name, rdma_mode_t mode)
{
    int result;
    struct config_t config = {};
    struct qp_info_t remote_info;
    
    if (setup_rdma_connection(&config, server_name, mode, &remote_info) != RDMA_SUCCESS) {
        return -1;
    }
    
    switch (mode) {
        case MODE_WRITE:
            result = rw_run_client(server_name);
            break;
        case MODE_READ:
            result = rd_run_client(server_name);
            break;
        case MODE_SEND_RECV:
            result = sr_run_client(server_name);
            break;
        case MODE_LAMBDA:
            result = lambda_run_client(server_name);
            break;
        default:
            fprintf(stderr, "Invalid mode\n");
            result = -1;
    }
    
    cleanup_resources(&config);
    return result;
}
