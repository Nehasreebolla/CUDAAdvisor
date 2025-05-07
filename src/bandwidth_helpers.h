#ifndef BANDWIDTH_HELPERS_H
#define BANDWIDTH_HELPERS_H

#include <cuda_runtime.h>

extern "C" __device__ unsigned long long getGpuTime();
extern "C" __device__ void recordMemAccess(unsigned long long bytes, unsigned long long time);
__host__ void computeBandwidth();

// Change these to extern declarations
extern __device__ unsigned long long total_bytes;
extern __device__ unsigned long long total_time_ns;

#endif