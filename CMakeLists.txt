cmake_minimum_required(VERSION 3.0)

get_filename_component(PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${PROJECT_NAME})

add_definitions(-DRAKSAMP_CLIENT=1)
add_definitions(-D_RAKNET_THREADSAFE=1)
add_definitions(-DNDEBUG=1)
add_definitions(-D__ANDROID_API__=14)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/sources ${PROJECT_NAME}_LIST)

add_definitions("-fpack-struct=1 -fvisibility=default -fstrict-aliasing -fno-omit-frame-pointer -fexceptions -mfloat-abi=soft -fstack-protector -fno-short-enums -frtti -g2 -gdwarf-2 -mthumb -ferror-limit=1 -w")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,relro -Wl,-z,noexecstack -Wl,--no-undefined -Wl,-z,now -Wl,--strip-all")

add_library(${PROJECT_NAME} SHARED ${${PROJECT_NAME}_LIST})

set_target_properties(${PROJECT_NAME} PROPERTIES
	CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS YES
)

target_link_libraries(${PROJECT_NAME} dl log android)

