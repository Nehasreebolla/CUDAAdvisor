# Dynamic profiling at sub-kernel level using LLVM

CUDAAdvisor is a profiling tool for GPU.

## Hardware Depebdencies
  - GPU server with cc 3.5 or later
 
## Software Depebdencies
  - LLVM 14.0 
  - CUDA 11.5 

## Installation
Your system should have LLVM installed.

`$ cd [llvm source tree]/lib/Transforms/`

`$ git clone https://github.com/sderek/CUDAAdvisor.git`

And copy the fowllowing lines into `lib/Transforms/CMakeLists.txt`

`add_subdirectory(CUDAAdvisor)`

Go to the top level of your LLVM build directory and rebuild, you should get a new file `lib/LLVMCudaAdvisor.so`. You are able to use the **opt** tool to access it. There is an exmple in `src/` subdirectory and a template is provided.

## Running
### Memory Bandwidth
Use pass -memory-bandwidth on the device LLVM IR. (see make file)
  - fname=<kernel-name>
  - line-range=<start>-<end>
In the src.cu file, add #include "bandwidth_helpers.cu"
For printing the results, add computeBandwidth() after cudaSynchronise() call.
### Compute Intensity
Use pass -compute-intensity  on the device LLVM IR. (see make file)
  - fname=<kernel-name>
  - line-range=<start>-<end>
In the src.cu file, add #include "compute_intensity.cu"
For printing the results, add printComputeIntensity() after cudaSynchronise() call.


## Authors
Nitya Bhamidipaty 
Bolla Nehasree
Nethi Keerthana
