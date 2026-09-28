// test_config_consistency.cpp — the couplings that silently ruin a real grasp.
//
// Everything here reads the INSTALLED files, because those are what actually
// run. The values are not hard-coded on either side: the test recomputes the
// relation from the two files, so changing either one alone turns the suite red.

#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <gtest/gtest.h>

namespace {

std::string read_file(const std::string &path) {
  std::ifstream input(path);
  EXPECT_TRUE(input.good()) << "cannot read " << path;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

/// The default of a xacro arg, e.g. `<xacro:arg name="litegrip_rad_to_mm"
/// default="65.0231"/>`.
double xacro_arg_default(const std::string &text, const std::string &name) {
  const std::regex pattern("<xacro:arg\\s+name=\"" + name +
                           "\"\\s+default=\"([^\"]+)\"");
  std::smatch match;
  if (!std::regex_search(text, match, pattern)) {
    ADD_FAILURE() << "no xacro arg named " << name;
    return std::nan("");
  }
  return std::stod(match[1].str());
}

/// The value of `max_velocity:` in a joint_limits YAML, restricted to the joint
/// we care about by scanning from its name onwards.
double joint_limit_velocity(const std::string &text, const std::string &joint) {
  const std::size_t joint_pos = text.find(joint + ":");
  if (joint_pos == std::string::npos) {
    ADD_FAILURE() << "no joint " << joint << " in joint_limits.yaml";
    return std::nan("");
  }
  const std::regex pattern("max_velocity:\\s*([0-9.eE+-]+)");
  std::smatch match;
  const std::string tail = text.substr(joint_pos);
  if (!std::regex_search(tail, match, pattern)) {
    ADD_FAILURE() << "no max_velocity for " << joint;
    return std::nan("");
  }
  return std::stod(match[1].str());
}

}  // namespace

/// The joint velocity limit MoveIt plans with must not exceed what the driver
/// can actually do.
///
/// If it is higher, the plan claims a duration shorter than reality, move_group
/// allows the controller roughly `duration * margin` to finish, the real
/// controller is still moving, and the goal is killed with "Controller is taking
/// too long to execute trajectory" — which looks like a hardware fault but is a
/// planning lie. The check is one-sided: planning a little slower than the
/// hardware can go is simply conservative.
TEST(ConfigConsistency, JointVelocityMatchesTheDriverRateCeiling) {
  const std::string control_share =
      ament_index_cpp::get_package_share_directory("litegrip_ros2_control");
  const std::string moveit_share =
      ament_index_cpp::get_package_share_directory("litegrip_moveit_config");

  const std::string xacro =
      read_file(control_share + "/urdf/litegrip.ros2_control.xacro");
  const double max_velocity_rad_s = xacro_arg_default(xacro, "litegrip_max_velocity_rad_s");
  const double rad_to_mm = xacro_arg_default(xacro, "litegrip_rad_to_mm");
  ASSERT_TRUE(std::isfinite(max_velocity_rad_s));
  ASSERT_TRUE(std::isfinite(rad_to_mm));

  const double driver_ceiling_m_s = max_velocity_rad_s * rad_to_mm / 1000.0;
  const double planned = joint_limit_velocity(
      read_file(moveit_share + "/config/joint_limits.yaml"),
      "gripper_opening_joint");
  ASSERT_TRUE(std::isfinite(planned));

  EXPECT_LE(planned, driver_ceiling_m_s + 1e-9)
      << "joint_limits.yaml allows " << planned << " m/s but the driver's rate "
      << "ceiling is " << driver_ceiling_m_s << " m/s ("
      << max_velocity_rad_s << " rad/s * " << rad_to_mm << " mm/rad). A plan "
      << "faster than the hardware makes move_group kill the goal for taking "
      << "too long.";

  // And not absurdly slower either: a limit far below the ceiling is not a
  // safety win, it just makes every motion crawl. 5x is the tolerance.
  EXPECT_GE(planned * 5.0, driver_ceiling_m_s)
      << "joint_limits.yaml is an order of magnitude slower than the driver can "
      << "go; that is a configuration slip, not caution";
}

/// The joint limits must agree with the model's own travel, or MoveIt refuses
/// positions the URDF says exist.
TEST(ConfigConsistency, JointPositionLimitsMatchTheModel) {
  const std::string urdf_share =
      ament_index_cpp::get_package_share_directory("litegrip_urdf");
  const std::string urdf =
      read_file(urdf_share + "/urdf/litegrip_urdf.urdf.xacro");
  const double stroke = xacro_arg_default(urdf, "stroke");

  const std::string moveit_share =
      ament_index_cpp::get_package_share_directory("litegrip_moveit_config");
  const std::string limits =
      read_file(moveit_share + "/config/joint_limits.yaml");

  const std::regex max_pattern("max_position:\\s*([0-9.eE+-]+)");
  std::smatch match;
  ASSERT_TRUE(std::regex_search(limits, match, max_pattern));
  const double max_position = std::stod(match[1].str());

  // The master joint's value is the total opening: twice one finger's travel.
  EXPECT_NEAR(max_position, 2.0 * stroke, 1e-9)
      << "joint_limits.yaml max_position disagrees with the model's "
      << "2 * stroke (" << 2.0 * stroke << " m)";
}

/// The SRDF must describe a robot with the same name as the URDF, or MoveIt
/// silently fails to attach the semantic description.
TEST(ConfigConsistency, SrdfAndUrdfAgreeOnTheRobotName) {
  const std::string moveit_share =
      ament_index_cpp::get_package_share_directory("litegrip_moveit_config");
  const std::string srdf = read_file(moveit_share + "/config/litegrip.srdf");
  const std::string urdf_xacro = read_file(moveit_share + "/urdf/litegrip_moveit.urdf.xacro");

  const std::regex name_pattern("<robot[^>]*name=\"([^\"]+)\"");
  std::smatch srdf_match;
  std::smatch urdf_match;
  ASSERT_TRUE(std::regex_search(srdf, srdf_match, name_pattern));
  ASSERT_TRUE(std::regex_search(urdf_xacro, urdf_match, name_pattern));
  EXPECT_EQ(srdf_match[1].str(), urdf_match[1].str());
}

/// The planning group must contain the master joint and NOT the mimic fingers:
/// a mimic joint has no independent command port, so putting it in a group makes
/// every plan unsatisfiable.
TEST(ConfigConsistency, PlanningGroupHoldsOnlyTheMasterJoint) {
  const std::string moveit_share =
      ament_index_cpp::get_package_share_directory("litegrip_moveit_config");
  const std::string srdf = read_file(moveit_share + "/config/litegrip.srdf");

  // [\s\S] rather than ".": the group's contents span several lines.
  const std::regex group_pattern("<group name=\"gripper\">([\\s\\S]*?)</group>");
  std::smatch group_match;
  ASSERT_TRUE(std::regex_search(srdf, group_match, group_pattern));
  const std::string group = group_match[1].str();

  EXPECT_NE(group.find("gripper_opening_joint"), std::string::npos)
      << "the group must contain the master joint";
  EXPECT_EQ(group.find("gripper_slide_joint_left"), std::string::npos)
      << "the mimic finger joints must not be in the group";
  EXPECT_EQ(group.find("gripper_slide_joint_right"), std::string::npos)
      << "the mimic finger joints must not be in the group";
}

/// The OMPL pipeline must NAME its plugin.
///
/// Without `planning_plugin`, MoveIt's PlanningPipeline falls back to picking an
/// arbitrary pluginlib planning plugin it can find. On this installation that
/// resolved to CHOMP, which segfaults on a 1-DOF group and took move_group down
/// the moment a goal was sent. Nothing in that symptom points back to a missing
/// YAML key, so it is pinned here.
TEST(ConfigConsistency, OmplPipelineNamesItsPlugin) {
  const std::string moveit_share =
      ament_index_cpp::get_package_share_directory("litegrip_moveit_config");
  const std::string ompl =
      read_file(moveit_share + "/config/ompl_planning.yaml");

  EXPECT_NE(ompl.find("planning_plugin:"), std::string::npos)
      << "ompl_planning.yaml must name planning_plugin; without it MoveIt picks "
         "an arbitrary available planner (measured: CHOMP, which crashes on "
         "this 1-DOF group)";
  EXPECT_NE(ompl.find("ompl_interface/OMPLPlanner"), std::string::npos)
      << "planning_plugin must be the OMPL plugin";
  EXPECT_NE(ompl.find("request_adapters:"), std::string::npos)
      << "ompl_planning.yaml must declare request_adapters, as MoveIt's own "
         "default config does";
}

/// Both ends of the travel must be named states, so they can be commanded by
/// name without anyone hard-coding a number.
TEST(ConfigConsistency, NamedStatesCoverBothEndsOfTravel) {
  const std::string moveit_share =
      ament_index_cpp::get_package_share_directory("litegrip_moveit_config");
  const std::string srdf = read_file(moveit_share + "/config/litegrip.srdf");

  // ⚠ Order-insensitive on purpose: xacro re-emits attributes in its own order
  //   (alphabetical), so "<group_state group="..." name="open">" is what the
  //   generated file actually contains — not the order written in the xacro.
  const std::regex open_state("<group_state[^>]*name=\"open\"");
  const std::regex closed_state("<group_state[^>]*name=\"closed\"");
  EXPECT_TRUE(std::regex_search(srdf, open_state))
      << "no named state 'open'";
  EXPECT_TRUE(std::regex_search(srdf, closed_state))
      << "no named state 'closed'";
}
