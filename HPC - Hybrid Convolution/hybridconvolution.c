// Convolution using Hybrid (MPI - Open MP)
// github : - @aditya1453
//          - @widyameiriska
//
//  convolution.c
//
//
// Serial Code Created by Josep Lluis Lerida on 11/03/15.
// 
// This program calculates the convolution for PPM images.
// The program accepts an PPM image file, a text definition of the kernel matrix and the PPM file for storing the convolution results.
// The program allows to define image partitions for processing large images (>500MB)
// The 2D image is represented by 1D vector for chanel R, G and B. The convolution is applied to each chanel separately.

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <mpi.h>
#include <omp.h>
#include <unistd.h>

// Structure to store image.
struct imagenppm{
    int altura;
    int ancho;
    char *comentario;
    int maxcolor;
    int P;
    int *R;
    int *G;
    int *B;
};
typedef struct imagenppm* ImagenData;

// Structure to store the kernel.
struct structkernel{
    int kernelX;
    int kernelY;
    float *vkern;
};
typedef struct structkernel* kernelData;

//Functions Definition
ImagenData initimage(char* nombre, FILE **fp, int partitions, int halo);
ImagenData duplicateImageData(ImagenData src, int partitions, int halo);

int readImage(ImagenData Img, FILE **fp, int dim, int halosize, long int *position);
int duplicateImageChunk(ImagenData src, ImagenData dst, int dim);
int initfilestore(ImagenData img, FILE **fp, char* nombre, long *position);
int savingChunk(ImagenData img, FILE **fp, int dim, int offset);
int convolve2D(int* inbuf, int* outbuf, int sizeX, int sizeY, float* kernel, int ksizeX, int ksizeY);
// void freeImagestructure(ImagenData *src);

//Open Image file and image struct initialization
ImagenData initimage(char* nombre, FILE **fp,int partitions, int halo){
    char c;
    char comentario[300];
    int i=0,chunk=0;
    ImagenData img=NULL;
    
    /*Opening ppm*/

    if ((*fp=fopen(nombre,"r"))==NULL){
        perror("Error: ");
    }
    else{
        //Memory allocation
        img=(ImagenData) malloc(sizeof(struct imagenppm));
        //Reading the first line: Magical Number "P3"
        fscanf(*fp,"%c%d ",&c,&(img->P));
        //Reading the image comment
        while((c=fgetc(*fp))!= '\n'){comentario[i]=c;i++;}
        comentario[i]='\0';
        //Allocating information for the image comment
        img->comentario = calloc(strlen(comentario),sizeof(char));
        strcpy(img->comentario,comentario);
        //Reading image dimensions and color resolution
        fscanf(*fp,"%d %d %d",&img->ancho,&img->altura,&img->maxcolor);
        chunk = img->ancho*img->altura / partitions;
        //We need to read an extra row.
        chunk = chunk + img->ancho * halo;
        if ((img->R=calloc(chunk,sizeof(int))) == NULL) {return NULL;}
        if ((img->G=calloc(chunk,sizeof(int))) == NULL) {return NULL;}
        if ((img->B=calloc(chunk,sizeof(int))) == NULL) {return NULL;}
    }
    return img;
}

//Duplicate the Image struct for the resulting image
ImagenData duplicateImageData(ImagenData src, int partitions, int halo){
    char c;
    char comentario[300];
    unsigned int imageX, imageY;
    int i=0, chunk=0;
    //Struct memory allocation
    ImagenData dst=(ImagenData) malloc(sizeof(struct imagenppm));

    //Copying the magic number
    dst->P=src->P;
    //Copying the string comment
    dst->comentario = calloc(strlen(src->comentario),sizeof(char));
    strcpy(dst->comentario,src->comentario);
    //Copying image dimensions and color resolution
    dst->ancho=src->ancho;
    dst->altura=src->altura;
    dst->maxcolor=src->maxcolor;
    chunk = dst->ancho*dst->altura / partitions;
    //We need to read an extra row.
    chunk = chunk + src->ancho * halo;
    if ((dst->R=calloc(chunk,sizeof(int))) == NULL) {return NULL;}
    if ((dst->G=calloc(chunk,sizeof(int))) == NULL) {return NULL;}
    if ((dst->B=calloc(chunk,sizeof(int))) == NULL) {return NULL;}
    return dst;
}

