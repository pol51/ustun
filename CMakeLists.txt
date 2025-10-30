cmake_minimum_required(VERSION 3.14)
project(ustun LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB SRC_FILES src/*.cpp)

add_executable(${PROJECT_NAME} ${SRC_FILES})

find_package(Boost REQUIRED COMPONENTS system)
find_package(spdlog REQUIRED)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Boost::system
        spdlog::spdlog
)

target_compile_options(${PROJECT_NAME}
    PRIVATE
        -Wall -Wextra -Wpedantic
)

install(TARGETS ${PROJECT_NAME})
