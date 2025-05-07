// add_vector.cu

#include <iostream>
#include <stdio.h>
#include <cuda_runtime.h>
#include "my.cu"


#define CHECK_CUDA_ERROR(call)                                           \
    do {                                                                 \
        cudaError_t err = call;                                          \
        if (err != cudaSuccess) {                                        \
            std::cerr << "CUDA error at " << __FILE__ << ":" << __LINE__ \
                      << " - " << cudaGetErrorString(err) << std::endl; \
            exit(EXIT_FAILURE);                                          \
        }                                                                \
    } while (0)

extern "C" __global__ void addVectors(float *a, float *b, float *c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (i < n){
        c[i] = a[i] + b[i];
    
        if (c[i] > 10){
            a[i] = 10;
        }
        else {
            a[i] = 11;
        }

        for (int j = 0; j < 10; ++j) {
            a[i] += 1;
            c[i] = c[i]*c[i];
        }

        b[i] = 2;
    }
    
}

int main() {
    int n = 10; // 1M elements
    size_t size = n * sizeof(float);

    // Allocate host memory
    float *h_a = new float[n];
    float *h_b = new float[n];
    float *h_c = new float[n];

    // Initialize input vectors
    for (int i = 0; i < n; ++i) {
        h_a[i] = 1.0f;
        h_b[i] = 2.0f;
    }

    // Allocate device memory
    float *d_a, *d_b, *d_c;
    CHECK_CUDA_ERROR(cudaMalloc(&d_a, size));
    CHECK_CUDA_ERROR(cudaMalloc(&d_b, size));
    CHECK_CUDA_ERROR(cudaMalloc(&d_c, size));

    // Copy data from host to device
    CHECK_CUDA_ERROR(cudaMemcpy(d_a, h_a, size, cudaMemcpyHostToDevice));
    CHECK_CUDA_ERROR(cudaMemcpy(d_b, h_b, size, cudaMemcpyHostToDevice));

    // Launch kernel
    int threadsPerBlock = 256;
    int blocksPerGrid = (n + threadsPerBlock - 1) / threadsPerBlock;
    addVectors<<<blocksPerGrid, threadsPerBlock>>>(d_a, d_b, d_c, n);

    // Check for kernel launch errors and sync errors
    CHECK_CUDA_ERROR(cudaGetLastError());   // Kernel launch
    CHECK_CUDA_ERROR(cudaDeviceSynchronize()); // Wait for kernel to finish

    // Copy result from device to host
    CHECK_CUDA_ERROR(cudaMemcpy(h_c, d_c, size, cudaMemcpyDeviceToHost));

    // Verify result
    for (int i = 0; i < 10; ++i)
        std::cout << "h_c[" << i << "] = " << h_c[i] << std::endl;

    // Cleanup
    CHECK_CUDA_ERROR(cudaFree(d_a));
    CHECK_CUDA_ERROR(cudaFree(d_b));
    CHECK_CUDA_ERROR(cudaFree(d_c));
    delete[] h_a;
    delete[] h_b;
    delete[] h_c;

    return 0;
}