//Read the corresponding chunk from the source Image
int readImage(ImagenData img, FILE **fp, int dim, int halosize, long *position){
    int i=0, k=0,haloposition=0;
    if (fseek(*fp,*position,SEEK_SET))
        perror("Error: ");
    haloposition = dim-(img->ancho*halosize*2);
    for(i=0;i<dim;i++) {
        // When start reading the halo store the position in the image file
        if (halosize != 0 && i == haloposition) *position=ftell(*fp);
        fscanf(*fp,"%d %d %d ",&img->R[i],&img->G[i],&img->B[i]);
        k++;
    }
//    printf ("Readed = %d pixels, posicio=%lu\n",k,*position);
    return 0;
}

//Duplication of the  just readed source chunk to the destiny image struct chunk
int duplicateImageChunk(ImagenData src, ImagenData dst, int dim){
    int i=0;
    
    for(i=0;i<dim;i++){
        dst->R[i] = src->R[i];
        dst->G[i] = src->G[i];
        dst->B[i] = src->B[i];
    }
//    printf ("Duplicated = %d pixels\n",i);
    return 0;
}

// Open kernel file and reading kernel matrix. The kernel matrix 2D is stored in 1D format.
kernelData leerKernel(char* nombre){
    FILE *fp;
    int i=0;
    kernelData kern=NULL;
    
    /*Opening the kernel file*/
    fp=fopen(nombre,"r");
    if(!fp){
        perror("Error: ");
    }
    else{
        //Memory allocation
        kern=(kernelData) malloc(sizeof(struct structkernel));
        
        //Reading kernel matrix dimensions
        fscanf(fp,"%d,%d,", &kern->kernelX, &kern->kernelY);
        kern->vkern = (float *)malloc(kern->kernelX*kern->kernelY*sizeof(float));
        
        // Reading kernel matrix values
        for (i=0;i<(kern->kernelX*kern->kernelY)-1;i++){
            fscanf(fp,"%f,",&kern->vkern[i]);
        }
        fscanf(fp,"%f",&kern->vkern[i]);
        fclose(fp);
    }
    return kern;
}

// Open the image file with the convolution results
int initfilestore(ImagenData img, FILE **fp, char* nombre, long *position){
    /*Se crea el fichero con la imagen resultante*/
    
    if ( (*fp=fopen(nombre,"w")) == NULL ){
        perror("Error: ");
        return -1;
    }
    /*Writing Image Header*/
    fprintf(*fp,"P%d\n%s\n%d %d\n%d\n",img->P,img->comentario,img->ancho,img->altura,img->maxcolor);
    *position = ftell(*fp);
    return 0;
}

// Writing the image partition to the resulting file. dim is the exact size to write. offset is the displacement for avoid halos.
int savingChunk(ImagenData img, FILE **fp, int dim, int offset){
    int i,k=0;
    //Writing image partition
    for(i=offset;i<dim+offset;i++){
        // Debug
        // if (i<100){
        //     printf("%d", i); if (i+1%20==0) printf("\n");
        // }
        fprintf(*fp,"%d %d %d ",img->R[i],img->G[i],img->B[i]);
    //    if ((i+1)%6==0) fprintf(*fp,"\n");
        k++;
    }
    
//    printf ("Writed = %d pixels, dim=%d, offset=%d\n",k,dim, offset);
    return 0;
}

// This function free the space allocated for the image structure.
// void freeImagestructure(ImagenData *src){
    
//     free((*src)->comentario);
//     free((*src)->R);
//     free((*src)->G);
//     free((*src)->B);
//     free(*src);
// }

