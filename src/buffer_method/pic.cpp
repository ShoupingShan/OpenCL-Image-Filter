#include <iostream>
#include<fstream>
#include <opencv2/opencv.hpp>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <CL/cl.h>
#include <sys/types.h>
#include<fcntl.h>
#define COMPUTE_KERNEL_FILENAME ("../../CL/oclFilter.cl") //  path to file
using namespace std;
int width = 512,height = 512;       // image resolution, change it to yours one
cl_int err;                         // error output
size_t global;                      // global domain size for our calculation
size_t local;                       // local domain size for our calculation
cl_device_id device_id;             // compute device id
cl_context context;                 // compute context
cl_command_queue commands;          // compute command queue
cl_program program;                 // compute program
cl_kernel kernel;                   // compute kernel
cl_mem input;                       // device memory used for the input array
cl_mem output;                      // device memory used for the output array
int pixelCount = width * height;    // size of 1D array of image data Width x Height
int gpu = 1;                        // GPU flag, set 0 for OpenCL computing at your CPU
double avgTime = 0;                 // statistics info | average time for a rendering one image
int counts = 0;                     // total count of made computings

static int LoadTextFromFile(const char *file_name, char **result_string, size_t *string_len)
{
    char cCurrentPath[1024];
    getcwd(cCurrentPath, sizeof(cCurrentPath));
    int fd;
    unsigned file_len;
    struct stat file_status;
    int ret;
    *string_len = 0;
    fd = open(file_name,O_RDONLY);
    if (fd == -1){
        printf("Error opening file %s\n", file_name);
        return -1;
    }
    ret = fstat(fd, &file_status);
    if (ret){
        printf("Error reading status for file %s\n", file_name);
        return -1;
    }
    file_len = (unsigned)file_status.st_size;
    *result_string = (char*)calloc(file_len + 1, sizeof(char));
    ret = (int)read(fd, *result_string, file_len);
    if (!ret){
        printf("Error reading from file %s\n", file_name);
        return -1;
    }
    close(fd);
    *string_len = file_len;
    return 0;
}

int initMyFilterCl()
{

    cl_platform_id *platform;
    cl_uint numplatform;
    err=clGetPlatformIDs(0,NULL,&numplatform);
    platform = (cl_platform_id*)malloc(sizeof(cl_platform_id)*numplatform);
    err = clGetPlatformIDs(numplatform,platform,NULL);
    err = clGetDeviceIDs(platform[0], gpu ? CL_DEVICE_TYPE_GPU : CL_DEVICE_TYPE_CPU, 1, &device_id, NULL);
    if (err != CL_SUCCESS){
        err = clGetDeviceIDs(platform[1], CL_DEVICE_TYPE_CPU, 1, &device_id, NULL);
        if (err != CL_SUCCESS){
            printf("Error: Failed to create a device group!\n");
            return EXIT_FAILURE;
        }
    }
    cl_char vendor_name[1024] = {0};
    cl_char device_name[1024] = {0};
    size_t returned_size = 0;
    err = clGetDeviceInfo(device_id, CL_DEVICE_VENDOR, sizeof(vendor_name), vendor_name, &returned_size);
    err |= clGetDeviceInfo(device_id, CL_DEVICE_NAME, sizeof(device_name), device_name, &returned_size);
    if (err != CL_SUCCESS)
        return EXIT_FAILURE;
    printf("Connecting to %s %s...\n", vendor_name, device_name);
    context = clCreateContext(NULL, 1, &device_id, NULL, NULL, &err);
    if (!context)
    {
        printf("Error: Failed to create a compute context!\n");
        return EXIT_FAILURE;
    }
    commands = clCreateCommandQueueWithProperties(context, device_id, NULL, &err);
    if (!commands)
    {
        printf("Error: Failed to create a command commands!\n");
        return EXIT_FAILURE;
    }
    char *source = 0;
    size_t length = 0;
    printf("Loading kernel source from file '%s'...\n", "oclFilter.cl");
    err = LoadTextFromFile(COMPUTE_KERNEL_FILENAME, &source, &length);
    if (!source || err)
    {
        printf("Error: Failed to load kernel source!\n");
        return EXIT_FAILURE;
    }
    program = clCreateProgramWithSource(context, 1, (const char **) &source, NULL, &err);
    if (!program)
    {
        printf("Error: Failed to create compute program!\n");
        return EXIT_FAILURE;
    }
    err = clBuildProgram(program, 0, NULL, "-cl-std=CL1.2", NULL, NULL); // you can use "-cl-std=CL2.0" or "-cl-std=CL1.1" but local workgroups must be equal in v1.1
    if (err != CL_SUCCESS)
    {
        size_t len;
        char buffer[pixelCount];

        printf("Error: Failed to build program executable!\n");
        clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
        printf("%s\n", buffer);
        exit(1);
    }
    kernel = clCreateKernel(program, "myFilter", &err);
    if (!kernel || err != CL_SUCCESS)
    {
        printf("Error: Failed to create compute kernel!\n");
        exit(1);
    }

    // Create the input and output arrays in device memory for our calculation
    input = clCreateBuffer(context,  CL_MEM_READ_ONLY,  sizeof(uchar) * pixelCount, NULL, NULL);
    output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(uchar) * pixelCount, NULL, NULL);
    // device memory used for the output array
    if (!input || !output)
    {
        printf("Error: Failed to allocate device memory!\n");
        exit(1);
    }
    // Detect if your gpu supports double precision
    cl_device_fp_config cfg;
    clGetDeviceInfo(device_id, CL_DEVICE_DOUBLE_FP_CONFIG, sizeof(cfg), &cfg, NULL);
    return 0;
}

