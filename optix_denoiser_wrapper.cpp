#include "optix_denoiser_wrapper.h"

#define NOMINMAX
#include <cuda_runtime.h>
#include <optix.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <optix_denoiser_tiling.h>

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

//Color Enum
enum class Color { Red, Green, Blue, Black, White, Yellow, Orange };

class  Debug
{
public:
    static void Log(const char* message, Color color = Color::Black);
    static void Log(const std::string message, Color color = Color::Black);
    static void Log(const int message, Color color = Color::Black);
    static void Log(const char message, Color color = Color::Black);
    static void Log(const float message, Color color = Color::Black);
    static void Log(const double message, Color color = Color::Black);
    static void Log(const bool message, Color color = Color::Black);
    static void send_log(const std::stringstream& ss, const Color& color);
};

//-------------------------------------------------------------------
void  Debug::Log(const char* message, Color color) 
{
    if (callbackInstance != nullptr)
        callbackInstance(message, (int)color, (int)strlen(message));
}

void  Debug::Log(const std::string message, Color color) {
    const char* tmsg = message.c_str();
    if (callbackInstance != nullptr)
        callbackInstance(tmsg, (int)color, (int)strlen(tmsg));
}

void  Debug::Log(const int message, Color color) {
    std::stringstream ss;
    ss << message;
    send_log(ss, color);
}

void  Debug::Log(const char message, Color color) 
{
    std::stringstream ss;
    ss << message;
    send_log(ss, color);
}

void  Debug::Log(const float message, Color color) 
{
    std::stringstream ss;
    ss << message;
    send_log(ss, color);
}

void  Debug::Log(const double message, Color color) 
{
    std::stringstream ss;
    ss << message;
    send_log(ss, color);
}

void Debug::Log(const bool message, Color color) 
{
    std::stringstream ss;
    if (message)
        ss << "true";
    else
        ss << "false";

    send_log(ss, color);
}

void Debug::send_log(const std::stringstream& ss, const Color& color) 
{
    const std::string tmp = ss.str();
    const char* tmsg = tmp.c_str();
    std::cout << ss.str() << std::endl;
    if (callbackInstance != nullptr)
        callbackInstance(tmsg, (int)color, (int)strlen(tmsg));
}

void RegisterDebugCallback(FuncCallBack cb) 
{
    callbackInstance = cb;
}

#define CUDA_CHECK( call )                                                     \
    do                                                                         \
    {                                                                          \
        cudaError_t error = call;                                              \
        if( error != cudaSuccess )                                             \
        {                                                                      \
            std::stringstream ss;                                              \
            ss << "CUDA call (" << #call << " ) failed with error: '"          \
               << cudaGetErrorString( error )                                  \
               << "' (" __FILE__ << ":" << __LINE__ << ")";                    \
            Debug::send_log(ss, Color::Red);                                   \
        }                                                                      \
    } while( 0 )

#define SUTIL_ASSERT( cond )                                                   \
    do                                                                         \
    {                                                                          \
        if( !(cond) )                                                          \
        {                                                                      \
            std::stringstream ss;                                              \
            ss << ": " << __FILE__ << " (" << __LINE__ << "): " << #cond;      \
            Debug::send_log(ss, Color::Red);                                   \
            std::cout << __FILE__ << " (" << __LINE__ << "): " << #cond;       \
        }                                                                      \
    } while( 0 )

#define SUTIL_ASSERT_MSG( cond, msg )                                          \
    do                                                                         \
    {                                                                          \
        if( !(cond) )                                                          \
        {                                                                      \
            std::stringstream ss;                                              \
            ss << ": " << __FILE__ << " (" << __LINE__ << "): " << #cond;      \
            Debug::send_log(ss, Color::Red);                                   \
            std::cout << ": " << __FILE__ << " (" << __LINE__ << "): " << #cond ; \
        }                                                                      \
    } while( 0 )

#define OPTIX_CHECK( call )                                                    \
    do                                                                         \
    {                                                                          \
        OptixResult res = call;                                                \
        if( res != OPTIX_SUCCESS )                                             \
        {                                                                      \
            std::stringstream ss;                                              \
            ss << "Optix call '" << #call << "' failed: " __FILE__ ":"         \
               << __LINE__ << ")";                                             \
            Debug::send_log(ss, Color::Red);                                   \
        }                                                                      \
    } while( 0 )

