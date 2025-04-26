LLVM_DIR := /home/nitya/llvm-project/build
CLANG := $(LLVM_DIR)/bin/clang
CLANGPP := $(LLVM_DIR)/bin/clang++
OPTCC := $(LLVM_DIR)/bin/opt
LLC := $(LLVM_DIR)/bin/llc
LLVM_LINK := $(LLVM_DIR)/bin/llvm-link
LLVM_DIS := $(LLVM_DIR)/bin/llvm-dis

include ../env.mk
include testtask.mk

SRC=add_vector.cu
EXE=add_vector# has to be same as the name of the source file without the extension
PASS=$(LLVM_DIR)/lib/LLVMCudaAdvisor.so
INSTRU=-instru-host -instru-kernel-memory
CFLAGS = --gcc-toolchain=/usr   -L/usr/local/cuda/lib64   -L/usr/lib/gcc/x86_64-linux-gnu/10   -lcudart -lstdc++   -isystem /usr/include/c++/10   -isystem /usr/include/x86_64-linux-gnu/c++/10   -isystem /usr/include/c++/10/backward   -isystem /usr/lib/gcc/x86_64-linux-gnu/10/include
DEBUG = -g
OPT = -O2 -std=c++11

# Define source files for the shared library
PRINT_SRC=print.cpp
CALC_SRC=calc.cpp
ANSF_SRC=ansf.cu
HOST_SO =libprint.so
GC = g++ -std=c++11 #for unordered_map


all: $(HOST_SO) device_fatbin.cu opt_host.bc

	$(CLANGPP) -c device_fatbin.cu -o device_fatbin.o \
    --cuda-path=/usr/local/cuda \
    -I/usr/local/cuda/include\
	$(CFLAGS)

	$(CLANGPP) opt_host.bc device_fatbin.o -o $(EXE) \
    -L. -lprint \
    -L/usr/local/cuda/lib64 -lcudart \
    -Wl,-rpath,. -Wl,-rpath,/usr/local/cuda/lib64\
	$(CFLAGS)

# for only host code only used during dev
# $(CLANGPP) opt_host.bc -L. -lprint -o final_exec \
# -Wl,-rpath,. \
# -lcudart -L/usr/local/cuda/lib64\
# $(CFLAGS)

run:
	$(CLANG) -o final_executable host.bc device.ptx -fopenmp -fopenmp-targets=nvptx64 $(CFLAGS)

host.bc: $(SRC)
# Compile host code to LLVM bitcode
	$(CLANGPP) -O0 -emit-llvm -c -g $(SRC) -o host.bc --cuda-host-only \
		--cuda-path=/usr/local/cuda \
		--cuda-gpu-arch=$(SM) \
		-I/usr/local/cuda/include\
		-isystem /usr/include/c++/10   -isystem /usr/include/x86_64-linux-gnu/c++/10   -isystem /usr/include/c++/10/backward   -isystem /usr/lib/gcc/x86_64-linux-gnu/10/include

device.bc: $(SRC)
# Compile device code to LLVM bitcode
	$(CLANGPP) -O0 -emit-llvm -c -g $(SRC) -o device.bc --cuda-device-only \
		--cuda-path=/usr/local/cuda \
		--cuda-gpu-arch=sm_75 \
		-I/usr/local/cuda/include\
		-isystem /usr/include/c++/10   -isystem /usr/include/x86_64-linux-gnu/c++/10   -isystem /usr/include/c++/10/backward   -isystem /usr/lib/gcc/x86_64-linux-gnu/10/include

linked_device.bc: device.bc ansf.bc
	$(LLVM_LINK) device.bc ansf.bc -o linked_device.bc

opt_host.bc: host.bc
# cp host.bc opt_host.bc
	$(OPTCC) -enable-new-pm=0 -load $(PASS) -device-pass -fname=my_func -line_range=12-25 host.bc -o opt_host.bc

opt_device.bc: linked_device.bc
# cp linked_device.bc opt_device.bc
	$(OPTCC) -enable-new-pm=0 -load $(PASS) $(INSTRU) linked_device.bc -o opt_device.bc