int computeMyFilterCl(uchar* inputData,uchar* outputData)
{
    err = clEnqueueWriteBuffer(commands, input, CL_TRUE, 0, sizeof(uchar) * pixelCount, inputData, 0, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to write to source array!\n");
        exit(1);
    }
    err = 0;
    err |= clSetKernelArg(kernel, 0, sizeof(cl_mem), &input);
    err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &output);
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to set kernel arguments! %d\n", err);
        exit(1);
    }
    // Get the maximum work group size for executing the kernel on the device
    //
    global = pixelCount;
    err = clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global, NULL, 0, NULL, NULL); // NULL  or &local // better NULL
    if (err)
    {
        printf("Error: Failed to execute kernel!\n");
        return EXIT_FAILURE;
    }

    // Wait for the command commands to get serviced before reading back results
    clFinish(commands);
    err = clEnqueueReadBuffer( commands, output, CL_TRUE, 0, sizeof(uchar) * pixelCount, outputData, 0, NULL, NULL );
    if (err != CL_SUCCESS)
    {
        printf("Error: Failed to read output array! %d\n", err);
        exit(1);
    }
    return 0;
}

void releaseMyFilterCl(){
    clReleaseMemObject(input);
    clReleaseMemObject(output);
    clReleaseProgram(program);
    clReleaseKernel(kernel);
    clReleaseCommandQueue(commands);
    clReleaseContext(context);
}

void show(const std::string& name, const cv::Mat& mat)
{
    if (!mat.empty())
    {   cv::namedWindow(name,CV_WINDOW_AUTOSIZE);
        cv::imshow(name, mat);
    }else{
        std::cout<<"Fail to load image：Input image is empaty！"<<std::endl;
    }
}