#define CUDA_SYNC_CHECK()                                                      \
    do                                                                         \
    {                                                                          \
        cudaDeviceSynchronize();                                               \
        cudaError_t error = cudaGetLastError();                                \
        if( error != cudaSuccess )                                             \
        {                                                                      \
            std::stringstream ss;                                              \
            ss << "CUDA error on synchronize with error '"                     \
               << cudaGetErrorString( error )                                  \
               << "' (" __FILE__ << ":" << __LINE__ << ")";                    \
            Debug::send_log(ss, Color::Red);                                   \
        }                                                                      \
    } while( 0 )


static void context_log_cb( uint32_t level, const char* tag, const char* message, void* /*cbdata*/ )
{
    if( level < 4 )
        std::cerr << "[" << std::setw( 2 ) << level << "][" << std::setw( 12 ) << tag << "]: "
                  << message << "\n";
}

// create four channel float OptixImage2D with given dimension. allocate memory on device and
// copy data from host memory given in hmem to device if hmem is nonzero.
static OptixImage2D createOptixImage2D( unsigned int width, unsigned int height, const float * hmem = nullptr ) 
{
    OptixImage2D oi;

    const uint64_t frame_byte_size = width * height * sizeof(float4);
    CUDA_CHECK( cudaMalloc( reinterpret_cast<void**>( &oi.data ), frame_byte_size ) );
    if( hmem )
    {
        CUDA_CHECK( cudaMemcpy(
                    reinterpret_cast<void*>( oi.data ),
                    hmem,
                    frame_byte_size,
                    cudaMemcpyHostToDevice
                    ) );
    }
    oi.width              = width;
    oi.height             = height;
    oi.rowStrideInBytes   = width*sizeof(float4);
    oi.pixelStrideInBytes = sizeof(float4);
    oi.format             = OPTIX_PIXEL_FORMAT_FLOAT4;
    return oi;
}

class OptiXDenoiser
{
public:
    struct Data
    {
        uint32_t  width    = 0;
        uint32_t  height   = 0;
        float*    color    = nullptr;
        float*    albedo   = nullptr;
        float*    normal   = nullptr;
        float*    flow     = nullptr;
        std::vector< float* > aovs;     // input AOVs
        std::vector< float* > outputs;  // denoised beauty, followed by denoised AOVs
        
        void clear()
        {
            width = 0;
            height = 0;
            color = nullptr;
            albedo = nullptr;
            normal = nullptr;
            aovs.clear();
            outputs.clear();
        }
    };

    // Initialize the API and push all data to the GPU -- normaly done only once per session
    // tileWidth, tileHeight: if nonzero, enable tiling with given dimension
    // kpMode: if enabled, use kernel prediction model even if no AOVs are given
    // temporalMode: if enabled, use a model for denoising sequences of images
    void init( const Data&  data,
               unsigned int tileWidth = 0,
               unsigned int tileHeight = 0,
               bool         kpMode = false,
               bool         temporalMode = false );

    // Execute the denoiser. In interactive sessions, this would be done once per frame/subframe
    void exec();

    // Update denoiser input data on GPU from host memory
    void update( const Data& data );

    // Copy results from GPU to host memory
    void getResults();

    // Cleanup state, deallocate memory -- normally done only once per render session
    void finish(); 

    // --- test flow vectors: flow is applied to noisy input image and written back to result
    // --- no denoising.
    void getFlowResults();

private:
    OptixDeviceContext    m_context      = nullptr;
    OptixDenoiser         m_denoiser     = nullptr;
    OptixDenoiserParams   m_params       = {};

    bool                  m_temporalMode;

    CUdeviceptr           m_intensity    = 0;
    CUdeviceptr           m_avgColor     = 0;
    CUdeviceptr           m_scratch      = 0;
    uint32_t              m_scratch_size = 0;
    CUdeviceptr           m_state        = 0;
    uint32_t              m_state_size   = 0;

    unsigned int          m_tileWidth    = 0;
    unsigned int          m_tileHeight   = 0;
    unsigned int          m_overlap      = 0;

