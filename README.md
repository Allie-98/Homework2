# Homework2

After cloning the repository, build the packages by doing:

     $ colcon build

Then, use the source command:

    $ source install/setup.bash

If you want to simulate the robot using the position controller, write the following command in a terminal:

    $ ros2 launch iiwa_bringup iiwa.launch.py

In another terminal, use the following command to run the node:

    $ros2 run ros2_kdl_package ros2_kdl_node


If you prefer to simulate the robot using the velocity controller, use this command instead:

    $ros2 launch iiwa_bringup iiwa.launch.py command_interface:="velocity" robot_controller:="velocity_controller"

In another terminal, use the following command to run the node:

    $ros2 run ros2_kdl_package ros2_kdl_node --ros-args -p cmd_interface:=velocity
    

If you prefer to simulate the robot using the effort controller, use this command instead:

    $ros2 launch iiwa_bringup iiwa.launch.py command_interface:="effort" robot_controller:="effort_controller"

In another terminal, use the following command to run the node:
    
    $ros2 run ros2_kdl_package ros2_kdl_node --ros-args -p cmd_interface:=effort

To rewatch the records, unzip the ROSBAG_PUNTO_3 folder and write on a terminal this command:

    $ros2 launch iiwa_bringup iiwa.launch.py command_interface:="effort" robot_controller:="effort_controller"

and on another terminal, write:

    $cd src/Homework2/ROSBAG_PUNTO_3/<bag_directory_name>

    $ros2 bag play <bag_file_name>

To rewatch the records in the folder ROSBAG_PUNTO_4, substitute the "ROSBAG_PUNTO_3" in the previous command with "ROSBAG_PUNTO_4"



##Pay attention

To select the desired trajectory among the 4 possible options, go to the kdl_planner.h file and set the choice_traj variable as follows:

    1 for a circular trajectory with a cubic polynomial curvilinear abscissa.
    
    2 for a linear trajectory with a cubic polynomial curvilinear abscissa.
    
    3 for a circular trajectory with trapezoidal velocity on the curvilinear abscissa.
    
    4 for a linear trajectory with a curvilinear abscissa.


In the same file you can also choose what kind of inverse dynamics control to use by changing the value of the variable choice_dyn:

    1 if you want to use the inverse dynamics in joints space.

    2 if you want to use the inverse dynamics in operational space.



