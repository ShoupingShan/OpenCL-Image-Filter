#ifdef __APPLE__
#include<opencl/opencl.h>
#else
#include<CL/cl.h>
#endif
#include <iostream>
#include<fstream>
#include <opencv2/opencv.hpp>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include<fcntl.h>

void Cleanup( cl_context context, cl_command_queue commandQueue, cl_program program, 
    cl_kernel kernel, cl_mem images[2], cl_sampler sampler);

void LoadImageFromOpenCV(cl_context &context, cv::Mat image, cl_mem &imageObject, size_t height, size_t width, cl_int &err);

cl_program CreateProgram(cl_context context, cl_device_id device, const char* fileName){
    cl_int err;
    cl_program program;

    std::ifstream kernelFile(fileName, std::ios::in);
    if (!kernelFile.is_open())
    {
        std::cerr << "Failed to open file for reading: " << fileName << std::endl;
        return NULL;
    }

    std::ostringstream oss;
    oss << kernelFile.rdbuf();

    std::string srcStdStr = oss.str();
    const char *srcStr = srcStdStr.c_str();
    program = clCreateProgramWithSource(context, 1,
                                        (const char**)&srcStr,
                                        NULL, NULL);
    if (program == NULL)
    {
        std::cerr << "Failed to create CL program from source." << std::endl;
        return NULL;
    }

    err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        // Determine the reason for the error
        char buildLog[16384];
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG,
                              sizeof(buildLog), buildLog, NULL);

        std::cerr << "Error in kernel: " << std::endl;
        std::cerr << buildLog;
        clReleaseProgram(program);
        return NULL;
    }

    return program;
}
cl_context CreateContext(cl_int & err){
    cl_platform_id *platform;
    cl_uint numplatform;
    cl_context context = NULL;
    err=clGetPlatformIDs(0,NULL,&numplatform);  
    platform = (cl_platform_id*)malloc(sizeof(cl_platform_id)*numplatform);
    err = clGetPlatformIDs(numplatform,platform,NULL);
    if (err != CL_SUCCESS || numplatform <= 0)
    {
        std::cerr << "Failed to find any OpenCL platforms." << std::endl;
        return NULL;
    }
    cl_context_properties contextProperties[] =
    {
        CL_CONTEXT_PLATFORM,
        (cl_context_properties)platform[0],
        0
    };
    context = clCreateContextFromType(contextProperties, CL_DEVICE_TYPE_GPU,
                                      NULL, NULL, &err);
    if (err != CL_SUCCESS)
    {
        std::cout << "Could not create GPU context, trying CPU..." << std::endl;
        context = clCreateContextFromType(contextProperties, CL_DEVICE_TYPE_CPU,
                                          NULL, NULL, &err);
        if (err != CL_SUCCESS)
        {
            std::cerr << "Failed to create an OpenCL GPU or CPU context." << std::endl;
            return NULL;
        }
    }

    return context;
    
}

cl_command_queue CraeteCommandQueue(cl_context context, cl_device_id &device){
    cl_int err;
    cl_device_id *devices;
    cl_command_queue commandQueue = NULL;
    size_t numDevices = -1;
    err = clGetContextInfo(context, CL_CONTEXT_DEVICES, 0, NULL, &numDevices);
    if(err != CL_SUCCESS){
        std::cerr<<"Failed get device information.";
        return NULL;
    }
    if(numDevices <=0 ){
        std::cerr<<"No devices available.";
        return NULL;
    }
    devices = new cl_device_id[numDevices/sizeof(cl_device_id)];
    err = clGetContextInfo(context, CL_CONTEXT_DEVICES, numDevices, devices, NULL);
    if(err != CL_SUCCESS){
        std::cerr<<"Fail to get device IDs";
        return NULL;
    }
    commandQueue = clCreateCommandQueueWithProperties(context, devices[0], NULL, &err);
    if(err != CL_SUCCESS){
        std::cerr<<"Fail to create command queue.";
        return NULL;
    }
    device = devices[0];//First device is returned.
    delete [] devices;
    return commandQueue;
}

