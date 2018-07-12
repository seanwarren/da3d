cmake_minimum_required (VERSION 2.8)
project (da3d)

# Find Matlab
find_package(Matlab COMPONENTS MEX_COMPILER MX_LIBRARY REQUIRED)

# GCC on MacOs needs this option to use the clang assembler
if ((CMAKE_CXX_COMPILER_ID STREQUAL "GNU") AND (APPLE))
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wa,-q")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wa,-q")
endif ()

# Optimize to the current CPU and enable warnings
if ((CMAKE_CXX_COMPILER_ID STREQUAL "GNU") OR
    (CMAKE_CXX_COMPILER_ID STREQUAL "Clang") OR
    (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"))
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native -Wall -Wextra")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -Wall -Wextra")
endif ()

# Enable C++11
if (CMAKE_VERSION VERSION_LESS "3.1")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
else ()
  set (CMAKE_CXX_STANDARD 11)
endif ()

# Enable OpenMP
find_package (OpenMP)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")

# Link LibFFTW
find_path (FFTW_INCLUDE_DIR fftw3.h)
find_library (FFTWF_LIBRARIES NAMES fftw3f)
link_libraries (${FFTWF_LIBRARIES})
if (NOT FFTW_INCLUDE_DIR)
  message (FATAL_ERROR "FFTW3 not found.")
endif ()

set(SOURCE_FILES DA3D.cpp DA3D.hpp DftPatch.hpp Image.hpp WeightMap.cpp WeightMap.hpp Utils.cpp Utils.hpp main.cpp)

matlab_add_mex(NAME da3d ${SOURCE_FILES} LINK_TO ${FFTWF_LIBRARIES})
target_include_directories(da3d PUBLIC ${FFTW_INCLUDE_DIR})

if (MEX_OUT_DIR)
   add_custom_command(TARGET da3d POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:da3d> ${MEX_OUT_DIR})
endif()