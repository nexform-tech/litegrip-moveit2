cmake_minimum_required(VERSION 3.16)
project(litegrip_moveit_config)

find_package(ament_cmake REQUIRED)

# ─────────────────────────────────────────────────────────────────────────────
# Generate the SRDF from its xacro at build time.
#
# The xacro stays the source of truth, and a real config/litegrip.srdf is
# installed alongside it so that tools which expect a plain SRDF (the setup
# assistant, inspection scripts) have one to read.
# ─────────────────────────────────────────────────────────────────────────────
find_program(XACRO_EXECUTABLE xacro REQUIRED)

set(SRDF_OUT ${CMAKE_CURRENT_BINARY_DIR}/litegrip.srdf)
add_custom_command(
  OUTPUT ${SRDF_OUT}
  COMMAND ${XACRO_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/srdf/litegrip.srdf.xacro
          -o ${SRDF_OUT}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/srdf/litegrip.srdf.xacro
  COMMENT "Generating litegrip.srdf from its xacro"
)
add_custom_target(litegrip_srdf ALL DEPENDS ${SRDF_OUT})

install(DIRECTORY launch config urdf srdf rviz
  DESTINATION share/${PROJECT_NAME}
)
install(FILES ${SRDF_OUT}
  DESTINATION share/${PROJECT_NAME}/config
)

# ─────────────────────────────────────────────────────────────────────────────
# Tests
#
# The one thing here that can silently ruin a real grasp is the coupling between
# the driver's rate ceiling and MoveIt's joint velocity limit. The test
# recomputes the relation from the two installed files, so changing either side
# alone turns the suite red.
# ─────────────────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_index_cpp REQUIRED)
  ament_add_gtest(test_config_consistency test/test_config_consistency.cpp)
  target_link_libraries(test_config_consistency
    ament_index_cpp::ament_index_cpp
  )
endif()

ament_package()
