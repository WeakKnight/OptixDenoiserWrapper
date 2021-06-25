#include "optix_denoiser_wrapper.h"
#define NOMINMAX
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

float* LoadRGBAFloatFromEXR(const char* texPath)
{
    float* imageData; // width * height * RGBA
    int w;
    int h;
    const char* err = nullptr; // or nullptr in C++11

    int ret = LoadEXR(&imageData, &w, &h, texPath, &err);

    if (ret != TINYEXR_SUCCESS)
    {
        if (err)
        {
            fprintf(stderr, "ERR : %s\n", err);
            FreeEXRErrorMessage(err); // release memory of error message.
        }
    }

    return imageData;
}

bool SaveEXR(const float* rgb, int width, int height, const char* outfilename) {

    EXRHeader header;
    InitEXRHeader(&header);

    EXRImage image;
    InitEXRImage(&image);

    image.num_channels = 4;

    std::vector<float> images[4];
    images[0].resize(width * height);
    images[1].resize(width * height);
    images[2].resize(width * height);
    images[3].resize(width * height);

    // Split RGBRGBRGB... into R, G and B layer
    for (int i = 0; i < width * height; i++) {
        images[0][i] = rgb[4 * i + 0];
        images[1][i] = rgb[4 * i + 1];
        images[2][i] = rgb[4 * i + 2];
        images[3][i] = rgb[4 * i + 3];
    }

    float* image_ptr[4];
    image_ptr[0] = &(images[2].at(0)); // B
    image_ptr[1] = &(images[1].at(0)); // G
    image_ptr[2] = &(images[0].at(0)); // R
    image_ptr[3] = &(images[3].at(0)); // A

    image.images = (unsigned char**)image_ptr;
    image.width = width;
    image.height = height;

    header.num_channels = 4;
    header.channels = (EXRChannelInfo*)malloc(sizeof(EXRChannelInfo) * header.num_channels);
    // Must be (A)BGR order, since most of EXR viewers expect this channel order.
    strncpy(header.channels[0].name, "B", 255); header.channels[0].name[strlen("B")] = '\0';
    strncpy(header.channels[1].name, "G", 255); header.channels[1].name[strlen("G")] = '\0';
    strncpy(header.channels[2].name, "R", 255); header.channels[2].name[strlen("R")] = '\0';
    strncpy(header.channels[3].name, "A", 255); header.channels[3].name[strlen("A")] = '\0';

    header.pixel_types = (int*)malloc(sizeof(int) * header.num_channels);
    header.requested_pixel_types = (int*)malloc(sizeof(int) * header.num_channels);
    for (int i = 0; i < header.num_channels; i++) {
        header.pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT; // pixel type of input image
        header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_HALF; // pixel type of output image to be stored in .EXR
    }

    const char* err = NULL; // or nullptr in C++11 or later.
    int ret = SaveEXRImageToFile(&image, &header, outfilename, &err);
    if (ret != TINYEXR_SUCCESS) {
        fprintf(stderr, "Save EXR err: %s\n", err);
        FreeEXRErrorMessage(err); // free's buffer for an error message
        return ret;
    }
    printf("Saved exr file. [ %s ] \n", outfilename);

    free(header.channels);
    free(header.pixel_types);
    free(header.requested_pixel_types);

}

int main()
{
   float* imageData; // width * height * RGBA
   int w;
   int h;
   const char* err = nullptr; // or nullptr in C++11

   int ret = LoadEXR(&imageData, &w, &h, "D:/Github/OptixDenoiserWrapper/PT_46s.exr", &err);

   if (ret != TINYEXR_SUCCESS)
   {
      if (err)
      {
         fprintf(stderr, "ERR : %s\n", err);
         FreeEXRErrorMessage(err); // release memory of error message.
      }
   }
   optix_denoiser_set_image_size(w, h);
   optix_denoiser_set_source_data_pointer(imageData);
   optix_denoiser_init();
   optix_denoiser_exec();
   float* denoisedImageData = optix_denoiser_get_result();
   SaveEXR(denoisedImageData, w, h, "denoised.exr");
   optix_denoiser_free();
   return 0;
}