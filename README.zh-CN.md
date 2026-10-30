本文件面向第一次使用 ROS 2 的人，说明如何安装、启动并控制 LiteGrip 夹爪。

[English](README.md) · **简体中文**

# litegrip_moveit_config

LiteGrip 自适应两指夹爪的 MoveIt 2 配置包，也是 litegrip 栈的最上层：

```text
litegrip_cpp（C++ SDK）  ->  litegrip_ros2_control  ->  litegrip_moveit_config
```

```text
litegrip-moveit2/
└── litegrip_moveit_config/    ROS 2 包本体，也是 colcon 构建的对象
```

本包负责语义描述（SRDF）、规划与关节限位配置、MoveIt 控制器映射，以及一条把整条栈（含 ros2_control）拉起的演示 launch。它本身没有运行时代码，所有规划与执行都交给官方 MoveIt 2；底层硬件通过 `litegrip_ros2_control` 调用 `litegrip_cpp` SDK。

## 1. 准备环境

### 1.1 系统要求

| | |
| --- | --- |
| 操作系统 | Ubuntu 22.04（x86_64） |
| ROS 2 | Humble Hawksbill |
| 硬件 | 可选。dry run 没有夹爪也能跑完整条控制链路 |

其他 Ubuntu 版本或其他 ROS 2 发行版不在支持范围内。

### 1.2 安装 ROS 2 Humble

如果机器上还没有 ROS 2：

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

把 ROS 环境写进 `~/.bashrc`：

```bash
echo "source /opt/ros/humble/setup.bash" >> ~/.bashrc
source ~/.bashrc
```

### 1.3 安装依赖

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

### 1.4 关于 Conda

如果启用了 Conda，先退出：

```bash
conda deactivate
```

Conda 的 Python 会顶掉系统 Python，导致 `colcon build` 报 `ModuleNotFoundError: No module named 'catkin_pkg'`。**不要**靠安装更多 Conda 包绕过，直接使用系统解释器。

## 2. 创建工作区并获取仓库

整条栈由 4 个独立仓库组成，需要放在同一个 colcon 工作区里：

| 仓库 | 作用 |
| --- | --- |
| `litegrip-cpp` | C++ SDK，直接通过 SocketCAN 驱动电机 |
| `litegrip-description` | 夹爪的 URDF 模型 |
| `litegrip-ros2` | ros2_control 硬件插件，把 ROS 命令交给 SDK |
| `litegrip-moveit2` | MoveIt 配置和演示 launch |

```bash
mkdir -p ~/litegrip_ws/src
cd ~/litegrip_ws/src

git clone https://github.com/nexform-tech/litegrip-cpp.git
git clone https://github.com/nexform-tech/litegrip-description.git
git clone https://github.com/nexform-tech/litegrip-ros2.git
git clone https://github.com/nexform-tech/litegrip-moveit2.git
```

**不要**使用旧仓库 `litegrip-urdf`。那个仓库的包名是 `litegrip_urdf`，而本包依赖的包名是 `litegrip_description`。

## 3. 确认 description 版本兼容

本包的 SRDF 和 `litegrip.ros2_control.xacro` 要求机器人描述满足两点：

- 提供单个主关节 `gripper_opening_joint`，两个手指是它的 `<mimic>` 跟随关节；
- 接受 `include_mock_ros2_control` 参数，用来关闭 description 自带的 mock 控制块。

检查已拉取的 description：

```bash
grep -n "gripper_opening_joint\|include_mock_ros2_control" \
  ~/litegrip_ws/src/litegrip-description/litegrip_description/urdf/litegrip_description.urdf.xacro
```

输出为空表示版本不兼容。此时 launch 仍会启动，`robot_state_publisher` 也会发布模型，但 `move_group` 会报 `Joint 'gripper_opening_joint' ... is not known to the URDF`，规划组 `gripper` 为空，MotionPlanning 面板里没有可用的规划组。先换到提供单主关节模型的 description 分支或版本，再继续。

不要假设默认分支一定兼容，先跑上面的检查。

## 4. 编译

```bash
cd ~/litegrip_ws
source /opt/ros/humble/setup.bash
colcon build --packages-up-to litegrip_moveit_config
source install/setup.bash
```

