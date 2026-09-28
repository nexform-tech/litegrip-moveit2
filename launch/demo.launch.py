"""demo.launch.py — bring up the whole LiteGrip stack and MoveIt.

    ros2 launch litegrip_moveit_config demo.launch.py
    ros2 launch litegrip_moveit_config demo.launch.py dry_run:=false hardware_enable:=true max_feedback_velocity_rad_s:=0.9

What this starts, in dependency order:

    ros2_control_node (controller_manager)   loads the LitegripSystem hardware
                                             component declared in the URDF
    joint_state_broadcaster                  /joint_states, which MoveIt needs
    gripper_controller                       position_controllers/GripperAction
                                             Controller, serving gripper_cmd
    robot_state_publisher                    TF and /robot_description
    move_group                               planning + the GripperCommand handle
    rviz2                                    MotionPlanning display

★ One description, one source: the robot description is built once and handed to
  every node that needs it. Letting each node run xacro separately is how a stack
  ends up with two subtly different robots.

★ dry_run defaults to true, and the real hardware needs BOTH switches
  (dry_run:=false and hardware_enable:=true). dry_run is not a no-op: the SDK
  runs its whole control path — rate limiting, torque budget, safety gate —
  against a simulated plant and only skips CAN, so this demo exercises the real
  control logic without a gripper attached.

★ About max_feedback_velocity_rad_s: the SDK needs a worst-case *measured*
  velocity to size the torque budget, and "-1 = not given" makes it refuse to
  send any motion frame (deliberate fail-closed). In DRY RUN this launch fills
  in the command rate ceiling, which is a bound derived from the simulation
  itself rather than an invented one. On the REAL path nothing is filled in: a
  value must be supplied, and the launch says so loudly if it is missing.
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, LogInfo, OpaqueFunction, TimerAction
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from moveit_configs_utils import MoveItConfigsBuilder


def _is_true(text: str) -> bool:
    return text.strip().lower() in ("1", "true", "yes", "on")


def generate_launch_description() -> LaunchDescription:
    declared = [
        DeclareLaunchArgument(
            "dry_run", default_value="true",
            description="true = the SDK's simulated plant (no CAN, no motor)"),
        DeclareLaunchArgument(
            "hardware_enable", default_value="false",
            description="real-hardware master switch; must be given together "
                        "with dry_run:=false"),
        DeclareLaunchArgument(
            "channel", default_value="can0",
            description="CAN interface carrying the gripper motor"),
        DeclareLaunchArgument(
            "safety_baseline", default_value="3.5",
            description="safety baseline version, or an explicit path to a "
                        "baseline file"),
        DeclareLaunchArgument(
            "max_feedback_velocity_rad_s", default_value="-1.0",
            description="worst-case feedback velocity bound (rad/s). -1 = NOT "
                        "GIVEN, which makes the control loop REFUSE to send any "
                        "motion frame. In dry run this launch substitutes the "
                        "command rate ceiling; on the real path you must "
                        "calibrate it"),
        DeclareLaunchArgument(
            "max_velocity_rad_s", default_value="1.5",
            description="command trajectory rate ceiling (rad/s); may only be "
                        "lowered below the SDK's own ceiling"),
        DeclareLaunchArgument(
            "torque_limit_nm", default_value="3.5",
            description="total control torque budget (N.m); may only be lowered"),
        DeclareLaunchArgument(
            "use_rviz", default_value="true",
            description="start rviz2 with the MotionPlanning display"),
    ]

    def _setup(context):
        moveit_share = get_package_share_directory("litegrip_moveit_config")
        control_share = get_package_share_directory("litegrip_ros2_control")

        dry_run = _is_true(LaunchConfiguration("dry_run").perform(context))
        bound_text = LaunchConfiguration("max_feedback_velocity_rad_s").perform(context)
        rate_text = LaunchConfiguration("max_velocity_rad_s").perform(context)
        try:
            bound = float(bound_text)
        except ValueError:
            bound = -1.0

        notes = []
        effective_bound = bound_text
        if bound <= 0.0 and dry_run:
            # Simulated plant: the measured velocity IS the commanded rate, so
            # the rate ceiling bounds it. Derived, not invented.
            effective_bound = rate_text
            notes.append(LogInfo(msg=(
                "[litegrip] dry run: max_feedback_velocity_rad_s not given, "
                f"using the command rate ceiling {rate_text} rad/s for the "
                "simulated plant.")))
        elif bound <= 0.0:
            notes.append(LogInfo(msg=(
                "[litegrip] ⚠ max_feedback_velocity_rad_s is NOT GIVEN — the "
                "control loop will REFUSE to send any motion frame, so the "
                "gripper will not move. Calibrate it on your unit (run it "
                "through real working conditions, take the peak feedback "
                "velocity, add margin) and pass it in.")))

        mappings = {
            "include_mock_ros2_control": "false",  # the real component is included
            "litegrip_dry_run": LaunchConfiguration("dry_run"),
            "litegrip_hardware_enable": LaunchConfiguration("hardware_enable"),
            "litegrip_channel": LaunchConfiguration("channel"),
            "litegrip_safety_baseline": LaunchConfiguration("safety_baseline"),
            "litegrip_max_feedback_velocity_rad_s": effective_bound,
            "litegrip_max_velocity_rad_s": LaunchConfiguration("max_velocity_rad_s"),
            "litegrip_torque_limit_nm": LaunchConfiguration("torque_limit_nm"),
        }

        moveit_config = (
            MoveItConfigsBuilder("litegrip", package_name="litegrip_moveit_config")
            .robot_description(
                file_path="urdf/litegrip_moveit.urdf.xacro", mappings=mappings)
            .robot_description_semantic(file_path="srdf/litegrip.srdf.xacro")
            .trajectory_execution(file_path="config/moveit_controllers.yaml")
            .joint_limits(file_path="config/joint_limits.yaml")
            # Pinned explicitly rather than relying on to_moveit_configs()'s
            # implicit default: this file is what gives the MotionPlanning panel
            # a draggable interactive marker at all.
            .robot_description_kinematics(file_path="config/kinematics.yaml")
            .planning_pipelines(pipelines=["ompl"])
            .to_moveit_configs()
        )

        # Built once, shared by every node (see the note at the top).
        robot_description = moveit_config.robot_description

        robot_state_publisher = Node(
            package="robot_state_publisher",
            executable="robot_state_publisher",
            output="both",
            parameters=[robot_description],
        )

        # controller_manager loads the LitegripSystem hardware component from the
        # URDF's <ros2_control> block and runs the controllers.
        control_node = Node(
            package="controller_manager",
            executable="ros2_control_node",
            output="both",
            parameters=[
                robot_description,
                os.path.join(control_share, "config", "litegrip_controllers.yaml"),
                os.path.join(moveit_share, "config", "demo_ros2_control.yaml"),
            ],
        )

        joint_state_broadcaster_spawner = Node(
            package="controller_manager",
            executable="spawner",
            arguments=["joint_state_broadcaster",
                       "--controller-manager", "/controller_manager"],
            output="both",
        )

        gripper_controller_spawner = Node(
            package="controller_manager",
            executable="spawner",
            arguments=["gripper_controller",
                       "--controller-manager", "/controller_manager"],
            output="both",
        )

        move_group = Node(
            package="moveit_ros_move_group",
            executable="move_group",
            output="both",
            parameters=[
                moveit_config.to_dict(),
                {"use_sim_time": False},
                # Keep execution enabled so the GripperCommand action goes out.
                {"allow_trajectory_execution": True},
            ],
        )

        rviz = Node(
            package="rviz2",
            executable="rviz2",
            name="rviz2",
            output="log",
            arguments=["-d", os.path.join(moveit_share, "rviz",
                                          "litegrip_moveit.rviz")],
            # ⚠ rviz2 needs the SEMANTIC description too, not just the URDF.
            #
            #   The MotionPlanning panel builds its own planning scene monitor
            #   inside the rviz node and reads `robot_description_semantic` from
            #   THAT node's parameters to populate the planning-group list.
            #   Passing only `robot_description` leaves the group list empty, and
            #   an empty group list means no interactive marker to drag — the
            #   panel looks broken rather than under-configured.
            #
            #   to_dict() carries robot_description, robot_description_semantic
            #   and the planning configs, which is what the panel expects.
            parameters=[moveit_config.to_dict(), {"use_sim_time": False}],
            condition=IfCondition(LaunchConfiguration("use_rviz")),
        )

        return notes + [
            robot_state_publisher,
            control_node,
            # Give the controller_manager a moment to load the hardware component
            # before asking it to spawn controllers.
            TimerAction(period=2.0, actions=[joint_state_broadcaster_spawner]),
            TimerAction(period=3.0, actions=[gripper_controller_spawner]),
            move_group,
            rviz,
        ]

    return LaunchDescription(declared + [OpaqueFunction(function=_setup)])
