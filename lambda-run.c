/**
 * @file lambda-run.c
 * @brief Example lambda function for RDMA remote execution
 *
 * Provides a simple string processing function that can be executed
 * remotely using the RDMA lambda mode. This function converts
 * input text to uppercase and demonstrates the lambda function
 * interface requirements.
 */

#include <string.h>

/**
 * @brief Processes input data by converting text to uppercase
 *
 * @param input Pointer to input buffer containing null-terminated string
 * @param input_size Size of input buffer in bytes
 * @param output Pointer to output buffer for result
 * @param output_size Pointer to store size of output data
 * @return int 0 on success, non-zero on failure
 *
 * Function signature required for RDMA lambda execution:
 * - Receives input data in registered memory region
 * - Processes data (converts to uppercase)
 * - Writes result to output buffer
 * - Updates output_size with result length
 */
int process_data(void* input, size_t input_size, 
                void* output, size_t* output_size) {
    const char* in_str = (const char*)input;
    char* out_str = (char*)output;

    size_t i;
    
    // Manual uppercase conversion without libc functions
    for (i = 0; i < input_size && in_str[i]; i++) {
        char c = in_str[i];
        out_str[i] = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
    
    // Ensure null termination and set output size
    out_str[i] = '\0';
    *output_size = i + 1;  // Include null terminator in size
    
    return 0;  // Success
}