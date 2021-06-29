using System.Collections;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine;

public static class OptixDenoiserWrapper
{    
    [DllImport("OptixDenoiserWrapper")]
    private static extern void optix_denoiser_set_image_size(uint width, uint height);
    [DllImport("OptixDenoiserWrapper")]
    private static extern void optix_denoiser_set_source_data_pointer(System.IntPtr ptr);
    [DllImport("OptixDenoiserWrapper")]
    private static extern void optix_denoiser_set_normal_data_pointer(System.IntPtr ptr);
    [DllImport("OptixDenoiserWrapper")]
    private static extern void optix_denoiser_set_albedo_data_pointer(System.IntPtr ptr);
    [DllImport("OptixDenoiserWrapper")]
    private static extern void optix_denoiser_init();
    [DllImport("OptixDenoiserWrapper")]
    private static extern void optix_denoiser_update();
    [DllImport("OptixDenoiserWrapper")]
    private static extern void optix_denoiser_exec();
    [DllImport("OptixDenoiserWrapper")]
    private static extern System.IntPtr optix_denoiser_get_result();
    [DllImport("OptixDenoiserWrapper")]
    private static extern void optix_denoiser_free();
}