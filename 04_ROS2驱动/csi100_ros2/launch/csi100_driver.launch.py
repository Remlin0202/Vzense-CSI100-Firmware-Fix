from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    pkg = get_package_share_directory('csi100_driver')
    params = os.path.join(pkg, 'config', 'csi100_params.yaml')

    return LaunchDescription([
        Node(
            package='csi100_driver',
            executable='csi100_driver_node',
            name='csi100_driver',
            parameters=[params],
            output='screen',
            emulate_tty=True,
        ),
    ])