    OptixDenoiserGuideLayer           m_guideLayer = {};
    std::vector< OptixDenoiserLayer > m_layers;
    std::vector< float* >             m_host_outputs;
};

void OptiXDenoiser::init( const Data&  data,
                          unsigned int tileWidth,
                          unsigned int tileHeight,
                          bool         kpMode,
                          bool         temporalMode )
{
    SUTIL_ASSERT( data.color  );
    SUTIL_ASSERT( data.outputs.size() >= 1 );
    SUTIL_ASSERT( data.width  );
    SUTIL_ASSERT( data.height );
    SUTIL_ASSERT_MSG( !data.normal || data.albedo, "Currently albedo is required if normal input is given" );
    SUTIL_ASSERT_MSG( ( tileWidth == 0 && tileHeight == 0 ) || ( tileWidth > 0 && tileHeight > 0 ), "tile size must be > 0 for width and height" );

    m_host_outputs = data.outputs;
    m_temporalMode = temporalMode;

    m_tileWidth  = tileWidth > 0 ? tileWidth : data.width;
    m_tileHeight = tileHeight > 0 ? tileHeight : data.height;

    //
    // Initialize CUDA and create OptiX context
    //
    {
        // Initialize CUDA
        CUDA_CHECK( cudaFree( nullptr ) );

        CUcontext cu_ctx = nullptr;  // zero means take the current context
        OPTIX_CHECK( optixInit() );
        OptixDeviceContextOptions options = {};
        options.logCallbackFunction       = &context_log_cb;
        options.logCallbackLevel          = 4;
        OPTIX_CHECK( optixDeviceContextCreate( cu_ctx, &options, &m_context ) );
    }

    //
    // Create denoiser
    //
    {
        /*****
        // load user provided model if model.bin is present in the currrent directory,
        // configuration of filename not done here.
        std::ifstream file( "model.bin" );
        if ( file.good() ) {
            std::stringstream source_buffer;
            source_buffer << file.rdbuf();
            OPTIX_CHECK( optixDenoiserCreateWithUserModel( m_context, (void*)source_buffer.str().c_str(), source_buffer.str().size(), &m_denoiser ) );
        }
        else
        *****/
        {
            OptixDenoiserOptions options = {};
            options.guideAlbedo = data.albedo ? 1 : 0;
            options.guideNormal = data.normal ? 1 : 0;

            OptixDenoiserModelKind modelKind;
            if( kpMode || data.aovs.size() > 0 )
            {
                SUTIL_ASSERT( !temporalMode );
                modelKind = OPTIX_DENOISER_MODEL_KIND_AOV;
            }
            else
            {
                modelKind = temporalMode ? OPTIX_DENOISER_MODEL_KIND_TEMPORAL : OPTIX_DENOISER_MODEL_KIND_HDR;
            }
            OPTIX_CHECK( optixDenoiserCreate( m_context, modelKind, &options, &m_denoiser ) );
        }
    }


    //
    // Allocate device memory for denoiser
    //
    {
        OptixDenoiserSizes denoiser_sizes;

        OPTIX_CHECK( optixDenoiserComputeMemoryResources(
                    m_denoiser,
                    m_tileWidth,
                    m_tileHeight,
                    &denoiser_sizes
                    ) );

        if( tileWidth == 0 )
        {
            m_scratch_size = static_cast<uint32_t>( denoiser_sizes.withoutOverlapScratchSizeInBytes );
            m_overlap = 0;
        }
        else
        {
            m_scratch_size = static_cast<uint32_t>( denoiser_sizes.withOverlapScratchSizeInBytes );
            m_overlap = denoiser_sizes.overlapWindowSizeInPixels;
        }

        if( data.aovs.size() == 0 && kpMode == false )
        {
            CUDA_CHECK( cudaMalloc(
                        reinterpret_cast<void**>( &m_intensity ),
                        sizeof( float )
                        ) );
        }
        else
        {
            CUDA_CHECK( cudaMalloc(
                        reinterpret_cast<void**>( &m_avgColor ),
                        3 * sizeof( float )
                        ) );
        }

        CUDA_CHECK( cudaMalloc(
                    reinterpret_cast<void**>( &m_scratch ),
                    m_scratch_size 
                    ) );

        CUDA_CHECK( cudaMalloc(
                    reinterpret_cast<void**>( &m_state ),
                    denoiser_sizes.stateSizeInBytes
                    ) );

        m_state_size = static_cast<uint32_t>( denoiser_sizes.stateSizeInBytes );

        OptixDenoiserLayer layer = {};
        layer.input  = createOptixImage2D( data.width, data.height, data.color );
        layer.output = createOptixImage2D( data.width, data.height );
        if( m_temporalMode )
        {
            // this is the first frame, create zero motion vector image
            void * flowmem;
            CUDA_CHECK( cudaMalloc( &flowmem, data.width * data.height * sizeof( float4 ) ) );
            CUDA_CHECK( cudaMemset( flowmem, 0, data.width * data.height * sizeof(float4) ) );
            m_guideLayer.flow = {(CUdeviceptr)flowmem, data.width, data.height, (unsigned int)(data.width * sizeof( float4 )), (unsigned int)sizeof( float4 ), OPTIX_PIXEL_FORMAT_FLOAT4 };

            layer.previousOutput = layer.input;         // first frame
        }
        m_layers.push_back( layer );

        if( data.albedo )
            m_guideLayer.albedo = createOptixImage2D( data.width, data.height, data.albedo );
        if( data.normal )
            m_guideLayer.normal = createOptixImage2D( data.width, data.height, data.normal );

        for( size_t i=0; i < data.aovs.size(); i++ )
        {
            layer.input  = createOptixImage2D( data.width, data.height, data.aovs[i] );
            layer.output = createOptixImage2D( data.width, data.height );
            if( m_temporalMode )
                layer.previousOutput = layer.input;     // first frame
            m_layers.push_back( layer );
        }
    }

    //
    // Setup denoiser
    //
    {
        OPTIX_CHECK( optixDenoiserSetup(
                    m_denoiser,
                    nullptr,  // CUDA stream
                    m_tileWidth + 2 * m_overlap,
                    m_tileHeight + 2 * m_overlap,
                    m_state,
                    m_state_size,
                    m_scratch,
                    m_scratch_size
                    ) );


        m_params.denoiseAlpha    = 0;
        m_params.hdrIntensity    = m_intensity;
        m_params.hdrAverageColor = m_avgColor;
        m_params.blendFactor     = 0.0f;
    }
}

