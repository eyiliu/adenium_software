add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/altel_tele_conf_json.hh
  COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/cpprawstring.cmake ${CMAKE_CURRENT_BINARY_DIR}/altel_tele_conf_json.hh ${CMAKE_CURRENT_SOURCE_DIR}/altel_tele_conf.json
  )

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/altel_layer_conf_json.hh
  COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/cpprawstring.cmake ${CMAKE_CURRENT_BINARY_DIR}/altel_layer_conf_json.hh ${CMAKE_CURRENT_SOURCE_DIR}/altel_layer_conf.json
  )

add_custom_target(adenium-teldaq-builtinJSON-custom ALL DEPENDS
  ${CMAKE_CURRENT_BINARY_DIR}/altel_tele_conf_json.hh
  ${CMAKE_CURRENT_BINARY_DIR}/altel_layer_conf_json.hh)

add_library(adenium-teldaq-builtinJSON-headers INTERFACE)

add_dependencies(adenium-teldaq-builtinJSON-headers adenium-teldaq-builtinJSON-custom)
target_include_directories(adenium-teldaq-builtinJSON-headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

install(TARGETS adenium-teldaq-builtinJSON-headers
  EXPORT ${PROJECT_NAME}Targets
  RUNTIME       DESTINATION bin      COMPONENT runtime
  LIBRARY       DESTINATION lib      COMPONENT runtime
  ARCHIVE       DESTINATION lib      COMPONENT devel
  PUBLIC_HEADER DESTINATION include  COMPONENT devel
  RESOURCE      DESTINATION resource COMPONENT runtime
  )
