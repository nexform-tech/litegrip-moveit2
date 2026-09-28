# litegrip_moveit_config

MoveIt 2 configuration for the **LiteGrip** adaptive two-finger gripper.

This is the top layer of the litegrip stack:

```
litegrip_cpp (C++ SDK)  ->  litegrip_ros2_control  ->  litegrip_moveit_config
```

It holds the semantic description (SRDF), the planning and joint-limit
configuration, the MoveIt controller mapping, and a demo launch that brings the
whole stack up **including ros2_control**.

## Contents

| Path | |
|---|---|
| `urdf/litegrip_moveit.urdf.xacro` | composes the geometry from `litegrip_urdf` with the real hardware component from `litegrip_ros2_control` |
| `srdf/litegrip.srdf.xacro` | source of truth for the SRDF; generated into `config/litegrip.srdf` at build time |
| `config/joint_limits.yaml` | ⚠ see "velocity coupling" below |
| `config/moveit_controllers.yaml` | drives the gripper through a GripperCommand action |
| `config/ompl_planning.yaml` | planner settings for the group |
| `config/kinematics.yaml` | intentionally empty — the group is joint-space only |
| `launch/demo.launch.py` | the whole stack: controller_manager, controllers, move_group, RViz |
| `rviz/litegrip_moveit.rviz` | MotionPlanning display |

## Demo

```bash
ros2 launch litegrip_moveit_config demo.launch.py
```

That runs in **dry run**: no CAN socket is opened and no motor is touched. It is
not a no-op, though — the SDK runs its entire control path (trajectory rate
limiting, torque budget, red-line safety gate) against a simulated plant, so the
demo exercises the real control logic. You should see the gripper in RViz, the
`gripper_controller` active, and `/joint_states` publishing.

Real hardware needs **both** switches, and one of them is genuinely required
before it will move at all:

```bash
ros2 launch litegrip_moveit_config demo.launch.py \
  dry_run:=false hardware_enable:=true \
  max_feedback_velocity_rad_s:=0.9        # calibrate this on your unit — see below
```

⚠ `max_feedback_velocity_rad_s` defaults to `-1.0`, which means **NOT GIVEN**,
which makes the control loop **refuse to send any motion frame**. That is
deliberate fail-closed behaviour: the torque budget is argued from a worst-case
velocity bound, and without one there is nothing to argue from.

⚠ The red lines shipped in the safety baseline are the **reference unit's**
measurement, not your gripper's. A unit whose calibration puts its closed end
outside those lines will — correctly — be refused all motion until the lines are
re-derived. See the SDK's README.

## The velocity coupling (read this before changing either number)

`config/joint_limits.yaml`'s `max_velocity` must not exceed what the driver can
actually do:

```
opening rate [m/s] = max_velocity_rad_s [rad/s] * rad_to_mm [mm/rad] / 1000
```

If it is set too high, MoveIt plans a gripper motion whose duration is far
shorter than reality. `move_group` then allows the controller roughly
`duration * allowed_goal_duration_margin`, the real controller — correctly rate
limited by the SDK — is still moving, and the goal is killed with
`Controller is taking too long to execute trajectory`. That looks like a
hardware fault but is a planning lie.

Do **not** fix that by raising `allowed_goal_duration_margin`; that weakens the
safety net for every controller in the stack.

`test/test_config_consistency.cpp` recomputes the relation from the installed
xacro default and this file, so changing either one alone turns the test suite
red. The same test also checks that the position limits match the model's
`2 * stroke`, that the SRDF and URDF agree on the robot name, that the planning
group contains the master joint and **not** the mimic fingers, and that both ends
of the travel are named states.

## Why a GripperCommand action and not a trajectory controller

A gripper's normal ending is "close until contact and stop" — while grasping it
gets stuck on the object and never reaches the commanded opening. Under a
`JointTrajectoryController` that ending has to be rescued with `goal_time: 0.0`,
which switches off timeout judgement for everything. `control_msgs/action/
GripperCommand` carries `stalled` and `reached_goal` and expresses the same
intent directly.

⚠ The action name is `/gripper_controller/gripper_cmd` (not
`follow_joint_trajectory`). Anything hard-wired to the old name must be updated.

## Build

```bash
colcon build --packages-select litegrip_cpp litegrip_ros2_control litegrip_moveit_config
colcon test  --packages-select litegrip_moveit_config
```

## License

BSD-3-Clause.
