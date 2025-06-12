# RDMA Communication Library - Technical Documentation

## Table of Contents
1. [Project Overview](#project-overview)
2. [Architecture](#architecture)
3. [Core Components](#core-components)
4. [RDMA Communication Modes](#rdma-communication-modes)
5. [Memory Management](#memory-management)
6. [Connection Establishment](#connection-establishment)
7. [Implementation Details](#implementation-details)
8. [Configuration and Constants](#configuration-and-constants)
9. [Usage Examples](#usage-examples)
10. [Error Handling](#error-handling)
11. [Performance Considerations](#performance-considerations)

## Project Overview

The RDMA Communication Library is a comprehensive C implementation that demonstrates various Remote Direct Memory Access (RDMA) communication patterns using InfiniBand/RoCE hardware. The library provides four distinct communication modes, each showcasing different RDMA capabilities and use cases.

### Key Features
- **Multiple Communication Modes**: Send-Receive, RDMA Write, RDMA Read, and Lambda (remote execution)
- **Cross-Platform Support**: Works with InfiniBand and RoCE (RDMA over Converged Ethernet)
- **Resource Management**: Automatic cleanup and proper resource lifecycle management
- **Signal Handling**: Graceful shutdown with proper resource cleanup
- **Modular Design**: Clear separation of concerns with mode-specific implementations

### Supported RDMA Hardware
- InfiniBand adapters
- RoCE v1/v2 capable Ethernet adapters
- Any hardware supporting the InfiniBand Verbs API

## Architecture

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    RDMA Application                         │
├─────────────────────────────────────────────────────────────┤
│  Send/Receive │  RDMA Write  │  RDMA Read   │    Lambda     │
│     Mode      │     Mode     │     Mode     │     Mode      │
├─────────────────────────────────────────────────────────────┤
│                   Common RDMA Core                          │
│  • Resource Management  • Connection Setup                 │
│  • Queue Pair Management • Memory Registration             │
│  • Completion Handling  • Signal Processing               │
├─────────────────────────────────────────────────────────────┤
│              InfiniBand Verbs API                          │
├─────────────────────────────────────────────────────────────┤
│               RDMA Hardware Layer                          │
│          (InfiniBand / RoCE Adapters)                     │
└─────────────────────────────────────────────────────────────┘
```

### Directory Structure

```
rdma-lib/
├── rdma.c                    # Main entry point and mode dispatch
├── common.h/.c              # Core RDMA functionality
├── lambda-run.c             # Example lambda function
├── send-receive/
│   ├── send_receive.h       # Two-sided communication interface
│   └── send_receive.c       # Two-sided communication implementation
├── rdma-write/
│   ├── rdma_write.h        # One-sided write interface
│   └── rdma_write.c        # One-sided write implementation
├── rdma-read/
│   ├── rdma_read.h         # One-sided read interface
│   └── rdma_read.c         # One-sided read implementation
└── lambda/
    ├── lambda.h            # Remote execution interface
    ├── lambda_server.c     # Server-side lambda execution
    └── lambda_client.c     # Client-side lambda execution
```

## Core Components

### 1. Configuration Structure (`struct config_t`)

The central configuration structure that holds all RDMA resources:

```c
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
```

**Purpose**: Encapsulates all RDMA resources needed for communication, ensuring proper resource management and cleanup.

### 2. Queue Pair Information (`struct qp_info_t`)

Metadata structure for establishing RDMA connections:

```c
struct qp_info_t {
    uint32_t qp_num;            // Queue Pair number
    union ibv_gid gid;          // GID for RoCEv2
    uint64_t addr;              // Remote buffer address
    uint32_t rkey;              // Remote key for RDMA operations
};
```

**Purpose**: Facilitates out-of-band exchange of connection parameters between client and server.

### 3. Resource Management Functions

#### Initialization (`init_resources`)
```c
rdma_status_t init_resources(struct config_t *config, rdma_mode_t mode);
```

**Process**:
1. **Device Discovery**: Enumerate available RDMA devices
2. **Context Creation**: Open device context for the first available device
3. **Protection Domain**: Allocate PD for memory protection
4. **Completion Queue**: Create CQ for operation completions
5. **Queue Pair**: Create RC (Reliable Connection) QP
6. **Memory Allocation**: Allocate data buffer
7. **Memory Registration**: Register buffer with appropriate access flags
8. **GID Query**: Retrieve GID for RoCE addressing

#### Cleanup (`cleanup_resources`)
```c
void cleanup_resources(struct config_t *config);
```

**Process**: Systematically destroys resources in reverse order of creation to avoid dependencies.

## RDMA Communication Modes

### 1. Send-Receive Mode (`MODE_SEND_RECV`)

**Characteristics**:
- **Two-sided communication**: Both sender and receiver must participate
- **Message semantics**: Complete message delivery
- **Flow control**: Natural backpressure through receive queue depth
- **Ordering**: Strong ordering guarantees

**Implementation Flow**:
```
Client                          Server
  │                              │
  ├── Post Send ──────────────────┤
  │                              ├── Post Receive (pre-posted)
  ├── Wait Completion             ├── Wait Completion
  │                              ├── Process Message
  ├── Post Receive               ├── Post Send (ACK)
  ├── Wait Completion ◄──────────┤
  ├── Process ACK                ├── Wait Completion
```

**Use Cases**:
- Interactive messaging applications
- Request-response protocols
- Applications requiring explicit acknowledgments

### 2. RDMA Write Mode (`MODE_WRITE`)

**Characteristics**:
- **One-sided communication**: Only sender initiates operation
- **Direct memory access**: Writes directly to remote memory
- **Immediate data**: Notifies receiver of write completion
- **High performance**: Minimal CPU involvement on target

**Implementation Flow**:
```
Client                          Server
  │                              │
  ├── RDMA Write with IMM ────────┤
  │   (data + length in imm)      ├── Post Receive (for IMM)
  ├── Wait Completion             ├── Wait Completion
  │                              ├── Extract length from IMM
  │                              ├── Process written data
```

**Technical Details**:
- Uses `IBV_WR_RDMA_WRITE_WITH_IMM` operation
- Immediate data contains message length (network byte order)
- Server must pre-post receive requests for immediate data notifications

### 3. RDMA Read Mode (`MODE_READ`)

**Characteristics**:
- **One-sided communication**: Reader initiates operation
- **Selective access**: Can read arbitrary memory ranges
- **Zero copy**: Direct memory-to-memory transfer
- **Scalable**: Minimal server CPU usage

**Implementation Flow**:
```
Client                          Server
  │                              │
  │                              ├── Initialize buffer with data
  │                              ├── Wait (passive)
  ├── RDMA Read (offset, length)──┤
  ├── Wait Completion             │
  ├── Process read data           │
```

**Technical Details**:
- Uses `IBV_WR_RDMA_READ` operation
- Client specifies offset and length for selective reading
- Server memory remains accessible throughout session

### 4. Lambda Mode (`MODE_LAMBDA`)

**Characteristics**:
- **Remote execution**: Function execution on remote server
- **Code mobility**: Dynamic function loading and execution
- **Hybrid communication**: Combines write operations with execution
- **Flexible processing**: Custom computation on remote data

**Implementation Architecture**:
```
Client Side:
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│  Shared Library │───▶│ Function Extract │───▶│ Code Transmission│
└─────────────────┘    └──────────────────┘    └─────────────────┘
                                │
                                ▼
                       ┌──────────────────┐
                       │   Input Data     │
                       └──────────────────┘

Server Side:
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│  Code Reception │───▶│ Memory Mapping   │───▶│ Function Execute│
└─────────────────┘    └──────────────────┘    └─────────────────┘
                                │
                                ▼
                       ┌──────────────────┐
                       │  Result Return   │
                       └──────────────────┘
```

**Implementation Flow**:
```
Client                          Server
  │                              │
  ├── Send Metadata ──────────────┤
  │   (func_name, sizes, QP info) ├── Receive Metadata
  ├── Send Function Code ─────────┤
  │                              ├── Map to Executable Memory
  ├── Send Input Data ────────────┤
  │                              ├── Execute Function
  ├── Wait for Result             ├── RDMA Write Result ────────┤
  ├── Process Output ◄────────────┤
```

**Lambda Function Interface**:
```c
typedef int (*lambda_fn)(void* input, size_t input_size, 
                        void* output, size_t* output_size);
```

**Memory Management**:
- **Executable Memory**: Uses `mmap()` with `PROT_EXEC` for code storage
- **Input/Output Buffers**: Separate regions for data processing
- **RDMA Registration**: All regions registered for remote access

## Memory Management

### Memory Region Access Flags

Different modes require different memory access permissions:

```c
// Send-Receive Mode
int access_flags = IBV_ACCESS_LOCAL_WRITE;

// Write Mode (Server)
int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;

// Read Mode (Server) 
int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;

// Lambda Mode
int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE;
```

### Buffer Layout

Standard buffer organization for different modes:

```
Standard Buffer (4096 bytes):
┌─────────────────────────────────────────────────────────┐
│                    Data Buffer                          │
│                   (MAX_BUFFER_SIZE)                     │
└─────────────────────────────────────────────────────────┘

Lambda Mode Buffer Layout:
┌──────────────────┬──────────────────┬──────────────────┐
│   Input Region   │  Output Region   │   Code Region    │
│  (2048 bytes)    │  (2048 bytes)    │  (separate mmap) │
└──────────────────┴──────────────────┴──────────────────┘
```

## Connection Establishment

### Queue Pair State Transitions

RDMA connections require specific state transitions:

```
RESET → INIT → RTR → RTS
  │       │      │     │
  │       │      │     └── Ready to Send
  │       │      └────────── Ready to Receive  
  │       └───────────────── Initialized
  └───────────────────────── Reset State
```

### State Transition Details

#### 1. INIT State (`modify_qp_to_init`)
- Sets port number and partition key
- Configures access flags based on operation mode
- Prepares QP for connection establishment

#### 2. RTR State (`modify_qp_to_rtr`)
- Configures remote QP number and GID
- Sets path MTU and addressing information
- Enables receive operations

#### 3. RTS State (`modify_qp_to_rts`)
- Configures timeout and retry parameters
- Enables send operations
- Completes connection establishment

### Out-of-Band Connection Setup

Uses TCP sockets for exchanging QP information:

```c
struct qp_info_t local_info = {
    .qp_num = config->qp->qp_num,
    .gid = config->gid,
    .addr = (uint64_t)config->buf,
    .rkey = config->mr->rkey
};
```

## Implementation Details

### Completion Handling

All RDMA operations use completion-based notification:

```c
void wait_completion(struct config_t *config) {
    struct ibv_wc wc;
    while (ibv_poll_cq(config->cq, 1, &wc) == 0);
    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Completion error: %s\n", ibv_wc_status_str(wc.status));
        die("RDMA operation failed");
    }
}
```

### Operation Posting

Unified operation posting interface:

```c
void post_operation(struct config_t *config, rdma_op_t op, 
                   const char *data, const struct qp_info_t *remote_info, 
                   size_t length);
```

**Operation Types**:
- `OP_SEND`: Two-sided send operation
- `OP_WRITE`: One-sided write operation  
- `OP_READ`: One-sided read operation

### Signal Handling

Graceful shutdown mechanism:

```c
void signal_handler(int signo) {
    printf("\nCaught signal %d, cleaning up...\n", signo);
    if (global_config) {
        cleanup_resources(global_config);
        global_config = NULL;
    }
    exit(0);
}
```

## Configuration and Constants

### Key Configuration Parameters

```c
#define MAX_BUFFER_SIZE 4096    // Maximum transfer size
#define TCP_PORT 18515          // Control plane port
#define IB_PORT 1              // InfiniBand port number
#define GID_INDEX 1            // GID index for RoCE
#define DEFAULT_MAX_WR 10      // Maximum work requests
#define CQ_SIZE 128            // Completion queue size
```

### Performance Tuning Parameters

```c
#define TIMEOUT 14             // QP timeout (4.096us * 2^14)
#define RETRY_COUNT 7          // RC retry attempts
#define RNR_RETRY 7           // RNR retry count
#define MAX_INLINE_DATA 256   // Inline data threshold
```

## Usage Examples

### Basic Send-Receive

```bash
# Terminal 1 (Server)
./rdma send

# Terminal 2 (Client)  
./rdma send <server_hostname>
```

### RDMA Write Operation

```bash
# Terminal 1 (Server)
./rdma write

# Terminal 2 (Client)
./rdma write <server_hostname>
```

### RDMA Read Operation

```bash
# Terminal 1 (Server)
./rdma read

# Terminal 2 (Client)
./rdma read <server_hostname>
```

### Lambda Function Execution

```bash
# Compile lambda function
gcc -shared -fPIC lambda-run.c -o lambda-run.so

# Terminal 1 (Server)
./rdma lambda

# Terminal 2 (Client)
./rdma lambda <server_hostname>
```

## Error Handling

### Error Classification

```c
typedef enum {
    RDMA_SUCCESS = 0,
    RDMA_ERR_DEVICE,        // Device-related errors
    RDMA_ERR_RESOURCE,      // Resource allocation errors  
    RDMA_ERR_COMMUNICATION  // Communication errors
} rdma_status_t;
```

### Error Handling Patterns

1. **Resource Errors**: Automatic cleanup and graceful degradation
2. **Communication Errors**: Retry logic with exponential backoff
3. **Device Errors**: Comprehensive error reporting and recovery suggestions

### Debugging Support

```c
#define DEBUG_LOG(fmt, ...) \
    do { if (DEBUG) fprintf(stderr, "[DEBUG][%s:%d] " fmt "\n", \
        __FILE__, __LINE__, ##__VA_ARGS__); } while (0)
```

## Performance Considerations

### Optimization Techniques

1. **Memory Registration**: Pre-register memory regions to avoid runtime overhead
2. **Completion Batching**: Poll multiple completions when possible
3. **Inline Data**: Use inline sends for small messages (< 256 bytes)
4. **Memory Alignment**: Align buffers to cache line boundaries

### Scalability Factors

1. **Queue Pair Limits**: Hardware-dependent QP limits
2. **Memory Registration Limits**: Physical memory registration constraints
3. **Completion Queue Sizing**: Balance memory usage vs. batching efficiency

### Latency Optimization

1. **Polling vs. Events**: Busy polling for lowest latency
2. **CPU Affinity**: Pin threads to specific CPU cores
3. **NUMA Awareness**: Allocate memory on local NUMA nodes

## Security Considerations

### Memory Protection

1. **Protection Domains**: Isolate memory regions between applications
2. **Access Control**: Restrict memory access permissions per operation type
3. **Remote Keys**: Validate remote memory keys before operations

### Lambda Mode Security

1. **Code Validation**: Implement code signature verification
2. **Sandboxing**: Execute functions in restricted environments  
3. **Resource Limits**: Implement execution time and memory limits

## Future Enhancements

### Planned Features

1. **Multi-threading Support**: Concurrent operation handling
2. **Connection Pooling**: Reuse connections for multiple operations
3. **Reliability Improvements**: Enhanced error recovery and reconnection
4. **Performance Monitoring**: Built-in latency and throughput metrics

### Extensibility Points

1. **Custom Operation Types**: Framework for adding new RDMA operations
2. **Pluggable Serialization**: Support for different data serialization formats
3. **Transport Abstraction**: Support for different RDMA transports (UC, UD)

---

*This documentation provides a comprehensive technical overview of the RDMA Communication Library. For implementation details, refer to the source code and inline documentation.*