// Copyright  (C)  2007  Francois Cauwe <francois at cauwe dot org>
 
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
 
#include <stdio.h>
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <memory>

#include "std_msgs/msg/float64_multi_array.hpp"
#include "sensor_msgs/msg/joint_state.hpp"

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/wait_for_message.hpp"

#include "kdl_robot.h"
#include "kdl_control.h"
#include "kdl_planner.h"
#include "kdl_parser/kdl_parser.hpp"
 
using namespace KDL;
using FloatArray = std_msgs::msg::Float64MultiArray;
using namespace std::chrono_literals;

class Iiwa_pub_sub : public rclcpp::Node
{
    public:
        Iiwa_pub_sub()
        : Node("ros2_kdl_node"), 
        node_handle_(std::shared_ptr<Iiwa_pub_sub>(this))
        {
            // declare cmd_interface parameter (position, velocity)
            declare_parameter("cmd_interface", "position"); // defaults to "position"
            get_parameter("cmd_interface", cmd_interface_);
            RCLCPP_INFO(get_logger(),"Current cmd interface is: '%s'", cmd_interface_.c_str());

            if (!(cmd_interface_ == "position" || cmd_interface_ == "velocity" || cmd_interface_ == "effort"))
            {
                RCLCPP_INFO(get_logger(),"Selected cmd interface is not valid!"); return;
            }

            iteration_ = 0;
            t_ = 0;
            joint_state_available_ = false; 

            // retrieve robot_description param
            auto parameters_client = std::make_shared<rclcpp::SyncParametersClient>(node_handle_, "robot_state_publisher");
            while (!parameters_client->wait_for_service(1s)) {
                if (!rclcpp::ok()) {
                    RCLCPP_ERROR(this->get_logger(), "Interrupted while waiting for the service. Exiting.");
                    rclcpp::shutdown();
                }
                RCLCPP_INFO(this->get_logger(), "service not available, waiting again...");
            }
            auto parameter = parameters_client->get_parameters({"robot_description"});
            
            
            // create KDLrobot structure
            KDL::Tree robot_tree;
            if (!kdl_parser::treeFromString(parameter[0].value_to_string(), robot_tree)){
                std::cout << "Failed to retrieve robot_description param!";
            }
            robot_ = std::make_shared<KDLRobot>(robot_tree);  
            
            // Create joint array
            unsigned int nj = robot_->getNrJnts();
            KDL::JntArray q_min(nj), q_max(nj);
            q_min.data << -2.96,-2.09,-2.96,-2.09,-2.96,-2.09,-2.96; //-2*M_PI,-2*M_PI; // TODO: read from urdf file
            q_max.data <<  2.96,2.09,2.96,2.09,2.96,2.09,2.96; //2*M_PI, 2*M_PI; // TODO: read from urdf file          
            robot_->setJntLimits(q_min,q_max);            
            joint_positions_.resize(nj); 
            joint_velocities_.resize(nj); 
            joint_efforts_.resize(nj); 
            joint_pos.resize(nj);
            joint_vel.resize(nj);
    
            // Subscriber to jnt states
            jointSubscriber_ = this->create_subscription<sensor_msgs::msg::JointState>(
                "/joint_states", 10, std::bind(&Iiwa_pub_sub::joint_state_subscriber, this, std::placeholders::_1));

            // Wait for the joint_state topic
            while(!joint_state_available_){
                RCLCPP_INFO(this->get_logger(), "No data received yet! ...");
                rclcpp::spin_some(node_handle_);
            }

            // Update KDLrobot object
            robot_->update(toStdVector(joint_positions_.data),toStdVector(joint_velocities_.data), toStdVector(joint_efforts_.data));
            KDL::Frame f_T_ee = KDL::Frame::Identity();
            robot_->addEE(f_T_ee);
            robot_->update(toStdVector(joint_positions_.data),toStdVector(joint_velocities_.data), toStdVector(joint_efforts_.data));
            joint_pos=joint_positions_; 
            joint_vel=joint_velocities_;
            

            // Compute EE frame
            init_cart_pose_ = robot_->getEEFrame();
            // std::cout << "The initial EE pose is: " << std::endl;  
            // std::cout << init_cart_pose_ <<std::endl;

            // Compute IK
            KDL::JntArray q(nj);
            robot_->getInverseKinematics(init_cart_pose_, q);
            // std::cout << "The inverse kinematics returned: " <<std::endl; 
            // std::cout << q.data <<std::endl;

            // Initialize controller
            //KDLController controller_(*robot_);
            controller_ = std::make_shared<KDLController>(*robot_);

            // EE's trajectory initial position (just an offset)
            Eigen::Vector3d init_position(Eigen::Vector3d(init_cart_pose_.p.data) - Eigen::Vector3d(0,0,0.1));

            // EE's trajectory end position (just opposite y)
            Eigen::Vector3d end_position; end_position << init_position[0], -init_position[1], init_position[2];

            // Plan trajectory
            double traj_duration =1.5, acc_duration = 0.5, t = 0.0 , r= 0.1;  //r=0.25 
            
            
            if(choice_traj ==1 ){
              planner_= KDLPlanner(traj_duration, init_position , r , acc_duration);
            }
            else if(choice_traj ==2){
              planner_ = KDLPlanner(traj_duration, acc_duration, init_position, end_position);
            }
            else if(choice_traj ==3){
              acc_duration = 0.2;
              planner_= KDLPlanner(traj_duration, init_position , r , acc_duration);
            }
            else if(choice_traj==4){
              acc_duration = 0.2;
              planner_ = KDLPlanner(traj_duration, acc_duration, init_position, end_position);
            }
            else{
              planner_ = KDLPlanner(traj_duration, acc_duration, init_position, end_position); // currently using trapezoidal velocity profile
            }
                     
            
            // Retrieve the first trajectory point
            p = planner_.compute_trajectory(t);

            // compute errors
            Eigen::Vector3d error = computeLinearError(p.pos, Eigen::Vector3d(init_cart_pose_.p.data));
            //std::cout << "The initial error is : " << error << std::endl;
            
            if(cmd_interface_ == "position"){
                // Create cmd publisher
                cmdPublisher_ = this->create_publisher<FloatArray>("/iiwa_arm_controller/commands", 10);
                timer_ = this->create_wall_timer(std::chrono::milliseconds(100), 
                                            std::bind(&Iiwa_pub_sub::cmd_publisher, this));
            
                // Send joint position commands
                for (long int i = 0; i < joint_positions_.data.size(); ++i) {
                    desired_commands_[i] = joint_positions_(i);
                }
            }
            else if(cmd_interface_ == "velocity"){
                // Create cmd publisher
                cmdPublisher_ = this->create_publisher<FloatArray>("/velocity_controller/commands", 10);
                timer_ = this->create_wall_timer(std::chrono::milliseconds(100), 
                                            std::bind(&Iiwa_pub_sub::cmd_publisher, this));
            
                // Send joint velocity commands
                for (long int i = 0; i < joint_velocities_.data.size(); ++i) {
                    desired_commands_[i] = joint_velocities_(i);
                }
            }
            else{
                // Create cmd publisher
                cmdPublisher_ = this->create_publisher<FloatArray>("/effort_controller/commands", 10);
                timer_ = this->create_wall_timer(std::chrono::milliseconds(20), 
                                            std::bind(&Iiwa_pub_sub::cmd_publisher, this)); 
                                            
                 // Send joint effort commands
                for (long int i = 0; i < joint_efforts_.data.size(); ++i) {
                    desired_commands_[i] = joint_efforts_(i);
                }
            }

            // Create msg and publish
            std_msgs::msg::Float64MultiArray cmd_msg;
            cmd_msg.data = desired_commands_;
            cmdPublisher_->publish(cmd_msg);

            RCLCPP_INFO(this->get_logger(), "Starting trajectory execution ...");
        }

