add_subdirectory(misc)

aux_source_directory(src LIB_SRC)
add_library(adenium-teldaq STATIC ${LIB_SRC} )
target_link_libraries(adenium-teldaq
  PUBLIC
  mycommon
  PRIVATE
  adenium-teldaq-builtinJSON-headers
  )

target_include_directories(adenium-teldaq
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  )

install(TARGETS adenium-teldaq
  EXPORT ${PROJECT_NAME}Targets
  RUNTIME       DESTINATION bin      COMPONENT runtime
  LIBRARY       DESTINATION lib      COMPONENT runtime
  ARCHIVE       DESTINATION lib      COMPONENT devel
  PUBLIC_HEADER DESTINATION include  COMPONENT devel
  RESOURCE      DESTINATION resource COMPONENT runtime  
  )
