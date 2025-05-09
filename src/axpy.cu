#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include "bandwidth_helpers.cu"
#include "compute_intensity.cu"

// Device helper functions
__device__ float foo_CC(float a) {
    return a * 0.9;
}

__device__ int foo_DD(float a) {
    if (threadIdx.x < 2 || threadIdx.y > 2)
        return (int)a;
    else
        return a + 2;
}

__device__ float foo_BB(float a) {
    if (threadIdx.x > 3 || threadIdx.y > 11)
        return a + foo_CC(a);
    else
        return a + (float)foo_DD(a) / 2;
}

__device__ float foo_AA(float a, float b) {
    if (threadIdx.x < 8 || threadIdx.y > 4)
        return a * 3.1415 + 1;
    else
        return (b * a) * 0.5 + foo_BB(b);
}

// Kernels (unchanged)
__global__ void axpy_kernel1(float a, float* x, float* y) {

	//getGpuTime();
    int blockId = blockIdx.x + blockIdx.y * gridDim.x;
    int threadId = blockId * (blockDim.x * blockDim.y) + (threadIdx.y * blockDim.x) + threadIdx.x;
    int index = threadId;
	bool i = 1;
	bool arr[10];
	arr[0] = i;
	bool b = arr[0];
	int shifted = b << 1; // shifted = 2 if b was true, 0 if false
	shifted = shifted + 1;
	char name[4];
	name[1] = 'a';
	name[2] = 'b';
	char c = name[2];
	c = c+1;

    y[index] = x[index] * 0.3;
    if (index > 2)
        y[index] += 99;
    else
        y[index] += 999 + foo_CC(a);
}

__global__ void axpy_kernel2(float a, float* x, float* y) {
    int blockId = blockIdx.x + blockIdx.y * gridDim.x;
    int threadId = blockId * (blockDim.x * blockDim.y) + (threadIdx.y * blockDim.x) + threadIdx.x;
    int index = threadId;

    float aa = y[index] + x[index] + 1.1;
    float b = 0.5 * y[index] + 0.25 * x[index] + 1.0;
    y[index] += (x[index] * 1.67 + foo_AA(aa, b));
}

// Main function
int main(int argc, char* argv[]) {
    float a = 2.0f;
    cudaSetDevice(0);

    if (argc != 5) {
        printf("usage: ./axpy [blocks_x] [blocks_y] [space]threads_x] [threads_y]\n");
        exit(1);
    }

    int blocksx = atoi(argv[1]);
    int blocksy = atoi(argv[2]);
    int kDataLenx = atoi(argv[3]);
    int kDataLeny = atoi(argv[4]);

    int sizen = blocksx * blocksy * kDataLenx * kDataLeny;
    cudaDeviceSetLimit(cudaLimitMallocHeapSize, 1024 * 1024 * 500);

    float* host_x = (float*)malloc(sizen * sizeof(float));
    float* host_y = (float*)malloc(sizen * sizeof(float));
    void* host_newbu = (void*)malloc(1000);

    for (int ii = 0; ii < sizen; ii++) {
        host_x[ii] = ii % 8;
        host_y[ii] = ii % 5;
    }

    float* device_x;
    float* device_y;
    cudaMalloc((void**)&device_x, sizen * sizeof(float));
    cudaMalloc((void**)&device_y, sizen * sizeof(float) + 18);

    cudaMemcpy(device_x, host_x, sizen * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(device_y, host_y, sizen * sizeof(float), cudaMemcpyHostToDevice);

    dim3 CTAs(blocksx, blocksy);
    dim3 Threads(kDataLenx, kDataLeny);
    std::cout << "launching kernel...\n";

    // Reset counters before kernel launch
    unsigned long long zero = 0;
    cudaMemcpyToSymbol(total_bytes_accessed, &zero, sizeof(unsigned long long));
    cudaMemcpyToSymbol(total_memory_time_ns, &zero, sizeof(unsigned long long));

    axpy_kernel1<<<CTAs, Threads>>>(a, device_x, device_y);
    cudaDeviceSynchronize();
	computeBandwidth();
	printComputeIntensity();

    axpy_kernel2<<<CTAs, Threads>>>(a, device_x, device_y);
    cudaDeviceSynchronize();
    //computeBandwidth();

    cudaMemcpy(host_y, device_y, sizen * sizeof(float), cudaMemcpyDeviceToHost);

    free(host_newbu);
    cudaFree(device_x);
    cudaFree(device_y);

    int verify = 0;
    for (int ii = 0; ii < 8; ii++)
        std::cout << "y[" << ii << "] = " << host_y[ii] << "\n";

    for (int ii = 0; ii < sizen; ii++) {
        if (host_y[ii] == ii % 5)
            verify++;
    }
    std::cout << "\n\n[TOOL verify] There are a total of\t" << verify << " incorrect numbers." << std::endl;
    if (verify == 0)
        std::cout << "[TOOL verify] passed!" << std::endl << std::endl;

    cudaDeviceReset();
    free(host_x);
    free(host_y);
    return 0;
}
