option(ADENIUM_BUILD_EUDAQ_MODULE "build eudaq module" ON)
if(NOT ADENIUM_BUILD_EUDAQ_MODULE)
  message(STATUS "Module eudaq_module_adenium is disabled (ADENIUM_BUILD_EUDAQ_MODULE=OFF)")
  return()
endif()
message(STATUS "Module eudaq_module_adenium is enabled (ADENIUM_BUILD_EUDAQ_MODULE=ON)")

find_package(eudaq REQUIRED
  COMPONENTS eudaq::core
  CONFIG
  PATHS ${CMAKE_INSTALL_PREFIX}
  NO_DEFAULT_PATH
  )

aux_source_directory(src MODULE_SRC)

add_library(eudaq_module_adenium SHARED ${MODULE_SRC})
target_link_libraries(eudaq_module_adenium eudaq::core adenium-teldaq mycommon)

install(TARGETS
  eudaq_module_adenium
  EXPORT ${PROJECT_NAME}Targets
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)
