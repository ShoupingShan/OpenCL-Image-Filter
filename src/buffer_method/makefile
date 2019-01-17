CC = g++
FILE = main
CFLAGS = -g -Wall -w
SRCS = $(FILE).cpp
PROG = $(FILE)
#INCLUDE = /opt/intel/intel-opencl-1.2-7.0.0.2567/opencl-1.2-sdk-7.0.0.2567/include
#LIBRARY = /opt/intel/intel-opencl-1.2-7.0.0.2567/opencl-1.2-sdk-7.0.0.2567/lib64
LIBRARY = /usr/local/cuda-9.1/lib64
INCLUDE = /usr/local/cuda-9.1/include/CL
LFLAG = -L$(LIBRARY)
IFLAG = -I$(INCLUDE)
OPENCV = `pkg-config opencv --cflags --libs`
LIBS = $(OPENCV) 

$(PROG):$(SRCS)
	$(CC) $(CFLAGS) -o $(PROG) $(SRCS) $(LIBS) $(IFLAG) $(LFLAG) -lOpenCL

clean:
	rm  $(PROG)