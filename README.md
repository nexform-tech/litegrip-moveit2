# litegrip_moveit_config

This document is for a first-time ROS 2 user: it takes you from installing the
dependencies to starting the stack and controlling the gripper.

**English** · [简体中文](README.zh-CN.md)

MoveIt 2 configuration for the **LiteGrip** adaptive two-finger gripper, and the
top layer of the litegrip stack:

```text
litegrip_cpp (C++ SDK)  ->  litegrip_ros2_control  ->  litegrip_moveit_config
```

```text
litegrip-moveit2/
└── litegrip_moveit_config/    the ROS 2 package, and what colcon builds
```

This package holds the semantic description (SRDF), the planning and joint-limit
configuration, the MoveIt controller mapping, and a demo launch that brings the
whole stack up, including ros2_control. It contains no runtime code: planning and
execution are handled by upstream MoveIt 2, and the hardware is reached through
`litegrip_ros2_control`, which calls the `litegrip_cpp` SDK.

## 1. Prepare the environment

### 1.1 Requirements

| | |
| --- | --- |
| Operating system | Ubuntu 22.04 (x86_64) |
| ROS 2 | Humble Hawksbill |
| Hardware | Optional. Dry run exercises the full control path without a gripper |

Other Ubuntu releases and other ROS 2 distributions are not supported.

### 1.2 Install ROS 2 Humble

If ROS 2 is not installed yet:

```bash
sudo apt update && sudo apt install -y curl software-properties-common
sudo add-apt-repository universe

sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
  -o /usr/share/keyrings/ros-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] \
http://packages.ros.org/ros2/ubuntu $(. /etc/os-release && echo $UBUNTU_CODENAME) main" \
  | sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null

sudo apt update
sudo apt install -y ros-humble-desktop ros-dev-tools
```

Add the ROS environment to `~/.bashrc`:

```bash
echo "source /opt/ros/humble/setup.bash" >> ~/.bashrc
source ~/.bashrc
```

### 1.3 Install dependencies

```bash
sudo apt install -y \
  ros-humble-moveit \
  ros-humble-moveit-configs-utils \
  ros-humble-ros2-control \
  ros-humble-ros2-controllers \
  ros-humble-gripper-controllers \
  ros-humble-controller-manager \
  ros-humble-joint-state-broadcaster \
  ros-humble-position-controllers \
  ros-humble-robot-state-publisher \
  ros-humble-xacro \
  ros-humble-rviz2
```

### 1.4 About Conda

If Conda is active, leave it first:

```bash
conda deactivate
```

Conda's Python shadows the system interpreter, and `colcon build` fails with
`ModuleNotFoundError: No module named 'catkin_pkg'`. Do not work around it by
installing more Conda packages; use the system interpreter.

## 2. Create the workspace and fetch the repositories

The stack is made of four repositories, all of which must sit in the same colcon
workspace:

| Repository | Role |
| --- | --- |
| `litegrip-cpp` | C++ SDK; drives the motor over SocketCAN |
| `litegrip-description` | URDF model of the gripper |
| `litegrip-ros2` | ros2_control hardware plugin that hands ROS commands to the SDK |
| `litegrip-moveit2` | MoveIt configuration and the demo launch |

```bash
mkdir -p ~/litegrip_ws/src
cd ~/litegrip_ws/src

git clone https://github.com/nexform-tech/litegrip-cpp.git
git clone https://github.com/nexform-tech/litegrip-description.git
git clone https://github.com/nexform-tech/litegrip-ros2.git
git clone https://github.com/nexform-tech/litegrip-moveit2.git
```

**Do not** use the old `litegrip-urdf` repository. Its package is named
`litegrip_urdf`, while this package depends on `litegrip_description`.

## 3. Confirm description compatibility

The SRDF and `litegrip.ros2_control.xacro` in this package require the robot
description to provide two things:

- a single master joint `gripper_opening_joint`, with both fingers as its
  `<mimic>` followers;
- an `include_mock_ros2_control` argument that turns off the description's own
  mock control block.

Check the description you cloned:

```bash
grep -n "gripper_opening_joint\|include_mock_ros2_control" \
  ~/litegrip_ws/src/litegrip-description/litegrip_description/urdf/litegrip_description.urdf.xacro
```

If the grep prints nothing, the versions are incompatible. The launch still
starts and `robot_state_publisher` still publishes a model, but `move_group`
logs `Joint 'gripper_opening_joint' ... is not known to the URDF`, the `gripper`
planning group is empty, and the MotionPlanning panel has no usable group.
Switch to a description branch or version that provides the single-joint model
before continuing.