    private:
    
        void cmd_publisher(){

            iteration_ = iteration_ + 1;

            // define trajectory
            double total_time = 1.5, total_time2=10; // 
            int trajectory_len = 150; // 
            int loop_rate = trajectory_len / total_time;
            double dt = 1.0 / loop_rate;
            t_+=dt;


            if (t_ < total_time2){

                // Set endpoint twist
                // double t = iteration_;
                // joint_velocities_.data[2] = 2 * 0.3 * cos(2 * M_PI * t / trajectory_len);
                // joint_velocities_.data[3] = -0.3 * sin(2 * M_PI * t / trajectory_len);

                // Integrate joint velocities
                // joint_positions_.data += joint_velocities_.data * dt;

                // Retrieve the trajectory point
                if(t_<total_time)
                	p = planner_.compute_trajectory(t_);
                else{
                   p=p;        
                }
                	
                // Compute EE frame
                KDL::Frame cartpos = robot_->getEEFrame();           

                // Compute desired Frame
                KDL::Frame desFrame; desFrame.M = cartpos.M; desFrame.p = toKDL(p.pos); 

                // compute errors
                Eigen::Vector3d error = computeLinearError(p.pos, Eigen::Vector3d(cartpos.p.data));
                Eigen::Vector3d o_error = computeOrientationError(toEigen(init_cart_pose_.M), toEigen(cartpos.M));
                std::cout << "The error norm is : " << error.norm() << std::endl;
                

                if(cmd_interface_ == "position"){
                    // Next Frame
                    KDL::Frame nextFrame; nextFrame.M = cartpos.M; nextFrame.p = cartpos.p + (toKDL(p.vel) + toKDL(1*error))*dt; 

                    // Compute IK
                    robot_->getInverseKinematics(nextFrame, joint_positions_);
                }
                else if(cmd_interface_ == "velocity"){

                    // Compute differential IK
                    Vector6d cartvel; cartvel << p.vel + 5*error, o_error;
                    joint_velocities_.data = pseudoinverse(robot_->getEEJacobian().data)*cartvel;
                    joint_positions_.data = joint_positions_.data + joint_velocities_.data*dt;
                }
                else{
                    unsigned int nj = robot_->getNrJnts();
                    //torque
                    Vector6d des_vel; 
                    KDL::JntArray ddqd , velocity_app;
                    ddqd.resize(nj);
                    velocity_app.resize(nj);

                    double Kp_=50, Kd_=20;
                    double Kpp_=50, Kdp_=20, Kpo_= 10 , Kdo_=1;
                   
                    
                    KDL::Frame desPos;
                    KDL::Twist desVel, desAcc;
                    
	            
	            if(choice_dyn == 2){
	            	desPos.p= toKDL(p.pos);
                        desPos.M = init_cart_pose_.M;
                   
                        desVel.vel = toKDL(p.vel);
                        desVel.rot = {0.0 , 0.0 , 0.0 };

                        desAcc.vel = toKDL(p.acc);
                        desAcc.rot = {0.0 , 0.0 , 0.0 };
                   
                    
                        joint_efforts_.data= controller_->idCntr(desPos, desVel,desAcc, Kpp_,Kpo_,Kdp_,Kdo_);         
                         //std::cout << "forza : " << joint_efforts_.data << std::endl;
	            
	            }
	            else{
	            	
	            	des_vel << p.vel + 5*error, o_error;
	                velocity_app= joint_vel;
                        joint_vel.data = pseudoinverse(robot_->getEEJacobian().data)*des_vel;
                    
                        joint_pos.data = joint_pos.data + joint_vel.data*dt;
                        ddqd.data=(joint_vel.data-velocity_app.data)/dt;
                                       
                    
                        joint_efforts_.data= controller_->idCntr(joint_pos, joint_vel, ddqd , Kp_ , Kd_);
                    	            
	            }

                    
                    
                }

                // Update KDLrobot structure
                robot_->update(toStdVector(joint_positions_.data),toStdVector(joint_velocities_.data), toStdVector(joint_efforts_.data));

                if(cmd_interface_ == "position"){
                    // Send joint position commands
                    for (long int i = 0; i < joint_positions_.data.size(); ++i) {
                        desired_commands_[i] = joint_positions_(i);
                    }
                }
                else if(cmd_interface_ == "velocity"){
                    // Send joint velocity commands
                    for (long int i = 0; i < joint_velocities_.data.size(); ++i) {
                        desired_commands_[i] = joint_velocities_(i);
                    }
                }
                else{
                    // Send joint effort commands
                    //std::cout<< "Data size "<< joint_efforts_.data.size() << std::endl;
                    for (long int i = 0; i < joint_efforts_.data.size(); ++i) {
                        desired_commands_[i] = joint_efforts_(i);
                    }
                }

                // Create msg and publish
                std_msgs::msg::Float64MultiArray cmd_msg;
                cmd_msg.data = desired_commands_;
                cmdPublisher_->publish(cmd_msg);

                // std::cout << "/////////////////////////////////////////////////" <<std::endl <<std::endl;
                // std::cout << "EE pose is: " << robot_->getEEFrame() <<std::endl;  
                // std::cout << "Jacobian: " << robot_->getEEJacobian().data <<std::endl;
                // std::cout << "joint_positions_: " << joint_positions_.data <<std::endl;
                // std::cout << "joint_velocities_: " << joint_velocities_.data <<std::endl;
                // std::cout << "iteration_: " << iteration_ <<std::endl <<std::endl;
                // std::cout << "/////////////////////////////////////////////////" <<std::endl <<std::endl;

               
            }
            else{
                RCLCPP_INFO_ONCE(this->get_logger(), "Trajectory executed successfully ...");
                // Send joint velocity commands
                //for (long int i = 0; i < joint_velocities_.data.size(); ++i) {
                //    desired_commands_[i] = 0.0;
                //}
                
                // Create msg and publish
                std_msgs::msg::Float64MultiArray cmd_msg;
                cmd_msg.data = desired_commands_;
                cmdPublisher_->publish(cmd_msg);
            }
        }

