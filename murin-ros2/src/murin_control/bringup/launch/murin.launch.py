# Copyright 2020 ros2_control Development Team
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction, RegisterEventHandler
from launch_ros.parameter_descriptions import ParameterFile, ParameterValue
from launch.conditions import IfCondition
from launch.event_handlers import OnShutdown
from launch.substitutions import Command, LaunchConfiguration, PathSubstitution

from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def launch_nodes(context):
    def enabled(name):
        return LaunchConfiguration(name).perform(context).lower() == "true"

    if enabled("use_mock_hardware") and (
        enabled("enable_socket_bridge") or enabled("require_bridge")
    ):
        raise ValueError(
            "The socket bridge requires the Murin plugin; use transport:=simulation for a hardware-free bridge"
        )
    controller_file = ParameterFile(
        PathSubstitution(FindPackageShare("murin_control"))
        / "config"
        / "murin_controllers.yaml",
        allow_substs=True,
    )
    controller_path = controller_file.evaluate(context)
    return [
        RegisterEventHandler(
            OnShutdown(
                on_shutdown=[
                    OpaqueFunction(function=lambda context: controller_file.cleanup()),
                ]
            )
        ),
        Node(
            package="murin_control",
            executable="robot_socket_bridge",
            parameters=[
                {
                    "server_url": LaunchConfiguration("server_url"),
                    "wheel_separation": ParameterValue(
                        LaunchConfiguration("wheel_separation"), value_type=float
                    ),
                }
            ],
            condition=IfCondition(LaunchConfiguration("enable_socket_bridge")),
            output="screen",
        ),
        # Control node
        Node(
            package="controller_manager",
            executable="ros2_control_node",
            name="controller_manager",
            parameters=[{"update_rate": 50}],
            output="both",
        ),
        # robot_state_publisher with robot_description from xacro
        Node(
            package="robot_state_publisher",
            executable="robot_state_publisher",
            output="both",
            parameters=[
                {
                    "robot_description": Command(
                        [
                            "xacro",
                            " ",
                            PathSubstitution(FindPackageShare("murin_control"))
                            / "urdf"
                            / "murin.urdf.xacro",
                            " ",
                            "transport:=",
                            LaunchConfiguration("transport"),
                            " serial_port:='",
                            LaunchConfiguration("serial_port"),
                            "'",
                            " baud_rate:=",
                            LaunchConfiguration("baud_rate"),
                            " wheel_radius:=",
                            LaunchConfiguration("wheel_radius"),
                            " wheel_separation:=",
                            LaunchConfiguration("wheel_separation"),
                            " require_bridge:=",
                            LaunchConfiguration("require_bridge"),
                            " use_mock_hardware:=",
                            LaunchConfiguration("use_mock_hardware"),
                        ]
                    )
                }
            ],
        ),
        Node(
            package="rviz2",
            executable="rviz2",
            name="rviz2",
            output="log",
            arguments=[
                "-d",
                PathSubstitution(FindPackageShare("murin_control"))
                / "rviz"
                / "murin.rviz",
            ],
            condition=IfCondition(LaunchConfiguration("gui")),
        ),
        Node(
            package="controller_manager",
            executable="spawner",
            name="controller_spawner",
            arguments=[
                "joint_state_broadcaster",
                "murin_base_controller",
                "--param-file",
                controller_path,
                "--controller-ros-args",
                "-r ~/cmd_vel:=/cmd_vel",
            ],
        ),
    ]


def generate_launch_description():
    return LaunchDescription(
        [
            DeclareLaunchArgument(
                "gui",
                default_value="true",
                description="Start RViz2 automatically with this launch file.",
            ),
            DeclareLaunchArgument(
                "use_mock_hardware",
                default_value="false",
                description="Start robot with mock hardware mirroring command to its states.",
            ),
            DeclareLaunchArgument(
                "transport",
                default_value="simulation",
                choices=["simulation", "serial"],
            ),
            DeclareLaunchArgument("serial_port", default_value=""),
            DeclareLaunchArgument("baud_rate", default_value="2000000"),
            DeclareLaunchArgument("wheel_radius", default_value="0.015"),
            DeclareLaunchArgument("wheel_separation", default_value="0.10"),
            DeclareLaunchArgument("enable_socket_bridge", default_value="false"),
            DeclareLaunchArgument(
                "require_bridge",
                default_value=LaunchConfiguration("enable_socket_bridge"),
            ),
            DeclareLaunchArgument("server_url", default_value="http://localhost:9091"),
            OpaqueFunction(function=launch_nodes),
        ]
    )