Do not assume the default branch is compatible. Run the check above.

## 4. Build

```bash
cd ~/litegrip_ws
source /opt/ros/humble/setup.bash
colcon build --packages-up-to litegrip_moveit_config
source install/setup.bash
```

`--packages-up-to` builds all four packages in dependency order. Run
`source install/setup.bash` again in every new terminal.

Run the tests:

```bash
colcon test --packages-select litegrip_ros2_control litegrip_moveit_config
colcon test-result --verbose
```

## 5. Look at the model first

This step loads only the URDF. It does not start MoveIt or ros2_control, so it
is the quickest way to confirm the installation and the model:

```bash
ros2 launch litegrip_description display.launch.py
```

RViz opens with a slider window. Drag the sliders to move the fingers.

## 6. Start the full stack (dry run)

```bash
ros2 launch litegrip_moveit_config demo.launch.py
```

The default is **dry run**: no CAN socket is opened and no motor is touched. The
SDK still runs its entire control path (rate limiting, torque budget, safety
gate) against a simulated plant, so the demo exercises the real control logic.

When it comes up you should see:

- the gripper model in RViz;
- the MotionPlanning panel's planning group set to `gripper`;
- `joint_state_broadcaster` and `gripper_controller` both `active`;
- `/joint_states` publishing.

Check from another terminal:

```bash
source ~/litegrip_ws/install/setup.bash

ros2 control list_controllers
ros2 action list -t | grep gripper
ros2 topic echo /joint_states --once
```

You should see the `/gripper_controller/gripper_cmd` action. If a controller is
not `active`, see the troubleshooting table in section 9.

## 7. Control the gripper

### 7.1 Drag the slider in RViz

In the MotionPlanning panel:

1. Open the **Query** tab.
2. Select **Joints**.
3. Drag the `gripper_opening_joint` slider.
4. Click **Plan & Execute**.

The slider spans 0 (closed) to 0.085452 (fully open) metres. The commandable
range is narrower because the SDK's red lines bound it; values outside that range
are clamped.

### 7.2 Use a named state

In the MotionPlanning panel:

1. Open **Goal State**.
2. Select `open` or `closed`.
3. Click **Plan & Execute**.

`open` is 0.085452 m and `closed` is 0.0 m.

### 7.3 Send a goal from a terminal

RViz is not required. Send the action directly:

```bash
source ~/litegrip_ws/install/setup.bash

ros2 action send_goal /gripper_controller/gripper_cmd \
  control_msgs/action/GripperCommand \
  "{command: {position: 0.04, max_effort: 3.5}}" --feedback
```

| Field | Meaning |
| --- | --- |
| `position` | Opening in metres. 0 is fully closed, 0.085452 is fully open |
| `max_effort` | Maximum effort in N·m. It is used only by the controller's stall detection; the real torque budget is enforced by the SDK's `torque_limit_nm` |

`--feedback` prints `reached_goal` and `stalled`. When the gripper closes onto
an object and stops, `stalled=true` is a normal success, not a failure.

### 7.4 Control from Python

Create `gripper_client.py`:

```python
import rclpy
from control_msgs.action import GripperCommand
from rclpy.action import ActionClient
from rclpy.node import Node


class GripperClient(Node):
    def __init__(self):
        super().__init__("litegrip_client")
        self._client = ActionClient(
            self, GripperCommand, "/gripper_controller/gripper_cmd"
        )

    def open_to(self, opening_m: float, max_effort_nm: float = 3.5):
        self._client.wait_for_server()
        goal = GripperCommand.Goal()
        goal.command.position = opening_m
        goal.command.max_effort = max_effort_nm
        return self._client.send_goal_async(goal)


def main():
    rclpy.init()
    node = GripperClient()
    future = node.open_to(0.04)
    rclpy.spin_until_future_complete(node, future)
    print("goal accepted:", future.result().accepted)
    rclpy.shutdown()


if __name__ == "__main__":
    main()
```

Run it:

```bash
python3 gripper_client.py
```

## 8. Connect real hardware

In real-hardware mode the gripper moves. For the first attempt, clear the finger
travel and keep your hands away from the gripper.

### 8.1 Configure CAN

You need a USB-CAN adapter (commonly a `gs_usb` device) and `can-utils`:

```bash
sudo apt install -y can-utils
sudo modprobe gs_usb

sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000
sudo ip link set can0 up
ip -details link show can0
```