`--packages-up-to` 会按依赖顺序编译全部 4 个包。每开一个新终端，都要重新执行一次 `source install/setup.bash`。

运行测试：

```bash
colcon test --packages-select litegrip_ros2_control litegrip_moveit_config
colcon test-result --verbose
```

## 5. 先看模型

这一步只加载 URDF，不涉及 MoveIt 和 ros2_control，适合确认安装和模型没问题：

```bash
ros2 launch litegrip_description display.launch.py
```

会打开 RViz 和一个滑条窗口。拖动滑条可以让手指开合。

## 6. 启动整条栈（dry run）

```bash
ros2 launch litegrip_moveit_config demo.launch.py
```

默认是 **dry run**：不打开 CAN 套接字，不碰电机。但 SDK 会针对一个仿真被控对象跑完整条控制链路（限速、力矩预算、安全闸），所以这个演示跑的是真实控制逻辑。

启动后应该看到：

- RViz 中出现夹爪模型；
- MotionPlanning 面板的规划组是 `gripper`；
- `joint_state_broadcaster` 和 `gripper_controller` 都是 `active`；
- `/joint_states` 持续发布。

另开一个终端检查：

```bash
source ~/litegrip_ws/install/setup.bash

ros2 control list_controllers
ros2 action list -t | grep gripper
ros2 topic echo /joint_states --once
```

期望看到 `/gripper_controller/gripper_cmd` 这个 action。如果控制器不是 `active`，先看本节末尾的排错表。

## 7. 控制夹爪

### 7.1 在 RViz 中拖动滑条

在 MotionPlanning 面板中：

1. 打开 **Query** 标签；
2. 选择 **Joints**；
3. 拖动 `gripper_opening_joint` 滑条；
4. 点击 **Plan & Execute**。

滑条范围是 0（闭合）到 0.085452（全开）米。实际可命令范围还会被 SDK 的安全红线再收窄一点，超出部分会被钳位。

### 7.2 使用命名状态

在 MotionPlanning 面板中：

1. 打开 **Goal State**；
2. 选择 `open` 或 `closed`；
3. 点击 **Plan & Execute**。

`open` 对应 0.085452 m，`closed` 对应 0.0 m。

### 7.3 在终端发送指令

不需要 RViz，直接通过 action 控制：

```bash
source ~/litegrip_ws/install/setup.bash

ros2 action send_goal /gripper_controller/gripper_cmd \
  control_msgs/action/GripperCommand \
  "{command: {position: 0.04, max_effort: 3.5}}" --feedback
```

| 字段 | 含义 |
| --- | --- |
| `position` | 开口，单位米。0 = 全闭，0.085452 = 全开 |
| `max_effort` | 最大力矩，单位 N·m。它只用于控制器的失速判定，真正的力矩预算由 SDK 的 `torque_limit_nm` 执行 |

`--feedback` 会打印 `reached_goal` 和 `stalled`。夹到物体停住时 `stalled=true` 是**正常成功**，不是失败。

### 7.4 用 Python 程序控制

新建 `gripper_client.py`：

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

运行：

```bash
python3 gripper_client.py
```

## 8. 接真机

真机模式下夹爪会实际动作。第一次尝试时清空手指行程，手离开夹爪。

### 8.1 配置 CAN

需要 USB-CAN 适配器（常见的是 `gs_usb` 芯片）和 `can-utils`：

```bash
sudo apt install -y can-utils
sudo modprobe gs_usb

sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000
sudo ip link set can0 up
ip -details link show can0
```

确认输出里是 `state UP` 和 `bitrate 1000000`。

### 8.2 用两个开关启动

```bash
ros2 launch litegrip_moveit_config demo.launch.py \
  dry_run:=false hardware_enable:=true \
  max_feedback_velocity_rad_s:=0.9
```

`dry_run` 和 `hardware_enable` 必须同时给出，少一个都不会动。

`0.9` 只是示例。`max_feedback_velocity_rad_s` 必须按你这台机器标定：让它跑真实工况，取峰值反馈速度，再加余量。默认值 `-1.0` 表示**未给出**，控制环会拒绝下发任何运动帧。这是刻意的 fail-closed 行为，不是占位符。

