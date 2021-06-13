add_custom_target(
   adenium-dma-module
   COMMAND make
   WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
   VERBATIM
   USES_TERMINAL
   )

set_target_properties(adenium-dma-module PROPERTIES EXCLUDE_FROM_ALL TRUE)


add_custom_target(
   adenium-dma-module-install
   COMMAND make modules_install
   WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
   VERBATIM
   USES_TERMINAL
   )

set_target_properties(adenium-dma-module-install PROPERTIES EXCLUDE_FROM_ALL TRUE)
