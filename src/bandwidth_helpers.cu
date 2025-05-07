#include <cuda_runtime.h>
#include <stdio.h>
#include "bandwidth_helpers.h"

// Define the device variables here (only once in the project)
__device__ unsigned long long total_bytes = 0;
__device__ unsigned long long total_time_ns = 0;

// Rest of your implementation...
extern "C" __device__ unsigned long long getGpuTime() {
    return clock64();
}

extern "C" __device__ void recordMemAccess(unsigned long long bytes, unsigned long long time) {
    atomicAdd(&total_bytes, bytes);
    atomicAdd(&total_time_ns, time);
}

__host__ void computeBandwidth() {
    unsigned long long bytes, time_ns;
    cudaMemcpyFromSymbol(&bytes, total_bytes, sizeof(unsigned long long));
    cudaMemcpyFromSymbol(&time_ns, total_time_ns, sizeof(unsigned long long));
    if (time_ns > 0) {
        double bandwidth = (double)bytes / (time_ns * 1e-9) / 1e9; // GB/s
        printf("Bandwidth: %.2f GB/s\n", bandwidth);
    } else {
        printf("No bandwidth data available\n");
    }
}