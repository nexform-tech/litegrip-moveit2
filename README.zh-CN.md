# litegrip_moveit_config

**LiteGrip** 自适应两指夹爪的 MoveIt 2 配置包。

[English](README.md) · **简体中文**

本包是 litegrip 栈的最上层：

```text
litegrip_cpp（C++ SDK）  ->  litegrip_ros2_control  ->  litegrip_moveit_config
```

```text
litegrip-moveit2/
└── litegrip_moveit_config/    ROS 2 包本体，也是 colcon 构建的对象
```

它承载语义描述（SRDF）、规划与关节限位配置、MoveIt 控制器映射，以及一条把整个栈
（**含 ros2_control**）拉起的演示 launch。

下文中的路径都相对于 `litegrip_moveit_config/`。

## 快速上手

从干净的工作区到「能在 RViz 里规划并执行」的夹爪，共四条命令。以下全部运行在
**dry run** 下：不打开 CAN 套接字，不碰电机。

```bash
source /opt/ros/humble/setup.bash
colcon build --packages-select litegrip_cpp litegrip_ros2_control litegrip_moveit_config
source install/setup.bash
ros2 launch litegrip_moveit_config demo.launch.py
```

在 colcon 工作区根目录执行，即同时包含上述三个包的那一层。`demo.launch.py`
默认就是 dry run，可以照抄执行；dry run 与真机的区别见[演示](#演示)。

启动成功后应当看到：RViz 里出现夹爪模型，MotionPlanning 面板的规划组为
`gripper`；`gripper_controller` 与 `joint_state_broadcaster` 均为 active；
`/joint_states` 持续发布。让它动起来见[在 RViz 中驱动夹爪](#在-rviz-中驱动夹爪)。

### 下一步：MoveIt 2 教程

以下都指向 MoveIt 2 官方教程的 Humble 版，与本栈的目标发行版一致：

| 教程 | 讲什么 |
| --- | --- |
| [MoveIt Quickstart in RViz](https://moveit.picknik.ai/humble/doc/tutorials/quickstart_in_rviz/quickstart_in_rviz_tutorial.html) | 本 launch 打开的就是这个面板：规划组、规划路径、交互式标记 |
| [MoveIt Setup Assistant](https://moveit.picknik.ai/humble/doc/examples/setup_assistant/setup_assistant_tutorial.html) | `config/` 下的 SRDF、关节限位、运动学文件是怎么生成的 |
| [URDF and SRDF](https://moveit.picknik.ai/humble/doc/examples/urdf_srdf/urdf_srdf_tutorial.html) | `urdf/litegrip_moveit.urdf.xacro` 与 `srdf/litegrip.srdf.xacro` 的职责划分 |
| [Low Level Controllers](https://moveit.picknik.ai/humble/doc/examples/controller_configuration/controller_configuration_tutorial.html) | MoveIt 如何把轨迹交给 ros2_control —— `config/moveit_controllers.yaml` 实现的正是它 |
| [Kinematics Configuration](https://moveit.picknik.ai/humble/doc/examples/kinematics_configuration/kinematics_configuration_tutorial.html) | `config/kinematics.yaml`，它才是 RViz 里出现可拖拽标记的原因 |
| [Move Group C++ Interface](https://moveit.picknik.ai/humble/doc/examples/move_group_interface/move_group_interface_tutorial.html) | 不经过 RViz，在自己的节点里规划并执行 |

教程总览：[tutorials](https://moveit.picknik.ai/humble/doc/tutorials/tutorials.html)；
示例总览：[examples](https://moveit.picknik.ai/humble/doc/examples/examples.html)。

## 包内容

| 路径 | 说明 |
| --- | --- |
| `urdf/litegrip_moveit.urdf.xacro` | 把 `litegrip_urdf` 的几何与 `litegrip_ros2_control` 的真实硬件组件组装成一份描述 |
| `srdf/litegrip.srdf.xacro` | SRDF 的唯一事实来源；构建时生成到 `config/litegrip.srdf` |
| `config/joint_limits.yaml` | ⚠ 见下文「速度耦合」 |
| `config/moveit_controllers.yaml` | 通过 GripperCommand action 驱动夹爪 |
| `config/ompl_planning.yaml` | 规划组的规划器设置 |
| `config/kinematics.yaml` | 规划组的 IK 求解器 —— RViz 能生成可拖拽标记的前提（见下文） |
| `launch/demo.launch.py` | 整个栈：controller_manager、各控制器、move_group、RViz |
| `rviz/litegrip_moveit.rviz` | MotionPlanning 显示配置 |

## 演示

```bash
ros2 launch litegrip_moveit_config demo.launch.py
```

上面这条运行在 **dry run**：不打开 CAN 套接字，不碰电机。但它并非空转 —— SDK 会针对
一个仿真被控对象跑完整条控制链路（轨迹限速、力矩预算、红线安全闸），所以这个演示跑的
是真实控制逻辑。你应当看到 RViz 中的夹爪、状态为 active 的 `gripper_controller`，以及
持续发布的 `/joint_states`。

真机需要**两个**开关同时给出，而且其中一个在夹爪动起来之前是真正必需的：

```bash
ros2 launch litegrip_moveit_config demo.launch.py \
  dry_run:=false hardware_enable:=true \
  max_feedback_velocity_rad_s:=0.9        # 需按你自己这台标定 —— 见下文
```

⚠ `max_feedback_velocity_rad_s` 默认为 `-1.0`，含义是**未给出**，此时控制环会**拒绝下发
任何运动帧**。这是刻意设计的故障安全（fail-closed）行为：力矩预算是从一个最坏情况速度
上界推导出来的，没有上界就没有推导依据。

⚠ 安全基线里的红线是**参考机**的实测值，不是你自己这台的。如果某台的标定结果把闭合端落
在这些红线之外，它会被——正确地——拒绝一切运动，直到红线重新推导。参见 SDK 的 README。

## 在 RViz 中驱动夹爪

在 **MotionPlanning** 面板里，规划组是 `gripper`。有三种驱动方式，按对夹爪的适配度递增排列：

| | |
| --- | --- |
| **拖动交互式标记** | 可行，`config/kinematics.yaml` 就是为此存在的 —— 但只有沿开合轴的分量可解，因为 1 自由度链无法满足任意 6 自由度位姿。横向拖动会解不出 IK，这是预期行为 |
| **Joints 标签页** | `gripper_opening_joint` 一个滑条；精确，且永远可解 |
| **用命名状态 Plan / Execute** | `open`（0.087 m）与 `closed`（0.0 m），行程的两端 |

以下两处配置必须同时正确，上述任何一项才会出现，而且两者都**静默失败**：

- **`config/kinematics.yaml` 必须为规划组定义求解器。** MoveIt 的 MotionPlanning 显示只为有
  求解器的组创建可拖拽标记 —— 拖动会变成一个位姿，没有求解器就没有东西把位姿变成关节值，
  于是标记根本不会创建。规划组仍然会出现在面板里，只是拖不动。
- **`rviz2` 必须同时拿到 `robot_description_semantic`，而不只是 `robot_description`。** 面板在
  rviz 节点内部运行自己的规划场景监视器，读取**该节点**参数里的 SRDF 来填充规划组列表。
  只给 URDF，组列表就是空的。演示 launch 正是因此传入 `moveit_config.to_dict()`。

已验证：面板显示 `group gripper`，交互式标记显示初始化成功，`/compute_ik` 能把沿开合轴
2 cm 处的位姿解到 `gripper_opening_joint = 0.02`，`error_code = 1 (SUCCESS)`。

## 速度耦合（改其中任何一个数字之前先读这一节）

`config/joint_limits.yaml` 里的 `max_velocity` 不得超过驱动实际能做到的值：

```text
开合速率 [m/s] = max_velocity_rad_s [rad/s] * rad_to_mm [mm/rad] / 1000
```

一旦设得过高，MoveIt 规划的夹爪运动时长会远短于真实情况。`move_group` 随后只给控制器大约
`duration * allowed_goal_duration_margin` 的时间，而真实控制器（已被 SDK 正确限速）还在运动，
于是目标被以 `Controller is taking too long to execute trajectory` 杀掉。这看起来像硬件故障，
实际是规划在说谎。

**不要**靠调大 `allowed_goal_duration_margin` 来消除它；那会削弱整条栈里所有控制器的安全网。

`test/test_config_consistency.cpp` 会从已安装的 xacro 默认值与本文件重新推导这个关系，所以
只改其中一处就会让测试套件变红。同一个测试还检查：位置限位与模型的 `2 * stroke` 一致、
SRDF 与 URDF 对机器人名的说法一致、规划组包含主关节而**不**包含模仿手指、行程两端都是
命名状态。

## 为什么用 GripperCommand action 而不是轨迹控制器

夹爪的正常结局是「闭合到接触就停」—— 抓取时它卡在物体上，永远到不了指令开度。在
`JointTrajectoryController` 下，这种结局只能靠 `goal_time: 0.0` 抢救，而那等于对所有情况
关掉超时判定。`control_msgs/action/GripperCommand` 直接带 `stalled` 与 `reached_goal`，
把同样的意图表达出来。

⚠ action 名是 `/gripper_controller/gripper_cmd`（不是 `follow_joint_trajectory`）。任何写死
旧名字的东西都必须更新。

## 构建

```bash
colcon build --packages-select litegrip_cpp litegrip_ros2_control litegrip_moveit_config
colcon test  --packages-select litegrip_moveit_config
```

## 许可证

版权所有 © 2026 NEXFORM ROBOTICS。以 [Apache License 2.0](LICENSE) 发布。
