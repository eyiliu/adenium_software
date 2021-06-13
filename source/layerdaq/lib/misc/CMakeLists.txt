add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/altel_reg_cmd_list_json.hh
  COMMAND ${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/cpprawstring.cmake ${CMAKE_CURRENT_BINARY_DIR}/altel_reg_cmd_list_json.hh ${CMAKE_CURRENT_SOURCE_DIR}/altel_reg_cmd_list.json
  )

add_custom_target(adenium-layerdaq-builtinJSON-custom ALL DEPENDS
  ${CMAKE_CURRENT_BINARY_DIR}/altel_reg_cmd_list_json.hh
  )

add_library(adenium-layerdaq-builtinJSON-headers INTERFACE)

add_dependencies(adenium-layerdaq-builtinJSON-headers adenium-layerdaq-builtinJSON-custom)
target_include_directories(adenium-layerdaq-builtinJSON-headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)

install(TARGETS adenium-layerdaq-builtinJSON-headers
  EXPORT ${PROJECT_NAME}Targets
  RUNTIME       DESTINATION bin      COMPONENT runtime
  LIBRARY       DESTINATION lib      COMPONENT runtime
  ARCHIVE       DESTINATION lib      COMPONENT devel
  PUBLIC_HEADER DESTINATION include  COMPONENT devel
  RESOURCE      DESTINATION resource COMPONENT runtime
  )

