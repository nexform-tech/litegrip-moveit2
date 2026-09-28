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
| `config/kinematics.yaml` | an IK solver for the group — needed so RViz can create a draggable marker (see below) |
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

## Driving the gripper from RViz

In the **MotionPlanning** panel the planning group is `gripper`. Three ways to
move the gripper, in increasing order of how well they suit a gripper:

| | |
|---|---|
| **Drag the interactive marker** | works, and is what `config/kinematics.yaml` exists for — but only the component along the opening axis is solvable, since a 1-DOF chain cannot satisfy an arbitrary 6-DOF pose. Dragging sideways will fail to find an IK solution, which is expected |
| **Joints tab** | a slider for `gripper_opening_joint`; exact and always solvable |
| **Plan / Execute with a named state** | `open` (0.087 m) and `closed` (0.0 m), the two ends of travel |

Two configuration details both have to be right for any of this to appear, and
both fail *silently*:

- **`config/kinematics.yaml` must define a solver for the group.** MoveIt's
  MotionPlanning display only creates a draggable marker for a group that has
  one — the drag becomes a pose, and without a solver there is nothing to turn
  the pose into a joint value, so no marker is created at all. The group still
  shows up in the panel; it just cannot be dragged.
- **`rviz2` must be given `robot_description_semantic`, not only
  `robot_description`.** The panel runs its own planning scene monitor inside
  the rviz node and reads the SRDF from *that* node's parameters to populate the
  planning-group list. Given only the URDF, the group list is empty. The demo
  launch passes `moveit_config.to_dict()` for exactly this reason.

Verified: the panel reports `group gripper`, the interactive marker display
initialises, and `/compute_ik` solves a pose 2 cm along the opening axis to
`gripper_opening_joint = 0.02` with `error_code = 1 (SUCCESS)`.

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