void OptiXDenoiser::update( const Data& data )
{
    SUTIL_ASSERT( data.color  );
    SUTIL_ASSERT( data.outputs.size() >= 1 );
    SUTIL_ASSERT( data.width  );
    SUTIL_ASSERT( data.height );
    SUTIL_ASSERT_MSG( !data.normal || data.albedo, "Currently albedo is required if normal input is given" );

    m_host_outputs = data.outputs;

    CUDA_CHECK( cudaMemcpy( (void*)m_layers[0].input.data, data.color, data.width * data.height * sizeof( float4 ), cudaMemcpyHostToDevice ) );

    if( m_temporalMode )
    {
        CUDA_CHECK( cudaMemcpy( (void*)m_guideLayer.flow.data, data.flow, data.width * data.height * sizeof( float4 ), cudaMemcpyHostToDevice ) );
        m_layers[0].previousOutput = m_layers[0].output;
    }

    if( data.albedo )
        CUDA_CHECK( cudaMemcpy( (void*)m_guideLayer.albedo.data, data.albedo, data.width * data.height * sizeof( float4 ), cudaMemcpyHostToDevice ) );

    if( data.normal )
        CUDA_CHECK( cudaMemcpy( (void*)m_guideLayer.normal.data, data.normal, data.width * data.height * sizeof( float4 ), cudaMemcpyHostToDevice ) );

    for( size_t i=0; i < data.aovs.size(); i++ )
    {
        CUDA_CHECK( cudaMemcpy( (void*)m_layers[i].input.data, data.aovs[i], data.width * data.height * sizeof( float4 ), cudaMemcpyHostToDevice ) );
        if( m_temporalMode )
            m_layers[i].previousOutput = m_layers[i].output;
    }
}

