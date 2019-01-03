__kernel void myFilter(
__global uchar* data, __global uchar* data2){
    float kernelMatrix[] = {
        -1, 0, -1,
         0, 4, 0,
        -1, 0, -1};
    uint kernelWidth = 3;
    uint kernelHeight = 3,width = 512,height = 512;
    uint N = get_global_id(0);
    uint x = N % width;
    uint y = N / width;
    float rSum = 0, kSum = 0;
    for (uint i = 0; i < kernelWidth; i++)
    {
        for (uint j = 0; j < kernelHeight; j++)
        {
            int pixelPosX = x + (i - (kernelWidth / 2));
            int pixelPosY = y + (j - (kernelHeight / 2));
            if (pixelPosX >= width)
                pixelPosX = (width<<1)-1-pixelPosX;  //Mirror symmetry
            if (pixelPosY >= height)
                pixelPosY = (height<<1)-1-pixelPosY; //Mirror symmetry
            if (pixelPosX < 0) 
                pixelPosX = -pixelPosX;
            if (pixelPosY < 0) 
                pixelPosY = -pixelPosY;
            uchar r = data[pixelPosX + pixelPosY * width];
            float kernelVal = kernelMatrix[i + j * kernelWidth];
            rSum += r * kernelVal;
            //printf("kernelVal = %f",kernelVal);  //Debug
        }
    }
    data2[x+y*width] = (uchar)rSum;
    barrier(CLK_LOCAL_MEM_FENCE | CLK_GLOBAL_MEM_FENCE);  //Keep variables in sync
}