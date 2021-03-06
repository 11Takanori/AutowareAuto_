# Copyright 2020-2021 The Autoware Foundation.
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

"""Testing of mpc_controller in LGSVL simulation using recordreplay planner."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import IncludeLaunchDescription
from launch.substitutions import LaunchConfiguration
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node
from ament_index_python import get_package_share_directory
import os


def get_share_file(package_name, file_name):
    return os.path.join(get_package_share_directory(package_name), file_name)


def generate_launch_description():
    """
    Launch nodes with params to test mpc_controller in simulation.

    mpc_controller + LGSVL + recordreplay planner + lexus_rx_450h_description
    """
    # --------------------------------- Params -------------------------------
    joy_translator_param_file = get_share_file(
        package_name='test_trajectory_following',
        file_name='param/logitech_f310_velocity_cmd.param.yaml')
    rviz_cfg_path = get_share_file(
        package_name='test_trajectory_following', file_name='config/default_control.rviz')
    urdf_path = get_share_file(
        package_name='lexus_rx_450h_description', file_name='urdf/lexus_rx_450h.urdf')
    with open(urdf_path, 'r') as infp:
        urdf_file = infp.read()

    # --------------------------------- Arguments -------------------------------
    joy_translator_param = DeclareLaunchArgument(
        'joy_translator_param_file',
        default_value=joy_translator_param_file,
        description='Path to config file for joystick translator'
    )

    # -------------------------------- Nodes-----------------------------------

    joy = Node(
        package='joy',
        executable='joy_node'
    )

    joy_translator = Node(
        package='joystick_vehicle_interface_nodes',
        executable='joystick_vehicle_interface_node_exe',
        parameters=[LaunchConfiguration('joy_translator_param_file')],
        remappings=[
            ('vehicle_command', '/vehicle/vehicle_command')
        ]
    )

    recordreplay_planner_path = get_share_file(
        package_name='recordreplay_planner_nodes',
        file_name='launch/recordreplay_planner_node.launch.py')
    recordreplay_planner_node = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(recordreplay_planner_path)
    )

    joy_ctrl_record_replay_traj = Node(
        package="test_trajectory_following",
        executable="joy_ctrl_record_replay_traj.py",
        name="joy_ctrl_record_replay_traj",
        parameters=[],
        remappings=[
            ("vehicle_kinematic_state", "/vehicle/vehicle_kinematic_state")
        ],
        output='screen',
    )

    vehicle_kinematics_sim_node = Node(
        package='test_trajectory_following',
        executable='vehicle_kinematics_sim.py',
        namespace='vehicle',
        output='screen',
    )

    lexus_rx_450h_description = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        name='robot_state_publisher',
        parameters=[{'robot_description': urdf_file}])

    rviz2 = Node(
        package='rviz2',
        executable='rviz2',
        name='rviz2',
        arguments=['-d', str(rviz_cfg_path)]
    )

    return LaunchDescription([
        joy_translator_param,
        joy,
        joy_translator,
        recordreplay_planner_node,
        joy_ctrl_record_replay_traj,
        vehicle_kinematics_sim_node,
        lexus_rx_450h_description,
        rviz2
    ])
