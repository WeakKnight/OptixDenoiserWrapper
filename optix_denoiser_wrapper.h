#pragma once
#include <stdio.h>
#include <sstream>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <iomanip>
#include <vector>
#include <cmath>
#include <stdint.h>

#define OPTIX_DENOISER_WRAPPER_API __declspec(dllexport) 

extern "C" 
{
    OPTIX_DENOISER_WRAPPER_API void     optix_denoiser_set_image_size(uint32_t width, uint32_t height);
    OPTIX_DENOISER_WRAPPER_API void     optix_denoiser_set_source_data_pointer(float* ptr);
    OPTIX_DENOISER_WRAPPER_API void     optix_denoiser_set_normal_data_pointer(float* ptr);
    OPTIX_DENOISER_WRAPPER_API void     optix_denoiser_set_albedo_data_pointer(float* ptr);
    OPTIX_DENOISER_WRAPPER_API void     optix_denoiser_init();
    OPTIX_DENOISER_WRAPPER_API void     optix_denoiser_update();
    OPTIX_DENOISER_WRAPPER_API void     optix_denoiser_exec();
    OPTIX_DENOISER_WRAPPER_API float*   optix_denoiser_get_result();
    OPTIX_DENOISER_WRAPPER_API void     optix_denoiser_free();
    OPTIX_DENOISER_WRAPPER_API float*   optix_denoiser_test();
    //Create a callback delegate
    typedef void(*FuncCallBack)(const char* message, int color, int size);
    static FuncCallBack callbackInstance = nullptr;
    OPTIX_DENOISER_WRAPPER_API void RegisterDebugCallback(FuncCallBack cb);
}