device.ptx: opt_device.bc
# Compile device code to PTX
	$(LLC) -march=nvptx64 -mcpu=sm_75 -filetype=asm opt_device.bc -o device.ptx
	ptxas --gpu-name $(SM) device.ptx -o device.o -v -maxrregcount=31 #for verbose, resources check

device.fatbin: device.ptx
	fatbinary --create=device.fatbin --image=profile=compute_75,file=device.ptx  --compress=true

device_fatbin.cu: device.fatbin
	xxd -i device.fatbin > device_fatbin.cu
# add registerFatbin and unregisterFatbin functions
	cat fatbin_stub.txt >> device_fatbin.cu

# only c code testing (not cuda)
test: $(HOST_SO) 
	$(CLANGPP) -g -c -emit-llvm main.cpp -o main.bc
	$(OPTCC) -enable-new-pm=0 -load $(PASS) $(INSTRU) -S main.bc -o main.instrumented.bc
	$(LLC) -filetype=obj main.instrumented.bc -o main.o
	$(CLANGPP) host.o -L. -lprint -Wl,-rpath,. $(CFLAGS) -o program

$(HOST_SO) : $(UPATH)/print.cpp  $(UPATH)/../common.h $(UPATH)/types.h $(UPATH)/calc.cpp
	$(GC) -c $(DEBUG) $(OPT) -Wall -D $(ANA_TASK) -fPIC -lm -fopenmp $(UPATH)/print.cpp -o $(UPATH)/print.o
	$(GC) $(UPATH)/print.o -shared -o $(HOST_SO)

ansf.bc :  $(ANSF) $(UPATH)/../common.h  $(UPATH)/types.h
	$(CLANGPP) $(DEBUG) $(OPT) -c --cuda-device-only -emit-llvm $(ANSF_SRC) -o ansf.bc --cuda-path=/usr/local/cuda   --cuda-gpu-arch=$(SM)   $(CFLAGS)

cu:
	$(CLANG) --version
	$(CLANGPP) $(SRC) -o $(EXE)   --cuda-path=/usr/local/cuda   --cuda-gpu-arch=$(SM) $(CFLAGS)


dis: host.bc device.bc device.link.bc # only used for debugging
	$(LLVM_DIS) host.bc -o host.ll
	$(LLVM_DIS) device.bc -o device.ll
	$(LLVM_DIS) device.link.bc -o device.link.ll

clean:
	rm -f *.o *.bc *.fatbin *.ptx *.cubin *.ll final_executable $(EXE)


## old code ignore
alternate: ansf.bc $(HOST_SO)
	$(CLANGPP) -g -c -emit-llvm  $(CFLAGS) $(SRC) --cuda-path=/usr/local/cuda --cuda-gpu-arch=$(SM) 
	echo "command 1 done"
	$(LLVM_LINK) "$(EXE)-cuda-nvptx64-nvidia-cuda-sm_75.bc" ansf.bc -o="device.link.bc"
	echo "command 2 done"		
	$(OPTCC) -enable-new-pm=0 -load $(PASS) $(INSTRU) -o device.bc device.link.bc
	echo "command 3 done"
	$(OPTCC) -enable-new-pm=0 -load $(PASS) $(INSTRU) -o host.bc "$(EXE).bc"
	echo "command 4 done"
	$(LLC) device.bc -march=nvptx64 -mcpu=$(SM) -filetype=asm -o device.ptx
	echo "command 5 done"
	/usr/local/cuda/bin/fatbinary --create=device.fatbin --image=profile=$(CP),file=device.ptx 
	echo "command 6 done"
	ld -r -b binary -o fatbin.o device.fatbin
	echo "command 7 done"
	$(CLANG) -c fatbin_wrapper.c -o fatbin_wrapper.o
	echo "command 8 done"	
	$(LLC) -filetype=obj $(EXE).bc -o host.o
	echo "command 9 done"	
	$(CLANGPP) host.o -L. -lprint -Wl,-rpath,. -lcudart -L/usr/local/cuda/lib64  $(CFLAGS) -o $(EXE) 
	$(CLANGPP) host.o fatbin_wrapper.o fatbin.o -lcudart -L/usr/local/cuda/lib64 -o $(EXE) $(CFLAGS)
	echo "command 10 done"