### 8.3 安全基线与标定

安全基线里的红线是参考机的实测值，不是你这台的。如果本机标定结果落在红线之外，夹爪会被拒绝一切运动，直到红线重新推导。标定流程见 SDK 的 README 和 description 包的标定文档。

## 9. 排错

| 现象 | 原因与处理 |
| --- | --- |
| `colcon build` 报 `ModuleNotFoundError: No module named 'catkin_pkg'` | Conda Python 顶掉了系统 Python，执行 `conda deactivate` 后重来 |
| 启动时报找不到 `gripper_opening_joint` | description 包版本不兼容，按第 3 节检查并更换版本 |
| `gripper_controller` 不是 `active` | 先确认 `/joint_states` 有发布；演示 launch 已经加载了 `demo_ros2_control.yaml` |
| `ros2 action list` 中没有 `gripper_cmd` | 控制器没起来，执行 `ros2 control list_controllers` 和 `ros2 control list_hardware_interfaces` 查看状态 |
| 真机模式下夹爪完全不动 | 大概率是 `max_feedback_velocity_rad_s` 没给或给错，查看 launch 输出的警告 |
| MoveIt 报 `Controller is taking too long to execute trajectory` | `joint_limits.yaml` 的速度上限和 SDK 限速不匹配。不要调大 `allowed_goal_duration_margin`，先核对下面的速度耦合 |

## 10. 包内容

| 路径 | 说明 |
| --- | --- |
| `urdf/litegrip_moveit.urdf.xacro` | 把 `litegrip_description` 的几何与 `litegrip_ros2_control` 的硬件组件组装成一份描述 |
| `srdf/litegrip.srdf.xacro` | SRDF 的唯一事实来源；构建时生成到 `config/litegrip.srdf` |
| `config/joint_limits.yaml` | MoveIt 侧的位置、速度、加速度限位 |
| `config/moveit_controllers.yaml` | 通过 GripperCommand action 驱动夹爪 |
| `config/ompl_planning.yaml` | 规划组的规划器设置 |
| `config/kinematics.yaml` | IK 求解器，RViz 创建可拖拽标记的前提 |
| `launch/demo.launch.py` | 整条栈：controller_manager、各控制器、move_group、RViz |
| `rviz/litegrip_moveit.rviz` | MotionPlanning 显示配置 |

## 11. 速度耦合

`config/joint_limits.yaml` 里的 `max_velocity` 不得超过驱动实际能做到的值：

```text
开合速率 [m/s] = max_velocity_rad_s [rad/s] * rad_to_mm [mm/rad] / 1000
```

设得过高时，MoveIt 规划的夹爪运动时长会远短于真实情况。`move_group` 随后只给控制器大约 `duration * allowed_goal_duration_margin` 的时间，而真实控制器还在运动，于是目标被以 `Controller is taking too long to execute trajectory` 杀掉。这看起来像硬件故障，实际是规划在说谎。

**不要**靠调大 `allowed_goal_duration_margin` 消除它，那会削弱整条栈里所有控制器的安全网。

`test/test_config_consistency.cpp` 会从已安装的 xacro 默认值与本文件重新推导这个关系，所以只改其中一处就会让测试套件变红。同一个测试还检查：位置限位与模型的 `2 * stroke` 一致、SRDF 与 URDF 对机器人名的说法一致、规划组包含主关节而**不**包含模仿手指、行程两端都是命名状态。

## 12. 为什么用 GripperCommand action

夹爪的正常结局是“闭合到接触就停”。抓取时它卡在物体上，永远到不了指令开度。在 `JointTrajectoryController` 下，这种结局只能靠 `goal_time: 0.0` 抢救，而那等于对所有情况关掉超时判定。`control_msgs/action/GripperCommand` 直接带 `stalled` 与 `reached_goal`，把同样的意图表达出来。

action 名是 `/gripper_controller/gripper_cmd`，不是 `follow_joint_trajectory`。

## 许可证

版权所有 © 2026 NEXFORM ROBOTICS。以 [Apache License 2.0](LICENSE) 发布。
