include(FetchContent)
include(ExternalProject)
FetchContent_Declare(
  external_eudaq
  GIT_REPOSITORY https://github.com/eudaq/eudaq.git
  GIT_TAG fc77bf805509e910091917bfa4072ced3f5b3480
  # FETCHCONTENT_UPDATES_DISCONNECTED ON
)
FetchContent_Populate(external_eudaq)

externalproject_add(
  external_eudaq
  SOURCE_DIR ${external_eudaq_SOURCE_DIR}
  CMAKE_ARGS -DEUDAQ_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}  -DEUDAQ_BUILD_GUI=ON -DEUDAQ_BUILD_STDEVENT_MONITOR=ON -DEUDAQ_LIBRARY_BUILD_LCIO=OFF -DUSER_EUDET_BUILD=ON -DUSER_TLU_BUILD=ON -DUSER_TLU_BUILD_EUDET=OFF
  PATCH_COMMAND unzip ${CMAKE_CURRENT_LIST_DIR}/eudaq_tlu_extern.zip -d ${external_eudaq_SOURCE_DIR}/user/tlu
  COMMAND rm -f ${CMAKE_INSTALL_PREFIX}/extern/LCIOConfig.cmake
  BUILD_ALWAYS true
  BUILD_COMMAND make -j2
  INSTALL_COMMAND make install
  COMMAND   echo ""
  COMMAND   echo ""
  )

set_target_properties(external_eudaq PROPERTIES EXCLUDE_FROM_ALL TRUE)