Confirm that the output shows `state UP` and `bitrate 1000000`.

### 8.2 Start with both switches

```bash
ros2 launch litegrip_moveit_config demo.launch.py \
  dry_run:=false hardware_enable:=true \
  max_feedback_velocity_rad_s:=0.9
```

`dry_run` and `hardware_enable` must both be given. With either one missing, the
gripper does not move.

`0.9` is an example. `max_feedback_velocity_rad_s` must be calibrated on your
unit: run it through real working conditions, take the peak feedback velocity,
and add margin. The default `-1.0` means **not given**, and the control loop
refuses to send any motion frame. That is deliberate fail-closed behaviour, not
a placeholder.

### 8.3 Safety baseline and calibration

The red lines in the safety baseline are the reference unit's measurements, not
your unit's. If your calibration puts the closed end outside those lines, the
gripper refuses all motion until the lines are re-derived. See the SDK README
and the description package's calibration guide for the procedure.

## 9. Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `colcon build` fails with `ModuleNotFoundError: No module named 'catkin_pkg'` | Conda's Python shadows the system interpreter. Run `conda deactivate` and build again |
| Startup reports `gripper_opening_joint` not found | The description package is incompatible. Check it as described in section 3 and switch versions |
| `gripper_controller` is not `active` | First confirm that `/joint_states` is publishing; the demo launch already loads `demo_ros2_control.yaml` |
| `gripper_cmd` is missing from `ros2 action list` | The controller did not start. Run `ros2 control list_controllers` and `ros2 control list_hardware_interfaces` |
| The gripper does not move in real-hardware mode | Most likely `max_feedback_velocity_rad_s` is missing or wrong. Read the launch warnings |
| MoveIt reports `Controller is taking too long to execute trajectory` | The velocity limit in `joint_limits.yaml` disagrees with the SDK's rate ceiling. Do not raise `allowed_goal_duration_margin`; check the velocity coupling in section 11 |

## 10. Package contents

| Path | Description |
| --- | --- |
| `urdf/litegrip_moveit.urdf.xacro` | Composes the geometry from `litegrip_description` with the hardware component from `litegrip_ros2_control` |
| `srdf/litegrip.srdf.xacro` | Source of truth for the SRDF; generated into `config/litegrip.srdf` at build time |
| `config/joint_limits.yaml` | MoveIt's position, velocity and acceleration limits |
| `config/moveit_controllers.yaml` | Drives the gripper through a GripperCommand action |
| `config/ompl_planning.yaml` | Planner settings for the group |
| `config/kinematics.yaml` | IK solver, required for RViz to create a draggable marker |
| `launch/demo.launch.py` | The whole stack: controller_manager, controllers, move_group, RViz |
| `rviz/litegrip_moveit.rviz` | MotionPlanning display configuration |

## 11. The velocity coupling

The `max_velocity` in `config/joint_limits.yaml` must not exceed what the driver
can actually do:

```text
opening rate [m/s] = max_velocity_rad_s [rad/s] * rad_to_mm [mm/rad] / 1000
```

If it is set too high, MoveIt plans a gripper motion whose duration is far
shorter than reality. `move_group` then allows the controller roughly
`duration * allowed_goal_duration_margin`, the real controller is still moving,
and the goal is killed with `Controller is taking too long to execute
trajectory`. That looks like a hardware fault, but the plan simply lied about
how long it takes.

**Do not** fix that by raising `allowed_goal_duration_margin`: that weakens the
safety net for every controller in the stack.

`test/test_config_consistency.cpp` recomputes the relation from the installed
xacro default and this file, so changing either one alone turns the test suite
red. The same test also checks that the position limits match the model's
`2 * stroke`, that the SRDF and URDF agree on the robot name, that the planning
group contains the master joint and **not** the mimic fingers, and that both
ends of travel are named states.

## 12. Why a GripperCommand action

A gripper's normal ending is "close until contact and stop". While grasping it
gets stuck on the object and never reaches the commanded opening. Under a
`JointTrajectoryController` that ending has to be rescued with `goal_time: 0.0`,
which switches off timeout judgement for everything.
`control_msgs/action/GripperCommand` carries `stalled` and `reached_goal` and
expresses the same intent directly.

The action name is `/gripper_controller/gripper_cmd`, not
`follow_joint_trajectory`.

## License

Copyright © 2026 NEXFORM ROBOTICS. Licensed under the
[Apache License 2.0](LICENSE).
