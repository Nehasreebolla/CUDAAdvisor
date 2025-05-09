SM =sm_75
CP =compute_75
LLVM = /home/nitya/llvm-project

PASS =$(LLVM)/build/lib/LLVMCudaAdvisor.so
UPATH =$(LLVM)/llvm/lib/Transforms/CUDAAdvisor/src/

INSTRU_TASK = -instru-kernel-memory
ANA_TASK = MD_MODE


LLVM_DIR := $(LLVM)/build
CLANG := $(LLVM_DIR)/bin/clang
CLANGPP := $(LLVM_DIR)/bin/clang++
OPTCC := $(LLVM_DIR)/bin/opt
LLC := $(LLVM_DIR)/bin/llc
LLVM_LINK := $(LLVM_DIR)/bin/llvm-link
LLVM_DIS := $(LLVM_DIR)/bin/llvm-dis


# You can use these if you set PATH=$PATH:$(LLVM_DIR)/bin
# Run & Add this command to .bashrc file `export PATH=$PATH:$(LLVM_DIR)/bin`
CLANG := clang
CLANGPP := clang++
OPTCC := opt
LLC := llc
LLVM_LINK := llvm-link
LLVM_DIS := llvm-dis

# Define source files for the shared library
PRINT_SRC=print.cpp
CALC_SRC=calc.cpp
ANSF_SRC=ansf.cu
HOST_SO =libprint.so

EXE=add_vector
SRC=$(EXE).cu
PASS=$(LLVM_DIR)/lib/LLVMCudaAdvisor.so
INSTRU=-instru-host 
CFLAGS = -Wnvcc-compat --gcc-toolchain=/usr   -L/usr/local/cuda/lib64   -L/usr/lib/gcc/x86_64-linux-gnu/10   -lcudart -lstdc++   -isystem /usr/include/c++/10   -isystem /usr/include/x86_64-linux-gnu/c++/10   -isystem /usr/include/c++/10/backward   -isystem /usr/lib/gcc/x86_64-linux-gnu/10/include
DEBUG = -g	
OPT = -O2 -std=c++11
GC = g++ -std=c++11 # for unordered_map

## Used in make all
PASS_SO ?= $(LLVM_DIR)/lib/LLVMCudaAdvisor.so
BIN_FILE=syrk
SRC_FILE=$(BIN_FILE).cu

INSTRU = -instru-kernel-sig -instru-kernel-basic -instru-kernel-call-path $(INSTRU_TASK) -constmerge

PASS_NAME ?= -memory-bandwidth
LINE_RANGE_DEVICE ?= -line-range=131-132
LINE_RANGE_HOST ?= -line-range=25-30
FNAME ?= -fname=syrk_kernel # Function name to instrument

all: $(BIN_FILE)

# Host Side
$(BIN_FILE).bc: $(SRC_FILE) $(BIN_FILE).fatbin
	$(CLANGPP) -stdlib=libc++ -Wall -Werror $(BIN_FILE).cu -march=x86-64 --cuda-host-only -relocatable-pch \
        -Xclang -fcuda-include-gpubinary -Xclang $(BIN_FILE).fatbin  -g -c -emit-llvm\
		-isystem /usr/include/c++/10   -isystem /usr/include/x86_64-linux-gnu/c++/10   -isystem /usr/include/c++/10/backward   -isystem /usr/lib/gcc/x86_64-linux-gnu/10/include\
		-fPIC
# $(OPTCC) -enable-new-pm=0 -load $(PASS_SO) $(PASS_NAME) \
# -fname=addVectors -line-range=21-22\
# $(BIN_FILE).bc -o $(BIN_FILE).bc
# Instrument host side code
# $(OPTCC) -enable-new-pm=0  -S -load $(PASS_SO) -instru-host-sig \
# 	$(BIN_FILE).bc -o $(BIN_FILE).bc
# $(OPTCC) -enable-new-pm=0  -S -load $(PASS_SO) -instru-host -instru-host-measure -const-merge \
# 	$(BIN_FILE).bc -o $(BIN_FILE).bc

