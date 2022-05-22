#
# ViewRanger VRC read-only raster driver
#

if(VRC_FIND_QUIETLY)
  set(_FIND_PNG_ARG QUIET)
endif()
find_package(PNG ${_FIND_PNG_ARG})

if(PNG_FOUND)
  set(PNG_INCLUDE_DIRS ${PNG_INCLUDE_DIRS})
  set(PNG_LIBRARIES ${PNG_LIBRARIES})
  set(PNG_VERSION_STRING ${PNG_VERSION_STRING})
  add_definitions(${PNG_DEFINITIONS})
endif()

#include(FeatureSummary)
#set_package_properties(Png PROPERTIES
#  DESCRIPTION "A Raster driver for ViewRanger VRC files"
#  )
