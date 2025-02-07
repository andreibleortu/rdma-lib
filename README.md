# RDMA Communication Library

A comprehensive implementation of RDMA (Remote Direct Memory Access) communication patterns featuring multiple operation modes and a novel remote function execution capability.

## Features

### Operation Modes
- **Send/Receive**: Traditional two-sided communication
- **RDMA Write**: One-sided write operations with immediate data
- **RDMA Read**: One-sided read operations with range selection
- **Lambda Mode**: Remote function execution over RDMA

### Core Capabilities
- Full RDMA resource management
- RoCEv2 (RDMA over Converged Ethernet) support
- Connection management and QP state transitions
- Memory registration and buffer management
- Comprehensive error handling and cleanup
- Signal handling for graceful termination

## Technical Details

### RDMA Resources
- Protection Domain (PD) for memory protection
- Queue Pairs (QP) in Reliable Connection (RC) mode
- Completion Queue (CQ) for operation completion notification
- Memory Regions (MR) with appropriate access flags
- Global ID with RoCEv2 support

### Buffer Management
- Configurable buffer size (default: 4096 bytes)
- Support for scatter-gather operations
- Automatic memory registration with appropriate access flags
- Protected memory regions for lambda function execution

### Queue Pair States
1. **INIT**: Initial QP setup with access flags
2. **RTR** (Ready to Receive): Path MTU and destination info setup
3. **RTS** (Ready to Send): Timeout and retry parameters

### Operation Modes Detail

#### Send/Receive
- Traditional two-sided communication
- Built-in acknowledgment mechanism
- Interactive message exchange capability

#### RDMA Write
- One-sided write operations
- Immediate data for completion notification
- Direct memory placement without remote CPU involvement

#### RDMA Read
- One-sided read operations
- Range-based memory access
- Zero-copy data transfer

#### Lambda Mode
- Remote function execution framework
- Dynamic code loading and execution
- Memory protection for executable regions
- Bi-directional data transfer

## Build Requirements

- Linux operating system
- RDMA-capable network interface (RoCE or InfiniBand)
- libibverbs development library
- GCC or compatible C compiler

## Installation

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

Server mode:
```bash
./rdma <mode>
# Modes: send, write, read, lambda
```

Client mode:
```bash
./rdma <mode> <server_ip>
# Example: ./rdma write 10.0.0.1
```

## Operation Mode Examples

### Send/Receive Mode
```bash
# Server
./rdma send

# Client
./rdma send 10.0.0.1
> Hello, RDMA!
Server acknowledged
```

### RDMA Write Mode
```bash
# Server
./rdma write

# Client
./rdma write 10.0.0.1
> Writing directly to server memory
Message sent successfully
```

### RDMA Read Mode
```bash
# Server
./rdma read
Enter text to store: Hello from server memory!

# Client
./rdma read 10.0.0.1
Enter range (start end): 0 5
Read data (6 bytes from position 0): Hello
```

### Lambda Mode
```bash
# Server
./rdma lambda

# Client
./rdma lambda 10.0.0.1
# Executes pre-compiled function remotely
```

## Technical Implementation Notes

### Memory Management
- Pre-registered memory regions for optimal performance
- Proper alignment for RDMA operations
- Protected executable memory for lambda functions

### Connection Management
- Out-of-band TCP connection for initial setup
- QP information exchange protocol
- Reliable connection establishment with retry logic

### Error Handling
- Comprehensive error checking and reporting
- Resource cleanup on failure
- Signal handling for graceful termination

### Performance Considerations
- Minimized memory registration operations
- Efficient buffer management
- Proper completion queue sizing

## Project Structure

```
.
├── common.h/c       # Core RDMA functionality
├── rdma.c          # Main program entry point
├── send-receive/   # Two-sided communication
├── rdma-write/     # One-sided write operations
├── rdma-read/      # One-sided read operations
└── lambda/         # Remote function execution
```

## Security Considerations

- Memory protection for lambda function execution
- Access control through RDMA permissions
- Proper cleanup of sensitive resources
- Input validation for all operations

## Contributing

Contributions are welcome! Here's how you can contribute to this project:
1. Fork the repository
2. Create a feature branch
3. Implement changes with proper documentation
4. Submit a pull request

## License

GNU General Public License v3 - See LICENSE file for details

## Authors

Andrei-Alexandru Bleorțu