cmake_minimum_required(VERSION 3.1)
project(OptixDenoiserWrapper VERSION 0.1.0)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

set(CMAKE_MODULE_PATH
"${CMAKE_SOURCE_DIR}/CMake"
${CMAKE_MODULE_PATH}
)

find_package(CUDA 5.0 REQUIRED)
include_directories(
"$ENV{OPTIX_SDK}/include"
${CUDA_INCLUDE_DIRS}
)

add_library(OptixDenoiserWrapper SHARED optix_denoiser_wrapper.cpp)
target_link_libraries(OptixDenoiserWrapper ${CUDA_LIBRARIES})

add_executable(Test test.cpp)
target_link_libraries(Test OptixDenoiserWrapper)
# target_link_libraries(Test OptixDenoiserWrapper ${CUDA_LIBRARIES})