$(BIN_FILE).o: $(BIN_FILE).bc
	$(LLC) -march=x86-64 -relocation-model=pic $(BIN_FILE).bc -o $(BIN_FILE).s
	$(CLANGPP) -c -Wall -fPIC $(BIN_FILE).s -o $(BIN_FILE).o

# GPU Side
$(BIN_FILE)-cuda-nvptx64-nvidia-cuda-$(SM).bc: $(SRC_FILE) 

	$(CLANGPP) -g -x cuda --cuda-device-only -emit-llvm  -c \
	--cuda-gpu-arch=$(SM) \
	-I/usr/local/cuda/include \
	-include __clang_cuda_runtime_wrapper.h \
	-isystem /usr/include/c++/10   -isystem /usr/include/x86_64-linux-gnu/c++/10   -isystem /usr/include/c++/10/backward   -isystem /usr/lib/gcc/x86_64-linux-gnu/10/include \
	$(BIN_FILE).cu -o $(BIN_FILE)-cuda-nvptx64-nvidia-cuda-$(SM).bc
# $(LLVM_LINK) $(BIN_FILE)-cuda-nvptx64-nvidia-cuda-$(SM).bc ansf.bc -o=$(BIN_FILE)-cuda-nvptx64-nvidia-cuda-$(SM).bc

# Instrument GPU side code
	$(OPTCC) -enable-new-pm=0 -load $(PASS_SO) $(PASS_NAME) \
		$(FNAME) $(LINE_RANGE_DEVICE)\
		$(BIN_FILE)-cuda-nvptx64-nvidia-cuda-$(SM).bc -o $(BIN_FILE)-cuda-nvptx64-nvidia-cuda-$(SM).bc


$(BIN_FILE).ptx: $(BIN_FILE)-cuda-nvptx64-nvidia-cuda-$(SM).bc
	$(LLC) -march=nvptx64 -mcpu=$(SM) $(BIN_FILE)-cuda-nvptx64-nvidia-cuda-$(SM).bc -o $(BIN_FILE).ptx
	sed -i -E 's/\.file[[:space:]]+([0-9]+)[[:space:]]+"([^"]+)"[[:space:]]+"([^"]+)"/.file \1 "\2\/\3"/' $(BIN_FILE).ptx

$(BIN_FILE).ptx.o: $(BIN_FILE).ptx
	ptxas -m64 --gpu-name=$(SM) $(BIN_FILE).ptx -o $(BIN_FILE).ptx.o

$(BIN_FILE).fatbin: $(BIN_FILE).ptx.o
	fatbinary --64 --create $(BIN_FILE).fatbin --image=profile=$(SM),file=$(BIN_FILE).ptx.o \
        --image=profile=$(CP),file=$(BIN_FILE).ptx -link

$(BIN_FILE)_dlink.o: $(BIN_FILE).fatbin
	nvcc $(BIN_FILE).fatbin -gencode arch=$(CP),code=$(SM) \
        -dlink -o $(BIN_FILE)_dlink.o -lcudart -lcudart_static -lcudadevrt

# Link both object files together (either nvcc or clang works here):
$(BIN_FILE): $(BIN_FILE).o $(BIN_FILE)_dlink.o
	nvcc $(BIN_FILE).o $(BIN_FILE)_dlink.o -o $(BIN_FILE) -arch=$(SM) -L/usr/lib/llvm-14/lib -lc++

# ansf.bc :  $(ANSF_SRC) $(UPATH)/../common.h  $(UPATH)/types.h
# 	clang++ -g $(CFLAGS) --cuda-gpu-arch=$(SM) -O2 -std=c++14 -c --cuda-device-only -emit-llvm $(ANSF_SRC) -o ansf.bc

clean:
	rm -f *.o *.bc *.fatbin *.ptx *.cubin *.ll final_executable $(EXE) $(BIN_FILE) *.s
