#include <stdio.h>

#ifdef __CUDA_ARCH__
// Hello world device function
__attribute__((noinline))
extern "C" __device__ void print(){

    printf("Hello world from device function!!\n");
}
#else

__attribute__((noinline))
extern "C" __host__ void print(){

    printf("Hello World from host function!!\n");
}
#endif