int main(int argc, char ** argv)
{
    using namespace cv;
    using namespace std;
    if (argc != 2)
    {
        std::cerr << "USAGE: " << argv[0] << " <inputImageFile> " << std::endl;
        return 1;
    }
    std::clock_t start;
    double duration = 0;

    /*Data Input*/
    size_t height = 0;
    size_t width = 0;
    ofstream fout1,fout2,fout3;
    fout1.open("./result/OpenCL_GPU.txt",ios::out);
    fout2.open("./result/Normal.txt",ios::out);
    Mat rgbimage = imread(argv[1]);
    height = rgbimage.rows;
    width = rgbimage.cols;
    if(!rgbimage.data)
    {
        std::cerr << "Fail to load image!" << endl;
        return -1;
    }
    Mat grayimage;
    cvtColor(rgbimage, grayimage, CV_BGR2GRAY);     //convert RGB to gray
    float  filter[3][3]={
        {-1, 0, -1},
        { 0, 4,  0},
        {-1, 0, -1}
    };

    /*Init Platform*/
    cl_context context = 0;
    cl_command_queue commandQueue = 0;
    cl_program program = 0;
    cl_device_id device = 0;
    cl_kernel kernel = 0;
    cl_mem images[2]={0,0};  
    cl_sampler sampler = 0;
    cl_int err;

    //Create an OpenCL Context
    context = CreateContext(err);
    if(err!=CL_SUCCESS){
        std::cerr << "Failed to create OpenCL context." << std::endl;
        return -1;
    }

    //Create a Command Queue 
    commandQueue = CraeteCommandQueue(context, device);
    if(commandQueue == NULL){
        Cleanup(context, commandQueue, program, kernel, images, sampler);
        return -1;
    }
    //Output device information
    cl_char vendor_name[100] = {0};
    cl_char device_name[100] = {0};
    size_t returned_size = 0;
    err = clGetDeviceInfo(device, CL_DEVICE_VENDOR, sizeof(vendor_name), vendor_name, &returned_size);
    err |= clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(device_name), device_name, &returned_size);
    if (err != CL_SUCCESS)
        return -1;
    printf("Connecting to %s %s...\n", vendor_name, device_name);
    //Test device supports images or not.
    cl_bool imageSupport = CL_FALSE;
    clGetDeviceInfo(device, CL_DEVICE_IMAGE_SUPPORT, sizeof(cl_bool), &imageSupport, NULL);
    if(imageSupport != CL_TRUE){
        std::cerr << "OpenCL device does not support images." << std::endl;
        Cleanup(context, commandQueue, program, kernel, images, sampler);
        return -1;
    }
    LoadImageFromOpenCV(context,grayimage, images[0],height, width, err);
    if(err != CL_SUCCESS){
        std::cerr<<"Error create image object";
        Cleanup(context, commandQueue, program, kernel, images, sampler);
        return -1;
    }
    // Create ouput image object
    cl_image_format clImageFormat;
    clImageFormat.image_channel_order = CL_INTENSITY;
    clImageFormat.image_channel_data_type = CL_UNSIGNED_INT8;
    images[1] = clCreateImage2D(context,
                                       CL_MEM_WRITE_ONLY,
                                       &clImageFormat,
                                       width,
                                       height,
                                       0,
                                       NULL,
                                       &err);
    if (err != CL_SUCCESS)
    {
        std::cerr << "Error creating output image object." << std::endl;
        Cleanup(context, commandQueue, program, kernel, images, sampler);
        return -1;
    }

    //Create image sampler
    sampler = clCreateSampler(context, 
                              CL_FALSE, // Non-normalized coordinates
                              CL_ADDRESS_CLAMP_TO_EDGE, //addressing_mode
                              CL_FILTER_LINEAR,  //filter_mode
                              &err);
    if (err != CL_SUCCESS)
    {
        std::cerr << "Error creating sampler object." << std::endl;
        Cleanup(context, commandQueue, program, kernel, images, sampler);
        return -1;
    }

    //Create OpenCL program
    program = CreateProgram(context, device, "../../ImageFilter2D.cl");
    if (program == NULL)
    {
        Cleanup(context, commandQueue, program, kernel, images, sampler);
        return -1;
    }
    // Create OpenCL kernel
    kernel = clCreateKernel(program, "edge_filter", NULL);
    if (kernel == NULL)
    {
        std::cerr << "Failed to create kernel" << std::endl;
        Cleanup(context, commandQueue, program, kernel, images, sampler);
        return -1;
    }

    //Set the kernel arguments
    err  = clSetKernelArg(kernel, 0, sizeof(cl_mem), &images[0]);
    err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &images[1]);
    err |= clSetKernelArg(kernel, 2, sizeof(cl_sampler), &sampler);
    err |= clSetKernelArg(kernel, 3, sizeof(cl_int), &width);
    err |= clSetKernelArg(kernel, 4, sizeof(cl_int), &height);
    if (err != CL_SUCCESS)
    {
        std::cerr << "Error setting kernel arguments. 4" << std::endl;
        Cleanup(context, commandQueue, program, kernel, images, sampler);
        return -1;
    }

    //Set workGroup
    size_t localWorkSize[2] = {4,8};
    size_t globalWorkSize[2] = {width  % localWorkSize[0] == 0 ? width : width  + localWorkSize[0] - width % localWorkSize[0],
                                height % localWorkSize[1] == 0 ? height: height + localWorkSize[1] - height% localWorkSize[1]};//{512,512}
    // for(int i=0;i<2;i++){
    //     std::cout<<"\nGlobalWorkSize = "<<globalWorkSize[i];  //Debug
    // }

    //Queue the kernel up for execution
    /*
    global_work_size参数指定NDRange在每一个维度上有多少个工作项
    local_work_size参数指定NDRange在每个维度上有多少个工作组
    */
    start = std::clock();
    err = clEnqueueNDRangeKernel(commandQueue, kernel, 2, NULL, globalWorkSize, localWorkSize, 0, NULL, NULL);
    if(err != CL_SUCCESS){
        std::cerr<<"Error queueing kernel for execution.";
        Cleanup(context, commandQueue, program, kernel, images, sampler);
        return -1;
    }
    std::cout << "\nExecuted program succesfully." << std::endl;
    duration = ( std::clock() - start ) / (double) CLOCKS_PER_SEC;
    std::cout << "\nOpenCL: time: " << duration<<std::endl;
    //Read output buffer from device
    uchar *buffer = new uchar[width*height];
    size_t origin[3] = { 0, 0, 0 };  //要读取的图像图像原点
    size_t region[3] = { width, height, 1};  //需要读取的图像区域，第三个参数是深度=1
    err = clEnqueueReadImage(commandQueue, images[1], CL_TRUE,
                                origin, region, 0, 0, buffer,
                                0, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        std::cerr << "Error reading result buffer." << std::endl;
        Cleanup(context, commandQueue, program, kernel, images, sampler);
        return -1;
    }
    cv::Mat output(height, width,CV_8U, buffer);
    imwrite("result.png", output);
    delete []buffer;
    Cleanup(context, commandQueue, program, kernel, images, sampler);
    return 0;
}

void LoadImageFromOpenCV(cl_context &context, cv::Mat image, cl_mem &imageObject, size_t height, size_t width, cl_int &err){
    uchar *data = image.data; 
    cl_image_format clImageFormat;
    clImageFormat.image_channel_order = CL_INTENSITY;
    clImageFormat.image_channel_data_type = CL_UNSIGNED_INT8;
    imageObject = clCreateImage2D(context, CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,
        &clImageFormat,width,height,0,data,&err);
    
}
void Cleanup( cl_context context, cl_command_queue commandQueue, cl_program program, 
    cl_kernel kernel, cl_mem images[2], cl_sampler sampler){
        for(int i = 0;i<2;i++){
            if(images[i]!=0){
                clReleaseMemObject(images[i]);
            }
        }
        if(commandQueue != 0){
            clReleaseCommandQueue(commandQueue);
        }
        if(kernel !=0){
            clReleaseKernel(kernel);
        }
        if( program !=0){
            clReleaseProgram(program);
        }
        if(sampler!=0){
            clReleaseSampler(sampler);
        }
        if(context !=0){
            clReleaseContext(context);
        }
}