///////////////////////////////////////////////////////////////////////////////
// 2D convolution
// 2D data are usually stored in computer memory as contiguous 1D array.
// So, we are using 1D array for 2D data.
// 2D convolution assumes the kernel is center originated, which means, if
// kernel size 3 then, k[-1], k[0], k[1]. The middle of index is always 0.
// The following programming logics are somewhat complicated because of using
// pointer indexing in order to minimize the number of multiplications.
//
//
// signed integer (32bit) version:
///////////////////////////////////////////////////////////////////////////////
int convolve2D(int* in, int* out, int dataSizeX, int dataSizeY,
               float* kernel, int kernelSizeX, int kernelSizeY)
{
    int i, j, m, n;
    int *inPtr, *inPtr2, *outPtr;
    float *kPtr;
    int kCenterX, kCenterY;
    int rowMin, rowMax;                             // to check boundary of input array
    int colMin, colMax;                             //
    float sum;                                      // temp accumulation buffer
    
    // check validity of params
    if(!in || !out || !kernel) return -1;
    if(dataSizeX <= 0 || kernelSizeX <= 0) return -1;
    
    // find center position of kernel (half of kernel size)
    kCenterX = (int)kernelSizeX / 2;
    kCenterY = (int)kernelSizeY / 2;
    
    // init working  pointers
    inPtr = inPtr2 = &in[dataSizeX * kCenterY + kCenterX];  // note that  it is shifted (kCenterX, kCenterY),
    outPtr = out;
    kPtr = kernel;
    // start convolution
    // #pragma omp parallel for schedule(dynamic,100) private (j, m, n, rowMax, rowMin, colMax, colMin)
    for(i= 0; i < dataSizeY; ++i)                   // number of rows
    {
        // compute the range of convolution, the current row of kernel should be between these
        rowMax = i + kCenterY;
        rowMin = i - dataSizeY + kCenterY;
        
        for(j = 0; j < dataSizeX; ++j)              // number of columns
        {
            // compute the range of convolution, the current column of kernel should be between these
            colMax = j + kCenterX;
            colMin = j - dataSizeX + kCenterX;
            
            sum = 0;                                // set to 0 before accumulate
            
            // flip the kernel and traverse all the kernel values
            // multiply each kernel value with underlying input data
            for(m = 0; m < kernelSizeY; ++m)        // kernel rows
            {
                // check if the index is out of bound of input array
                if(m <= rowMax && m > rowMin)
                {
                    for(n = 0; n < kernelSizeX; ++n)
                    {
                        // check the boundary of array
                        if(n <= colMax && n > colMin)
                            sum += *(inPtr - n) * *kPtr;
                        
                        ++kPtr;                     // next kernel
                    }
                }
                else
                    kPtr += kernelSizeX;            // out of bound, move to next row of kernel
                
                inPtr -= dataSizeX;                 // move input data 1 raw up
            }
            
            // convert integer number
            if(sum >= 0) *outPtr = (int)(sum + 0.5f);
//            else *outPtr = (int)(sum - 0.5f)*(-1);
            // For using with image editors like GIMP or others...
            else *outPtr = (int)(sum - 0.5f);
            // For using with a text editor that read ppm images like libreoffice or others...
//            else *outPtr = 0;
            
            kPtr = kernel;                          // reset kernel to (0,0)
            inPtr = ++inPtr2;                       // next input
            ++outPtr;                               // next output
        }
    }
    
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// MAIN FUNCTION
//////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    // Variable declaration
    int rank, size;
    MPI_Status status;
    
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int i=0,j=0,k=0;
    
//    int headstored=0, imagestored=0, stored;
    if(argc != 5){ // Master & slaves check the argument input
        if (rank==0){
            printf("Usage: %s <image-file> <kernel-file> <result-file> <partitions>\n", argv[0]);
            printf("\n\nError, Missing parameters:\n");
            printf("format: ./serialconvolution image_file kernel_file result_file\n");
            printf("- image_file : source image path (*.ppm)\n");
            printf("- kernel_file: kernel path (text file with 1D kernel matrix)\n");
            printf("- result_file: result image path (*.ppm)\n");
            printf("- partitions : Image partitions\n\n");
        }
        return -1;
    }
    
    //////////////////////////////////////////////////////////////////////////////////////////////////
    // READING IMAGE HEADERS, KERNEL Matrix, DUPLICATE IMAGE DATA, OPEN RESULTING IMAGE FILE
    //////////////////////////////////////////////////////////////////////////////////////////////////
    /*
        ==== Job Distribution ====
        Master      : - Reading image header
                      - Kernel Matrix
                      - Duplicate Image data
                      - Open Resulting Image File
        all slave   : - kernel Matrix
    */

    int imagesize, partitions, partsize, chunksize, halo, halosize;
    long position=0;
    double start, tstart=0, tend=0, tread=0, tcopy=0, tconv=0, tstore=0, treadk=0;
    struct timeval tim;
    FILE *fpsrc=NULL,*fpdst=NULL;
    ImagenData source=NULL, output=NULL;
    kernelData kern=NULL;

    if (rank==0){ // Master
        // Store number of partitions
        partitions = atoi(argv[4]);

        ////////////////////////////////////////
        // Reading kernel matrix
        ////////////////////////////////////////
        
        gettimeofday(&tim, NULL); 
        start = tim.tv_sec+(tim.tv_usec/1000000.0);
        tstart = start;
        
        if ( (kern = leerKernel(argv[2]))==NULL) {
            //        free(source);
            //        free(output);
            return -1;
        }
        
        //The matrix kernel define the halo size to use with the image. The halo is zero when the image is not partitioned.
        if (partitions==1) halo=0;
        else halo = (kern->kernelY/2)*2; 
        gettimeofday(&tim, NULL);
        treadk = treadk + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);
        
        ///////////////////////////////////////////////////////////////////////////////////////////////
        //Reading Image Header. Image properties: Magical number, comment, size and color resolution.
        ///////////////////////////////////////////////////////////////////////////////////////////////

        gettimeofday(&tim, NULL);
        start = tim.tv_sec+(tim.tv_usec/1000000.0);
        //Memory allocation based on number of partitions and halo size.
        if ( (source = initimage(argv[1], &fpsrc, partitions, halo)) == NULL) {
            return -1;
        }
        gettimeofday(&tim, NULL);
        tread = tread + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);

        //Duplicate the image struct.
        gettimeofday(&tim, NULL);
        start = tim.tv_sec+(tim.tv_usec/1000000.0);
        if ( (output = duplicateImageData(source, partitions, halo)) == NULL) {
            return -1;
        }
        gettimeofday(&tim, NULL);
        tcopy = tcopy + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);

        ///////////////////////////////////////////////////////////////////////////
        //Initialize Image Storing file. Open the file and store the image header.
        ///////////////////////////////////////////////////////////////////////////
        
        gettimeofday(&tim, NULL);
        start = tim.tv_sec+(tim.tv_usec/1000000.0);
        
        if (initfilestore(output, &fpdst, argv[3], &position)!=0) {
            perror("Error: ");
            //        free(source);
            //        free(output);
            return -1;
        }
        
        gettimeofday(&tim, NULL);
        tstore = tstore + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);

    } else { // Slaves also read the kernel 
        
        if ( (kern = leerKernel(argv[2]))==NULL) {
            //        free(source);
            //        free(output);
            return -1;
        }
        
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // CHUNK READING
    //////////////////////////////////////////////////////////////////////////////////////////////////
    /*
        ==== Job Distribution ====
        - Master       : - read chunk image
                         - distributing work by static way
                         - do convolution
                         - receive result from slaves
                         
        - Slaves       : - receive job from master
                         - do convolution
                         - send result
    */

    int rem_job, job, pixel, width, height, dest; 
    int msg[5]; // {width, height, halosize, pixel, partition}
    int *ptrR = NULL, *ptrG = NULL, *ptrB = NULL;

    // Alocating Memory 
    ImagenData partImgIn =NULL;
    ImagenData partImgOut=NULL; 

    if (rank==0){ 
        
        int c=0, offset=0;
        imagesize = source->altura*source->ancho;
        partsize  = (source->altura*source->ancho)/partitions;
        // printf("%s ocupa %dx%d=%d pixels. Partitions=%d, halo=%d, partsize=%d pixels\n", argv[1], source->altura, source->ancho, imagesize, partitions, halo, partsize);
        while (c < partitions) {
            // printf("Master : partition %d\n", c+1);
            ////////////////////////////////////////////////////////////////////////////////
            // Reading Next chunk.
            ////////////////////////////////////////////////////////////////////////////////
        
            gettimeofday(&tim, NULL);
            start = tim.tv_sec+(tim.tv_usec/1000000.0);
            if (c==0) {
                halosize  = halo/2;
                chunksize = partsize + (source->ancho*halosize);
                offset    = 0;
            }
            else if(c<partitions-1) {
                halosize  = halo;
                chunksize = partsize + (source->ancho*halosize);
                offset    = (source->ancho*halo/2);
            }
            else {
                halosize  = halo/2;
                chunksize = partsize + (source->ancho*halosize);
                offset    = (source->ancho*halo/2);
            }

            //DEBUG
            // printf("\nRound = %d, position = %ld, partsize= %d, chunksize=%d pixels\n", c, position, partsize, chunksize);
            
            if (readImage(source, &fpsrc, chunksize, halo/2, &position)) {
                return -1;
            }

            gettimeofday(&tim, NULL);
            tread = tread + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);
            
            //Duplicate the image chunk
            gettimeofday(&tim, NULL);
            start = tim.tv_sec+(tim.tv_usec/1000000.0);
            if ( duplicateImageChunk(source, output, chunksize) ) {
                return -1;
            }
            gettimeofday(&tim, NULL);
            tcopy = tcopy + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);

            // DEBUG
            // for (i=0;i<chunksize;i++)
            //    if (source->R[i]!=output->R[i] || source->G[i]!=output->G[i] || source->B[i]!=output->B[i]) printf("At position i=%d %d!=%d,%d!=%d,%d!=%d\n",i,source->R[i],output->R[i], source->G[i],output->G[i],source->B[i],output->B[i]);
            

            ///////////////////////////////////////////////////////////////////////////
            // Distributing the Chunk Image to Slaves
            ///////////////////////////////////////////////////////////////////////////
            
            gettimeofday(&tim, NULL);
            start = tim.tv_sec+(tim.tv_usec/1000000.0);
            
            // printf("Image Height : %d\n", source->altura);
            // printf("Image Width  : %d\n", source->ancho);
            // // Creating Job Distribution
            // printf("Master : Preparing Distributing Job\n");
            job       = source->altura/(size*partitions); // number of chunk image row 
            rem_job   = (source->altura/partitions)%size; // number of remaining row 
            pixel     = job * source->ancho; // total element

            msg[0] = source->ancho; 
            msg[1] = source->altura;
            msg[2] = halosize;
            msg[3] = pixel;
            msg[4] = partitions;

            // Broadcast number of pixel to other slaves
            MPI_Bcast(msg, 5, MPI_INT, 0, MPI_COMM_WORLD);
            
            ptrR = source->R + pixel + rem_job * source->ancho;
            ptrG = source->G + pixel + rem_job * source->ancho; 
            ptrB = source->B + pixel + rem_job * source->ancho; 

            // printf("Master : Sending Chunk Image ... %x\n", fpdst);
            // Send chunk of Image to slaves
            // printf("Master : Sending Job\n");
            for (dest=1;dest<size;dest++){
                
                // Sending chunk of Image
                MPI_Send(ptrR, pixel, MPI_INT, dest, 1, MPI_COMM_WORLD);
                MPI_Send(ptrG, pixel, MPI_INT, dest, 2, MPI_COMM_WORLD);
                MPI_Send(ptrB, pixel, MPI_INT, dest, 3, MPI_COMM_WORLD);
                
                // updating the pointer
                ptrR += pixel;
                ptrG += pixel;
                ptrB += pixel; 
            }

            //////////////////////////////////////////////////////////////////////////////////////////////////
            // CHUNK CONVOLUTION - MASTER
            //////////////////////////////////////////////////////////////////////////////////////////////////
            /*
                Master get an extra job (remaining job)
            */
            // printf("Master : Convolution\n");
            
            // Open Branch Using OpenMP
            
            #pragma omp parallel num_threads(3)
            {
                #pragma omp sections nowait
                {
                    #pragma omp section 
                    convolve2D(source->R, output->R, source->ancho, (source->altura/(size*partitions))+ rem_job +halosize, kern->vkern, kern->kernelX, kern->kernelY);
                    
                    #pragma omp section 
                    convolve2D(source->G, output->G, source->ancho, (source->altura/(size*partitions))+ rem_job +halosize, kern->vkern, kern->kernelX, kern->kernelY);
                    
                    #pragma omp section 
                    convolve2D(source->B, output->B, source->ancho, (source->altura/(size*partitions))+ rem_job +halosize, kern->vkern, kern->kernelX, kern->kernelY);               
                }
            }   
            
            // convolve2D(source->R, output->R, source->ancho, (source->altura/(size*partitions))+ rem_job +halosize, kern->vkern, kern->kernelX, kern->kernelY);
            // convolve2D(source->G, output->G, source->ancho, (source->altura/(size*partitions))+ rem_job +halosize, kern->vkern, kern->kernelX, kern->kernelY);
            // convolve2D(source->B, output->B, source->ancho, (source->altura/(size*partitions))+ rem_job +halosize, kern->vkern, kern->kernelX, kern->kernelY);
      
            
            // printf("Master : Convolution Done\n");
            // // Reset Pointer
            // ptrR = output->R + pixel + rem_job * source->ancho;
            // ptrG = output->G + pixel + rem_job * source->ancho; 
            // ptrB = output->B + pixel + rem_job * source->ancho; 
            // printf("ptrR = %x   | ptrG = %x   | ptrB = %x \n", output->R, output->G, output->B );
            
            //////////////////////////////////////////////////////////////////////////////
            // Receive result from slaves
            //////////////////////////////////////////////////////////////////////////////
            // printf("Master : Receiving result\n");
            output->R += rem_job*source->ancho;
            output->G += rem_job*source->ancho;
            output->B += rem_job*source->ancho;

            for (i=1;i<size;i++){     
                MPI_Recv(output->R + i*pixel, pixel, MPI_INT, i, 1, MPI_COMM_WORLD, &status);
                MPI_Recv(output->G + i*pixel, pixel, MPI_INT, i, 2, MPI_COMM_WORLD, &status);
                MPI_Recv(output->B + i*pixel, pixel, MPI_INT, i, 3, MPI_COMM_WORLD, &status);
            }
            
            gettimeofday(&tim, NULL);
            tconv = tconv + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);
            //////////////////////////////////////////////////////////////////////////////////////////////////
            // CHUNK SAVING
            //////////////////////////////////////////////////////////////////////////////////////////////////

            // printf("Partsize : %d\n ", partsize);
            // printf("Chunksize : %d\n ", chunksize);
            // printf("Partsize : %d\n ", pixel);
            // printf("Offset : %d\n ", offset);
            // printf("Master : Chunk saving\n");
            // Storing resulting image partition.
            gettimeofday(&tim, NULL);
            start = tim.tv_sec+(tim.tv_usec/1000000.0);
            if (savingChunk(output, &fpdst, partsize, offset)) {
                perror("Error: ");
                //        free(source);
                //        free(output);
                return -1;
            }
            gettimeofday(&tim, NULL);
            tstore = tstore + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);
            //Next partition
            c++;
        }

        fclose(fpsrc);
        fclose(fpdst);

        gettimeofday(&tim, NULL);
        tend = tim.tv_sec+(tim.tv_usec/1000000.0);
        printf("\nMaster:\n");
        printf("Imatge: %s\n", argv[1]);
        printf("ISizeX : %d\n", source->ancho);
        printf("ISizeY : %d\n", source->altura);
        printf("kSizeX : %d\n", kern->kernelX);
        printf("kSizeY : %d\n", kern->kernelY);
        printf("%.6lf seconds elapsed for Reading image file.\n", tread);
        printf("%.6lf seconds elapsed for copying image structure.\n", tcopy);
        printf("%.6lf seconds elapsed for Reading kernel matrix.\n", treadk);
        printf("%.6lf seconds elapsed for make the convolution.\n", tconv);
        printf("%.6lf seconds elapsed for writing the resulting image.\n", tstore);
        printf("%.6lf seconds elapsed\n", tend-tstart);
        
        // freeImagestructure(&source);
        // freeImagestructure(&output);

        // free momory
        free(source->R);    free(source->G);    free(source->B);
        free(output->R);    free(output->G);    free(output->B);
    
    } else{ // Slaves

        // Receive message broadcast from Master
        
        MPI_Bcast(msg, 5, MPI_INT, 0, MPI_COMM_WORLD);
        width      = msg[0];
        height     = msg[1];
        halosize   = msg[2];
        pixel      = msg[3];
        partitions = msg[4];
        
        // DEBUG
        // printf("Width           : %d\n", width);
        // printf("Height          : %d\n", height);
        // printf("Halosize        : %d\n", halosize);
        // printf("Number of pixel : %d\n", pixel);
        
        // printf("Slave(%d) : Alocating Memory\n", rank);
        // Alocating Memory - convolution input 
        partImgIn =(ImagenData) malloc(sizeof(struct imagenppm));
        partImgIn->R=calloc(pixel,sizeof(int)); 
        partImgIn->G=calloc(pixel,sizeof(int)); 
        partImgIn->B=calloc(pixel,sizeof(int)); 

        // Alocating Memory - convolution output
        partImgOut =(ImagenData) malloc(sizeof(struct imagenppm));
        partImgOut->R=calloc(pixel,sizeof(int)); 
        partImgOut->G=calloc(pixel,sizeof(int)); 
        partImgOut->B=calloc(pixel,sizeof(int)); 
        
        // printf("Slave(%d) : Receiving Chunk Image\n", rank);
        // Receiving Chunk Image From Master
        MPI_Recv(partImgIn->R, pixel, MPI_INT, 0, 1, MPI_COMM_WORLD, &status);
        MPI_Recv(partImgIn->G, pixel, MPI_INT, 0, 2, MPI_COMM_WORLD, &status);
        MPI_Recv(partImgIn->B, pixel, MPI_INT, 0, 3, MPI_COMM_WORLD, &status);

        // DEBUG : print receiving image                
        // for (j = 0; j<50;j++){
        //     printf("proc(%d) : partImgIn[%d] = %d \n",rank, j, partImgIn->R[j]);
        //     if ((j+1)%20==0) printf("\n");
        // }

        //////////////////////////////////////////////////////////////////////////////////////////////////
        // CHUNK CONVOLUTION - SLAVE
        //////////////////////////////////////////////////////////////////////////////////////////////////
        // printf("Slave(%d) : Convolution\n", rank);
        // gettimeofday(&tim, NULL);
        // start = tim.tv_sec+(tim.tv_usec/1000000.0);
        
        // Open Branch Using OpenMP

        #pragma omp parallel num_threads(3)
        {
            #pragma omp sections nowait
            {
                #pragma omp section 
                convolve2D(partImgIn->R, partImgOut->R, width, (height/(size*partitions))+halosize, kern->vkern, kern->kernelX, kern->kernelY);
                                    
                #pragma omp section 
                convolve2D(partImgIn->G, partImgOut->G, width, (height/(size*partitions))+halosize, kern->vkern, kern->kernelX, kern->kernelY);
                
                #pragma omp section 
                convolve2D(partImgIn->B, partImgOut->B, width, (height/(size*partitions))+halosize, kern->vkern, kern->kernelX, kern->kernelY);             
            }
        }   

        // convolve2D(partImgIn->R, partImgOut->R, width, (height/(size*partitions))+halosize, kern->vkern, kern->kernelX, kern->kernelY);
        // convolve2D(partImgIn->G, partImgOut->G, width, (height/(size*partitions))+halosize, kern->vkern, kern->kernelX, kern->kernelY);
        // convolve2D(partImgIn->B, partImgOut->B, width, (height/(size*partitions))+halosize, kern->vkern, kern->kernelX, kern->kernelY);
        
        // gettimeofday(&tim, NULL);
        // tconv = tconv + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);
        // printf("Slave(%d) : Convolution Done\n", rank);
        // DEBUG : print result image                
        // for (j = 0; j<50;j++){
        //     printf("proc(%d) : partImgOut[%d] = %d \n",rank, j, partImgOut->R[j]);
        //     if ((j+1)%20==0) printf("\n");
        // }
        // printf("Slave(%d) : Sending Result\n", rank);
        // // Sending chunk of Image
        MPI_Send(partImgOut->R, pixel, MPI_INT, 0, 1, MPI_COMM_WORLD);
        MPI_Send(partImgOut->G, pixel, MPI_INT, 0, 2, MPI_COMM_WORLD);
        MPI_Send(partImgOut->B, pixel, MPI_INT, 0, 3, MPI_COMM_WORLD);

        // freeImagestructure(&partImgIn);
        // freeImagestructure(&partImgOut);
        
        // Free memory
        free(partImgOut->R);    free(partImgOut->G);    free(partImgOut->B);
        free(partImgIn->R);     free(partImgIn->G);     free(partImgIn->B);

        // printf("slave (%d) : %.6lf seconds elapsed for make the convolution.\n", rank, tconv);
    }
    
    MPI_Finalize();
    return 0;
}
