cmake_minimum_required(VERSION 3.16)

project(hpc_task_runtime VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HPC_BUILD_BENCHMARKS "Build benchmark executables" ON)

add_library(hpc_runtime
    src/task_runtime.cpp
)

target_include_directories(hpc_runtime
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(hpc_runtime
    PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

if(HPC_BUILD_BENCHMARKS)
    add_executable(hpc_benchmarks
        benchmarks/benchmark_main.cpp
    )

    target_link_libraries(hpc_benchmarks
        PRIVATE
            hpc_runtime
    )

    target_compile_options(hpc_benchmarks
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
endif()
