// Device variables
__device__ unsigned long long total_bytes_accessed = 0;
__device__ unsigned long long total_memory_time_ns = 0;

// Bandwidth measurement functions
extern "C" {
    __device__ unsigned long long getGpuTime() {
        return clock64();
    }

    __device__ __noinline__ void recordMemAccess(unsigned long long bytes, unsigned long long cycles) {
        atomicAdd(&total_bytes_accessed, bytes);
        atomicAdd(&total_memory_time_ns, cycles);
    }

}

extern "C" void computeBandwidth() {
    unsigned long long bytes, time_ns;
    cudaMemcpyFromSymbol(&bytes, total_bytes_accessed, sizeof(unsigned long long));
    cudaMemcpyFromSymbol(&time_ns, total_memory_time_ns, sizeof(unsigned long long));
    double bandwidth = ((double)bytes / 1e9) / (time_ns * 1e-9);  // GB/s
    
    printf("Memory Bandwidth: %.8f GB/cycles\n", bandwidth);
    printf("Total Bytes Accessed: %llu bytes\n", bytes);
    printf("Total Time: %.8llu cycles\n", time_ns);  // ns 
}


