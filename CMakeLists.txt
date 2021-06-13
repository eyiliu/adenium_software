cmake_minimum_required(VERSION 3.11 FATAL_ERROR)
project(adenium VERSION 1.0.0 LANGUAGES CXX C)

message(STATUS "CMAKE_ROOT:                  ${CMAKE_ROOT}")
message(STATUS "CMAKE_VERSION:               ${CMAKE_VERSION}")
message(STATUS "CMAKE_HOST_SYSTEM:           ${CMAKE_HOST_SYSTEM}")
message(STATUS "CMAKE_HOST_SYSTEM_PROCESSOR: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_SYSTEM:                ${CMAKE_SYSTEM}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR:      ${CMAKE_SYSTEM_PROCESSOR}")


if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR}/INSTALL CACHE PATH "Default prefix of install directories" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
  message(STATUS "Setting build type to '${CMAKE_BUILD_TYPE}' as none was specified.")
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(BUILD_SHARED_LIBS TRUE)

if(NOT BUILD_SHARED_LIBS)
  set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
endif()

set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(CMAKE_INSTALL_RPATH "@loader_path/../lib64;@loader_path/../lib")
else()
  set(CMAKE_INSTALL_RPATH "\$ORIGIN/../lib64:\$ORIGIN/../lib")
endif()
set(CMAKE_SKIP_BUILD_RPATH  FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)


# find_package order
set(CMAKE_FIND_USE_PACKAGE_ROOT_PATH       ON)   # 1. -DXX_ROOT and user ENV XX_ROOT
set(CMAKE_FIND_USE_CMAKE_PATH              ON)   # 2. -DCMAKE_PREFIX_PATH
set(CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH  OFF)  # 3. user ENV CMAKE_PREFIX_PATH  and XX_DIR
#                                                # 4. find_package option HINTS
set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH OFF)  # 5. ENV PATH
set(CMAKE_FIND_USE_PACKAGE_REGISTRY        OFF)  # 6. user registery: /home/user/.cmake
set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH       ON)   # 7. system CMAKE_SYSTEM_PREFIX_PATH
set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY OFF)  # 8. system registery: somewhere
#                                                # 9. find_package option PATHS

## thread                                          
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


add_subdirectory(external)


if(ADENIUM_EXTERNAL_CONFIG_ONLY)
  message("")
  message("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
  message("!!!option ADENIUM_EXTERNAL_CONFIG_ONLY is temporarily enabled,  (by -DADENIUM_EXTERNAL_CONFIG_ONLY=ON)") 
  message("!!!cmake is now configured to build external packages only")
  message("!!!To build eudaq:")  
  message("!!!>> make external_eudaq")
  message("!!!Re-run cmake configuration will disable ADENIUM_EXTERNAL_CONFIG_ONLY. Then you can continue to build the main software")
  message("!!!>> cmake ..")
  message("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
  message("")
  message("")
  unset(ADENIUM_EXTERNAL_CONFIG_ONLY CACHE)
  return()
endif()

add_subdirectory(source)

# install-tree
set(CONF_INCLUDE_DIRS "\${CMAKE_CURRENT_LIST_DIR}/../include")
configure_file(cmake/Config.cmake.in         ${PROJECT_BINARY_DIR}/cmake_install/${PROJECT_NAME}Config.cmake @ONLY)
configure_file(cmake/ConfigVersion.cmake.in  ${PROJECT_BINARY_DIR}/cmake_install/${PROJECT_NAME}ConfigVersion.cmake @ONLY)
install(FILES
  ${PROJECT_BINARY_DIR}/cmake_install/${PROJECT_NAME}Config.cmake
  ${PROJECT_BINARY_DIR}/cmake_install/${PROJECT_NAME}ConfigVersion.cmake
  DESTINATION cmake COMPONENT devel)

install(EXPORT ${PROJECT_NAME}Targets DESTINATION cmake)


# build-tree
set(CONF_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/../source/lib/include ${PROJECT_BINARY_DIR})
configure_file(cmake/Config.cmake.in          ${PROJECT_BINARY_DIR}/cmake/${PROJECT_NAME}Config.cmake @ONLY)
configure_file(cmake/ConfigVersion.cmake.in   ${PROJECT_BINARY_DIR}/cmake/${PROJECT_NAME}ConfigVersion.cmake @ONLY)

export(PACKAGE ${PROJECT_NAME})

