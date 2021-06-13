set(LIB_SRC  linenoise.cpp wcwidth.cpp ConvertUTF.cpp)
add_library(linenoiseng STATIC ${LIB_SRC})
target_include_directories(linenoiseng
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
  )

set(LIB_PUBLIC_HEADERS linenoise.h)
set_target_properties(linenoiseng PROPERTIES PUBLIC_HEADER "${LIB_PUBLIC_HEADERS}")


install(TARGETS linenoiseng
  EXPORT ${PROJECT_NAME}Targets
  RUNTIME       DESTINATION bin      COMPONENT runtime
  LIBRARY       DESTINATION lib      COMPONENT runtime
  ARCHIVE       DESTINATION lib      COMPONENT devel
  PUBLIC_HEADER DESTINATION include  COMPONENT devel
  RESOURCE      DESTINATION resource COMPONENT runtime
  )