void OptiXDenoiser::exec()
{
    if( m_intensity )
    {
        OPTIX_CHECK( optixDenoiserComputeIntensity(
                    m_denoiser,
                    nullptr, // CUDA stream
                    &m_layers[0].input,
                    m_intensity,
                    m_scratch,
                    m_scratch_size
                    ) );
    }
    
    if( m_avgColor )
    {
        OPTIX_CHECK( optixDenoiserComputeAverageColor(
                    m_denoiser,
                    nullptr, // CUDA stream
                    &m_layers[0].input,
                    m_avgColor,
                    m_scratch,
                    m_scratch_size
                    ) );
    }

    /**
    OPTIX_CHECK( optixDenoiserInvoke(
                m_denoiser,
                nullptr, // CUDA stream
                &m_params,
                m_state,
                m_state_size,
                &m_guideLayer,
                m_layers.data(),
                static_cast<unsigned int>( m_layers.size() ),
                0, // input offset X
                0, // input offset y
                m_scratch,
                m_scratch_size
                ) );
    **/
    OPTIX_CHECK( optixUtilDenoiserInvokeTiled(
                m_denoiser,
                nullptr, // CUDA stream
                &m_params,
                m_state,
                m_state_size,
                &m_guideLayer,
                m_layers.data(),
                static_cast<unsigned int>( m_layers.size() ),
                m_scratch,
                m_scratch_size,
                m_overlap,
                m_tileWidth,
                m_tileHeight
                ) );

    CUDA_SYNC_CHECK();
}

inline float catmull_rom(
    float       p[4],
    float       t)
{
    return p[1] + 0.5f * t * ( p[2] - p[0] + t * ( 2.f * p[0] - 5.f * p[1] + 4.f * p[2] - p[3] + t * ( 3.f * ( p[1] - p[2]) + p[3] - p[0] ) ) );
}

// apply flow to image at given pixel position (using bilinear interpolation), write back RGB result.
static void addFlow(
    float4*             result,
    const float4*       image,
    const float4*       flow,
    unsigned int        width,
    unsigned int        height,
    unsigned int        x,
    unsigned int        y )
{
    float dst_x = float( x ) - flow[x + y * width].x;
    float dst_y = float( y ) - flow[x + y * width].y;

    float x0 = dst_x - 1.f;
    float y0 = dst_y - 1.f;

    float r[4][4], g[4][4], b[4][4];
    for (int j=0; j < 4; j++)
    {
        for (int k=0; k < 4; k++)
        {
            int tx = static_cast<int>( x0 ) + k;
            if( tx < 0 )
                tx = 0;
            else if( tx >= (int)width )
                tx = width - 1;

            int ty = static_cast<int>( y0 ) + j;
            if( ty < 0 )
                ty = 0;
            else if( ty >= (int)height )
                ty = height - 1;

            r[j][k] = image[tx + ty * width].x;
            g[j][k] = image[tx + ty * width].y;
            b[j][k] = image[tx + ty * width].z;
        }
    }
    float tx = dst_x <= 0.f ? 0.f : dst_x - floorf( dst_x );

    r[0][0] = catmull_rom( r[0], tx );
    r[0][1] = catmull_rom( r[1], tx );
    r[0][2] = catmull_rom( r[2], tx );
    r[0][3] = catmull_rom( r[3], tx );

    g[0][0] = catmull_rom( g[0], tx );
    g[0][1] = catmull_rom( g[1], tx );
    g[0][2] = catmull_rom( g[2], tx );
    g[0][3] = catmull_rom( g[3], tx );

    b[0][0] = catmull_rom( b[0], tx );
    b[0][1] = catmull_rom( b[1], tx );
    b[0][2] = catmull_rom( b[2], tx );
    b[0][3] = catmull_rom( b[3], tx );

    float ty = dst_y <= 0.f ? 0.f : dst_y - floorf( dst_y );

    result[y * width + x].x = catmull_rom( r[0], ty );
    result[y * width + x].y = catmull_rom( g[0], ty );
    result[y * width + x].z = catmull_rom( b[0], ty );
}