        void joint_state_subscriber(const sensor_msgs::msg::JointState& sensor_msg){

            //for (size_t i = 0; i < sensor_msg.effort.size(); ++i) {
            //     RCLCPP_INFO(this->get_logger(), "Positions %zu: %f", i, sensor_msg.position[i]);                
            // }
            // std::cout<<"\n";
            //for (size_t i = 0; i < sensor_msg.effort.size(); ++i) {
            //     RCLCPP_INFO(this->get_logger(), "Velocities %zu: %f", i, sensor_msg.velocity[i]);
            // }
            // std::cout<<"\n";
            // for (size_t i = 0; i < sensor_msg.effort.size(); ++i) {
            //     RCLCPP_INFO(this->get_logger(), "Efforts %zu: %f", i, sensor_msg.effort[i]);
            // }

            joint_state_available_ = true;
            for (unsigned int i  = 0; i < sensor_msg.position.size(); i++){
                joint_positions_.data[i] = sensor_msg.position[i];
                joint_velocities_.data[i] = sensor_msg.velocity[i];
                joint_efforts_.data[i] = sensor_msg.effort[i];
            }
        }

        rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr jointSubscriber_;
        rclcpp::Publisher<FloatArray>::SharedPtr cmdPublisher_;
        rclcpp::TimerBase::SharedPtr timer_; 
        rclcpp::TimerBase::SharedPtr subTimer_;
        rclcpp::Node::SharedPtr node_handle_;

        std::vector<double> desired_commands_ = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        KDL::JntArray joint_positions_;
        KDL::JntArray joint_velocities_;
        KDL::JntArray joint_efforts_;
        KDL::JntArray joint_pos;
        KDL::JntArray joint_vel;
        trajectory_point p;
       
                
        std::shared_ptr<KDLRobot> robot_;
        std::shared_ptr<KDLController> controller_;
        KDLPlanner planner_;

        int iteration_;
        bool joint_state_available_;
        double t_;
        std::string cmd_interface_;
        KDL::Frame init_cart_pose_;
};

 
int main( int argc, char** argv )
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<Iiwa_pub_sub>());
    rclcpp::shutdown();
    return 1;
}
