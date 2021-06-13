add_subdirectory(linenoiseng)
add_subdirectory(getopt)
add_subdirectory(rapidjson)

set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(mycommon INTERFACE)

target_link_libraries(mycommon INTERFACE
  ${CMAKE_DL_LIBS} #dl not sure if it works with full static
  Threads::Threads
  linenoiseng
  getopt
  rapidjson
  $<$<AND:$<CXX_COMPILER_ID:GNU>,$<AND:$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>,$<VERSION_GREATER:$<CXX_COMPILER_VERSION>,5.3>>>:stdc++fs>
  )

target_include_directories(mycommon
  INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
  )

set(LIB_PUBLIC_HEADERS mysystem.hh)
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.15.0") 
  set_target_properties(mycommon PROPERTIES PUBLIC_HEADER "${LIB_PUBLIC_HEADERS}")  
else()
  message(STATUS "Unable to export PUBLIC_HEADER from interface target by CMake < 3.15, using CMake INSTALL FILES alternatively")
  install(FILES ${LIB_PUBLIC_HEADERS} DESTINATION include)
endif()

install(TARGETS mycommon
  EXPORT ${PROJECT_NAME}Targets
  RUNTIME       DESTINATION bin      COMPONENT runtime
  LIBRARY       DESTINATION lib      COMPONENT runtime
  ARCHIVE       DESTINATION lib      COMPONENT devel
  PUBLIC_HEADER DESTINATION include  COMPONENT devel
  RESOURCE      DESTINATION resource COMPONENT runtime
  )