uchar * filter_normal(uchar *data, uchar *result,float filter[][3],int kernel_height = 3,int kernel_width = 3,int height = 512, int width = 512){
    
    uchar ** extend_data = new uchar*[height+kernel_height-1];
    for(int i=0;i<height+kernel_height-1;i++)
        extend_data[i]=new uchar[width+kernel_width-1];
    for(int i=0;i<kernel_height+height-1;i++)
        for(int j=0;j<kernel_width+width-1;j++){
            extend_data[i][j]=0;
        }
    for(int i=kernel_height/2,m=0;i<kernel_height/2+height,m<height;i++,m++)
        for(int j=kernel_width/2,n=0;j<kernel_width/2+width,n<width;j++,n++){
            extend_data[i][j] = data[m*width+n];  //fill
        }
    for(int i=kernel_height/2;i<kernel_height/2+height;i++)
        for(int j= kernel_width/2;j<kernel_width/2+width;j++){
            float sum = 0;
            for(int m = -kernel_height / 2;m <= kernel_height/2 ; m++)
                for(int n= -kernel_width / 2;n <= kernel_width/2 ; n++){
                    sum += extend_data[i+m][j+n] * filter[m+kernel_height/2][n+kernel_width/2];
                }
            //if(i>height/2){cout<<" sum = "<<sum;}
            result[(i-kernel_height/2)*width + j-kernel_width/2] = uchar(sum);
        }
    for(int i=0;i<height+kernel_height-1;i++)
        delete []extend_data[i];
    delete []extend_data;  
    return result;
}

void write(std::ofstream &os, uchar * result, long length){
    for(long i=0;i<length;i++)
        os<<int(result[i])<<" ";
    os.close();
}
int main()
{
    using namespace cv;
    ofstream fout1,fout2,fout3;
    fout1.open("./result/OpenCL.txt",ios::out);
    fout2.open("./result/Normal.txt",ios::out);
    fout3.open("./result/OpenCV.txt",ios::out);
    Mat rgbimage = imread("../../data/Lena.jpg");
    if(!rgbimage.data)//judge weather the image load successfully
    {
        cout << "Fail to load image!" << endl;
        return false;
    }
    Mat grayimage;
    cvtColor(rgbimage, grayimage, CV_BGR2GRAY);     //convert RGB to gray
    initMyFilterCl();
    float  filter[3][3]={
        {-1, 0, -1},
        { 0, 4,  0},
        {-1, 0, -1}
    };
    uchar *result=new uchar [pixelCount];
	for(int i=0;i<pixelCount;i++)
		result[i]=0;
    std::clock_t start1,start2,start3;
    double duration1;
    uchar outputData[pixelCount]={0};
    uchar* data;
    start1 = std::clock();
    data = grayimage.data;
    computeMyFilterCl(data,outputData);
    duration1 = ( std::clock() - start1 ) / (double) CLOCKS_PER_SEC;
    avgTime+=duration1;
    counts++;
    std::cout << "\nOpenCL: time: " << duration1 << " avgTime: " << avgTime/counts <<'\n';
    write(fout1,outputData,pixelCount);
    releaseMyFilterCl();
    counts = 0;
    avgTime = 0;
    start2 = std::clock();
    filter_normal(data,&(*result),filter,3,3,height,width);  //Use array reference to transport params
    duration1 = ( std::clock() - start2 ) / (double) CLOCKS_PER_SEC;
    avgTime+=duration1;
    counts++;
    std::cout << "\nNormal: time: " << duration1 << " avgTime: " << avgTime/counts <<'\n';
    write(fout2,result,pixelCount);
    counts = 0;
    avgTime = 0;
    start3 = std::clock();
    Mat kern = (Mat_<float>(3,3)<< -1,0,-1,
                                    0,4, 0,
                                   -1,0,-1);
    Mat dstImage;
    filter2D(grayimage,dstImage,grayimage.depth(),kern);
    duration1 = ( std::clock() - start3 ) / (double) CLOCKS_PER_SEC;
    avgTime+=duration1;
    counts++;
    std::cout << "\nOpenCV: time: " << duration1 << " avgTime: " << avgTime/counts <<'\n';
    write(fout3,dstImage.data,pixelCount);
    show("Frame before", grayimage);
    Mat frameOut3(grayimage.rows, grayimage.cols, CV_8U, outputData);
    show("Opencl frame after", frameOut3);
    Mat frameOut4(grayimage.rows, grayimage.cols, CV_8U, result);
    show("Normal frame after", frameOut4);
    show("OpenCV frame after",dstImage);
    waitKey(0);
    delete []result;
    return 0;
}