void OptiXDenoiser::getFlowResults()
{
    if( m_layers.size() == 0 )
        return;

    const uint64_t frame_byte_size = m_layers[0].output.width*m_layers[0].output.height*sizeof(float4);

    const float4* device_flow = (float4*)m_guideLayer.flow.data;
    if( !device_flow )
        return;
    float4* flow = new float4[ frame_byte_size ];
    CUDA_CHECK( cudaMemcpy( flow, device_flow, frame_byte_size, cudaMemcpyDeviceToHost ) );

    float4* image = new float4[ frame_byte_size ];

    for( size_t i=0; i < m_layers.size(); i++ )
    {
        CUDA_CHECK( cudaMemcpy( image, (float4*)m_layers[i].input.data, frame_byte_size, cudaMemcpyDeviceToHost ) );

        for( unsigned int y=0; y < m_layers[i].input.height; y++ )
            for( unsigned int x=0; x < m_layers[i].input.width; x++ )
                addFlow( (float4*)m_host_outputs[i], image, flow, m_layers[i].input.width, m_layers[i].input.height, x, y );
    }

    delete[] image;
    delete[] flow;
}

void OptiXDenoiser::getResults()
{
    const uint64_t frame_byte_size = m_layers[0].output.width*m_layers[0].output.height*sizeof(float4);
    for( size_t i=0; i < m_layers.size(); i++ )
    {
        CUDA_CHECK( cudaMemcpy(
                    m_host_outputs[i],
                    reinterpret_cast<void*>( m_layers[i].output.data ),
                    frame_byte_size,
                    cudaMemcpyDeviceToHost
                    ) );
    }
}

void OptiXDenoiser::finish() 
{
    // Cleanup resources
    optixDenoiserDestroy( m_denoiser );
    optixDeviceContextDestroy( m_context );

    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_intensity)) );
    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_avgColor)) );
    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_scratch)) );
    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_state)) );
    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_guideLayer.albedo.data)) );
    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_guideLayer.normal.data)) );
    CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_guideLayer.flow.data)) );
    for( size_t i=0; i < m_layers.size(); i++ )
        CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_layers[i].input.data) ) );
    for( size_t i=0; i < m_layers.size(); i++ )
        CUDA_CHECK( cudaFree(reinterpret_cast<void*>(m_layers[i].output.data) ) ); 
}

static OptiXDenoiser::Data s_data;
static OptiXDenoiser* s_denoiser = nullptr;
static float* s_output_buffer = nullptr;

void optix_denoiser_set_image_size(uint32_t width, uint32_t height)
{
    Debug::Log("Width:" + std::to_string(width));
    Debug::Log("Height:" + std::to_string(height));
    s_data.width = width;
    s_data.height = height;
}
void optix_denoiser_set_source_data_pointer(float* ptr)
{
    s_data.color = ptr;
}
void optix_denoiser_set_normal_data_pointer(float* ptr)
{
    s_data.normal = ptr;
}
void optix_denoiser_set_albedo_data_pointer(float* ptr)
{
    s_data.albedo = ptr;
}
void optix_denoiser_init()
{
    Debug::Log("Denoiser Init");
    s_output_buffer = new float[s_data.width * s_data.height * 4];
    s_data.outputs.push_back(s_output_buffer);
    if (s_denoiser != nullptr)
    {
        delete s_denoiser;
    }
    s_denoiser = new OptiXDenoiser();
    s_denoiser->init(s_data);
}
void optix_denoiser_update()
{
    s_denoiser->update(s_data);
}
void optix_denoiser_exec()
{
    Debug::Log("Denoiser Exec");
    s_denoiser->exec();
}
float* optix_denoiser_get_result()
{
    s_denoiser->getResults();
    return s_data.outputs[0];
}
void optix_denoiser_free()
{
    delete[] s_output_buffer;
    s_denoiser->finish();
    delete s_denoiser;
    s_denoiser = nullptr;
    s_data.clear();
}
float* optix_denoiser_test()
{
    float* imageData; // width * height * RGBA
    int w;
    int h;
    const char* err = nullptr; // or nullptr in C++11
    int ret = LoadEXR(&imageData, &w, &h, "D:/Github/OptixDenoiserWrapper/PT_46s.exr", &err);
    return imageData;
}


