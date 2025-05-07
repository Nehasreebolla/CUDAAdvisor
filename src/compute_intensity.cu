#include <stdio.h>

// Device counters
__device__ unsigned long long total_flops = 0;
__device__ unsigned long long total_bytes = 0;

extern "C" __device__ void recordFlop(unsigned long long count) {
    atomicAdd(&total_flops, count);
}

extern "C" __device__ void recordBytesAccess(unsigned long long bytes) {
    atomicAdd(&total_bytes, bytes);
}

extern "C" void printComputeIntensity() {
    unsigned long long flops, bytes;
    cudaMemcpyFromSymbol(&flops, total_flops, sizeof(unsigned long long));
    cudaMemcpyFromSymbol(&bytes, total_bytes, sizeof(unsigned long long));

    printf("\nCompute Intensity Results:\n");
    printf("Total FLOPs: %llu\n", flops);
    printf("Total Bytes: %llu\n", bytes);
    if (bytes > 0) {
        printf("Compute Intensity: %.2f FLOPs/byte\n", (double)flops/bytes);
    }
    
    // Reset counters before kernel launch
    unsigned long long zero = 0;
    cudaMemcpyToSymbol(total_bytes, &zero, sizeof(unsigned long long));
    cudaMemcpyToSymbol(total_flops, &zero, sizeof(unsigned long long));
}
