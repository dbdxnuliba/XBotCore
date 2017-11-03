/*
 * Copyright (C) 2016 IIT-ADVR
 * Author: Arturo Laurenzi, Luca Muratore, Giuseppe Rigano
 * email:  arturo.laurenzi@iit.it, luca.muratore@iit.it, giuseppe.rigano@iit.it
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>
*/


#include <XCM/CommunicationInterfaceROS.h>
#include <sensor_msgs/Imu.h>
#include <geometry_msgs/WrenchStamped.h>

#include <XCM/XBotUtils.h>

extern "C" XBot::CommunicationInterfaceROS* create_instance(XBot::RobotInterface::Ptr robot, XBot::XBotXDDP::Ptr xddp_handler )
{
  return new XBot::CommunicationInterfaceROS(robot, xddp_handler);
}

extern "C" void destroy_instance( XBot::CommunicationInterfaceROS* instance )
{
  delete instance;
}

namespace XBot {

bool CommunicationInterfaceROS::callback(std_srvs::SetBoolRequest& req,
                                         std_srvs::SetBoolResponse& res,
                                         const std::string& port_name)
{
    _msgs.at(port_name) = req.data ? "start" : "stop";
    res.success = true;
    return true;
}

bool XBot::CommunicationInterfaceROS::callback_cmd(XCM::cmd_serviceRequest& req,
                                                   XCM::cmd_serviceResponse& res,
                                                   const std::string& port_name)
{
    _msgs.at(port_name) = req.cmd;
    res.success = true;
    return true;
}

bool XBot::CommunicationInterfaceROS::callback_master_communication_iface(XCM::cmd_serviceRequest& req,
                                                                          XCM::cmd_serviceResponse& res,
                                                                          const std::string& port_name)
{
    _msgs.at(port_name) = req.cmd;
    res.success = true;
    return true;
}

bool XBot::CommunicationInterfaceROS::callback_hand(const std_msgs::Float64::ConstPtr& msg, int hand_id)
{
  
    _hand_value_map[hand_id] = msg->data;
    return true;
}

CommunicationInterfaceROS::CommunicationInterfaceROS():
    CommunicationInterface()
{
    int argc = 1;
    const char *arg = "dummy_arg";
    char* argg = const_cast<char*>(arg);
    char** argv = &argg;

    if(!ros::isInitialized()){
        ros::init(argc, argv, "ros_communication_interface_" + std::to_string(XBot::get_time_ns()));
    }

    _nh = std::make_shared<ros::NodeHandle>();
}

CommunicationInterfaceROS::CommunicationInterfaceROS(XBotInterface::Ptr robot, XBot::XBotXDDP::Ptr xddp_handler):
    CommunicationInterface(robot, xddp_handler),
    _path_to_cfg(robot->getPathToConfig())
{
    int argc = 1;
    const char *arg = "dummy_arg";
    char* argg = const_cast<char*>(arg);
    char** argv = &argg;

    if(!ros::isInitialized()){
        ros::init(argc, argv, "ros_communication_interface_" + std::to_string(XBot::get_time_ns()));
    }

    _nh = std::make_shared<ros::NodeHandle>();

    load_robot_state_publisher();

    load_ros_message_interfaces();

    for(const auto& pair : robot->getImu()){
        XBot::ImuSensor::ConstPtr imuptr = pair.second;
        std::string imu_topic_name;
        imu_topic_name = "/xbotcore/" + robot->getUrdf().name_ + "/imu/" + imuptr->getSensorName();
        _imu_pub_map[imuptr->getSensorId()] = _nh->advertise<sensor_msgs::Imu>(imu_topic_name, 1);
    }

    for(const auto& pair : robot->getForceTorque()){
        XBot::ForceTorqueSensor::ConstPtr ftptr = pair.second;
        std::string ft_topic_name;
        ft_topic_name = "/xbotcore/" + robot->getUrdf().name_ + "/ft/" + ftptr->getSensorName();
        _ft_pub_map[ftptr->getSensorId()] = _nh->advertise<geometry_msgs::WrenchStamped>(ft_topic_name, 1);
    }
    
    for(const auto& pair : robot->getHand()){
        XBot::Hand::Ptr hptr = pair.second;
        std::string h_topic_name;
        h_topic_name = "/xbotcore/" + robot->getUrdf().name_ + "/hand/" + hptr->getHandName();
        _hand_pub_map[hptr->getHandId()] = _nh->advertise<std_msgs::Float64>(h_topic_name+"/state", 1);
        _hand_sub_map[hptr->getHandId()] = _nh->subscribe<std_msgs::Float64>(h_topic_name+"/command", 1,boost::bind(&XBot::CommunicationInterfaceROS::callback_hand,this,_1,hptr->getHandId()));
        _hand_value_map[hptr->getHandId()] = 0.0;
    }
}

void CommunicationInterfaceROS::load_robot_state_publisher()
{
    KDL::Tree kdl_tree;
    kdl_parser::treeFromUrdfModel(_robot->getUrdf(), kdl_tree);

    _robot_state_pub = std::make_shared<robot_state_publisher::RobotStatePublisher>(kdl_tree);

    _urdf_param_name = "/xbotcore/" + _robot->getUrdf().getName() + "/robot_description";
    _tf_prefix = "/xbotcore/" + _robot->getUrdf().getName();
    _nh->setParam(_urdf_param_name, _robot->getUrdfString());

}


void CommunicationInterfaceROS::load_ros_message_interfaces() {

    YAML::Node root_cfg = YAML::LoadFile(_path_to_cfg);
    // TBD check if they exist
    const YAML::Node &ros_interface_root = root_cfg["RobotInterfaceROS"];
    _control_message_type = ros_interface_root["control_message_type"].as<std::string>();
    _jointstate_message_type = ros_interface_root["jointstate_message_type"].as<std::string>();

    const YAML::Node &ctrl_msg_root = root_cfg[_control_message_type];
    _control_message_factory_name = ctrl_msg_root["subclass_factory_name"].as<std::string>();
    _control_message_class_name = ctrl_msg_root["subclass_name"].as<std::string>();
    _control_message_path_to_so = ctrl_msg_root["path_to_shared_lib"].as<std::string>();

    computeAbsolutePath(_control_message_path_to_so,
                        LIB_MIDDLE_PATH,
                        _control_message_path_to_so
                       );

    // Loading the requested control message
    _controlmsg_factory.open( _control_message_path_to_so.c_str(),
                              _control_message_factory_name.c_str());
    if (!_controlmsg_factory.isValid()) {
        // NOTE print to celebrate the wizard
        printf("error (%s) : %s\n", shlibpp::Vocab::decode(_controlmsg_factory.getStatus()).c_str(),
               _controlmsg_factory.getLastNativeError().c_str());
    }
    // open and init control message
    _controlmsg_instance.open(_controlmsg_factory);
    _receive_commands_ok = _controlmsg_instance->init(_path_to_cfg, GenericControlMessage::Type::Rx);
    if(_receive_commands_ok){
        std::cout << "Receive commands from ROS ok!" << std::endl;
        _receive_commands_ok = true;
    }
    // save pointer to the control message
    _control_message = &_controlmsg_instance.getContent();

    const YAML::Node &jointstate_msg_root = root_cfg[_jointstate_message_type];
    _jointstate_message_factory_name = jointstate_msg_root["subclass_factory_name"].as<std::string>();
    _jointstate_message_class_name = jointstate_msg_root["subclass_name"].as<std::string>();
    _jointstate_message_path_to_so = jointstate_msg_root["path_to_shared_lib"].as<std::string>();

    computeAbsolutePath(_jointstate_message_path_to_so,
                        LIB_MIDDLE_PATH,
                        _jointstate_message_path_to_so
                       );

    // Loading the requested jointstate message
    _jointstatemsg_factory.open( _jointstate_message_path_to_so.c_str(),
                              _jointstate_message_factory_name.c_str());
    if (!_jointstatemsg_factory.isValid()) {
        // NOTE print to celebrate the wizard
        printf("error (%s) : %s\n", shlibpp::Vocab::decode(_jointstatemsg_factory.getStatus()).c_str(),
               _jointstatemsg_factory.getLastNativeError().c_str());
    }
    // open and init jointstate message
    _jointstatemsg_instance.open(_jointstatemsg_factory);
    _send_robot_state_ok = _jointstatemsg_instance->init(_path_to_cfg, GenericJointStateMessage::Type::Tx);
    if(_send_robot_state_ok){
        std::cout << "Send robot state over ROS ok!" << std::endl;
        _send_robot_state_ok = true;
    }
    // save pointer to the jointstate message
    _jointstate_message = &_jointstatemsg_instance.getContent();

    /* Fill maps joint_id -> message indices */

    for( const std::string& joint_name : _robot->getEnabledJointNames() ){
        int id = _robot->getJointByName(joint_name)->getJointId();
        _jointid_to_jointstate_msg_idx[id] = _jointstate_message->getIndex(joint_name);
        _jointid_to_command_msg_idx[id] = _control_message->getIndex(joint_name);;
    }
    
    /* check the flag to publish or not the /tf */
    if(ros_interface_root["publish_tf"]) { 
        _publish_tf = ros_interface_root["publish_tf"].as<bool>();
    }
    else {
        std::cout << "WARNING in " << __func__ << "! " << ros_interface_root << " node does NOT contain optional node publish_tf. I will assume it to TRUE" << std::endl;
    }

}
void CommunicationInterfaceROS::sendRobotState()
{

    /* TF */

    _robot->getJointPosition(_joint_name_map);
    std::map<std::string, double> _joint_name_std_map(_joint_name_map.begin(), _joint_name_map.end());

    if(_publish_tf){
        _robot_state_pub->publishTransforms(_joint_name_std_map, ros::Time::now(), "");
        _robot_state_pub->publishFixedTransforms("");
    }

    /* Joint states */

    if( !_send_robot_state_ok ) return;
    
    for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
        double fault_value;
        _xddp_handler->get_fault(id, fault_value);
        _jointstate_message->fault(joint_state_msg_idx) = fault_value;
    }

    _robot->getJointPosition(_joint_id_map);

    for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
        _jointstate_message->linkPosition(joint_state_msg_idx) = _joint_id_map.at(id);
    }

    _robot->getMotorPosition(_joint_id_map);

    for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
        _jointstate_message->motorPosition(joint_state_msg_idx) = _joint_id_map.at(id);
    }

    _robot->getJointVelocity(_joint_id_map);

    for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
       _jointstate_message->linkVelocity(joint_state_msg_idx) = _joint_id_map.at(id);
    }

    _robot->getMotorVelocity(_joint_id_map);

    for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
        _jointstate_message->motorVelocity(joint_state_msg_idx) = _joint_id_map.at(id);
    }

    _robot->getJointEffort(_joint_id_map);

    for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
        _jointstate_message->effort(joint_state_msg_idx) = _joint_id_map.at(id);
    }

    _robot->getTemperature(_joint_id_map);

    for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
        _jointstate_message->temperature(joint_state_msg_idx) = _joint_id_map.at(id);
    }

     _robot->getPositionReference(_joint_id_map);
     
     for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
        _jointstate_message->position_reference(joint_state_msg_idx) = _joint_id_map.at(id);
    }

     _robot->getVelocityReference(_joint_id_map);

    for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
        _jointstate_message->velocity_reference(joint_state_msg_idx) = _joint_id_map.at(id);
    }
    
    for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
        _jointstate_message->stiffness(joint_state_msg_idx) = _joint_id_map.at(id);
    }

     _robot->getEffortReference(_joint_id_map);

    for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
        _jointstate_message->effort_reference(joint_state_msg_idx) = _joint_id_map.at(id);
    }
    
    _robot->getStiffness(_joint_id_map);

    for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
        _jointstate_message->stiffness(joint_state_msg_idx) = _joint_id_map.at(id);
    }

     _robot->getDamping(_joint_id_map);

    for( int id : _robot->getEnabledJointId() ){
        int joint_state_msg_idx = _jointid_to_jointstate_msg_idx.at(id);
        _jointstate_message->damping(joint_state_msg_idx) = _joint_id_map.at(id);
    }

    _jointstate_message->publish();

    /* IMU */

    for(const auto& pair : _robot->getImu()){

        XBot::ImuSensor::ConstPtr imuptr = pair.second;

        Eigen::Quaterniond q;
        Eigen::Vector3d w, a;

        imuptr->getImuData(q, a, w);

        sensor_msgs::Imu msg;

        msg.angular_velocity.x = w.x();
        msg.angular_velocity.y = w.y();
        msg.angular_velocity.z = w.z();

        msg.linear_acceleration.x = a.x();
        msg.linear_acceleration.y = a.y();
        msg.linear_acceleration.z = a.z();

        msg.orientation.w = q.w();
        msg.orientation.x = q.x();
        msg.orientation.y = q.y();
        msg.orientation.z = q.z();

        msg.header.stamp = ros::Time::now();
        msg.header.frame_id = pair.first;

        _imu_pub_map.at(imuptr->getSensorId()).publish(msg);

    }


    /* FT */

    for(const auto& pair : _robot->getForceTorque()){

        XBot::ForceTorqueSensor::ConstPtr ftptr = pair.second;

        Eigen::Vector6d w;
        ftptr->getWrench(w);

        geometry_msgs::WrenchStamped msg;

        msg.wrench.force.x = w(0);
        msg.wrench.force.y = w(1);
        msg.wrench.force.z = w(2);
        msg.wrench.torque.x = w(3);
        msg.wrench.torque.y = w(4);
        msg.wrench.torque.z = w(5);

        msg.header.stamp = ros::Time::now();
        msg.header.frame_id = pair.first;

        _ft_pub_map.at(ftptr->getSensorId()).publish(msg);

    }
    
    
     /* HAND */

    for(const auto& pair : _robot->getHand()){

        XBot::Hand::Ptr hptr = pair.second;

        double grasp = hptr->getGraspReference();

        std_msgs::Float64 msg;

        msg.data = grasp;

        _hand_pub_map.at(hptr->getHandId()).publish(msg);

    }
}

void CommunicationInterfaceROS::resetReference()
{
    _robot->getPositionReference(_joint_id_map);
    
    for( const auto& pair : _jointid_to_command_msg_idx ){
         _control_message->position(pair.second) = _joint_id_map[pair.first];
    }

    _robot->getVelocityReference(_joint_id_map);

    for( const auto& pair : _jointid_to_command_msg_idx ){
        _control_message->velocity(pair.second) = _joint_id_map[pair.first];
    }

    _robot->getEffortReference(_joint_id_map);

    for( const auto& pair : _jointid_to_command_msg_idx ){
        _control_message->effort(pair.second) = _joint_id_map[pair.first];
    }

    _robot->getStiffness(_joint_id_map);

    for( const auto& pair : _jointid_to_command_msg_idx ){
        _control_message->stiffness(pair.second) = _joint_id_map[pair.first];
    }

    _robot->getDamping(_joint_id_map);

    for( const auto& pair : _jointid_to_command_msg_idx ){
        _control_message->damping(pair.second) = _joint_id_map[pair.first];
    }

    
}


void CommunicationInterfaceROS::receiveReference()
{
    if( !_receive_commands_ok ) return;

    ros::spinOnce();
    
    if (current_seq_id < _control_message->seq_id()) {
        
        current_seq_id = _control_message->seq_id();

        for( const auto& pair : _jointid_to_command_msg_idx ){
            _joint_id_map[pair.first] = _control_message->position(pair.second);
        }

        _robot->setPositionReference(_joint_id_map);


        for( const auto& pair : _jointid_to_command_msg_idx ){
            _joint_id_map[pair.first] = _control_message->velocity(pair.second);
        }

        _robot->setVelocityReference(_joint_id_map);


        for( const auto& pair : _jointid_to_command_msg_idx ){
            _joint_id_map[pair.first] = _control_message->effort(pair.second);
        }

        _robot->setEffortReference(_joint_id_map);


        for( const auto& pair : _jointid_to_command_msg_idx ){
            _joint_id_map[pair.first] = _control_message->stiffness(pair.second);
        }

        _robot->setStiffness(_joint_id_map);


        for( const auto& pair : _jointid_to_command_msg_idx ){
            _joint_id_map[pair.first] = _control_message->damping(pair.second);
        }

        _robot->setDamping(_joint_id_map);
    }
//     else {
//         resetReference();
//     }
//     
    
    /* HAND */

    for(const auto& pair : _robot->getHand()){

        XBot::Hand::Ptr hptr = pair.second;
        
        double grasp = _hand_value_map[hptr->getHandId()];

        hptr->grasp(grasp);
    }


}

bool CommunicationInterfaceROS::advertiseSwitch(const std::string& port_name)
{
    if( _services.count(port_name) > 0 ){
        return false;
    }

    _services[port_name] = _nh->advertiseService<std_srvs::SetBoolRequest, std_srvs::SetBoolResponse>
                                    (port_name,
                                     boost::bind(&CommunicationInterfaceROS::callback,
                                                 this,
                                                 _1, _2, port_name)
                                     );
    _msgs[port_name] = "";

    std::cout << "Advertised service " << port_name << std::endl;

    return true;
}

void XBot::CommunicationInterfaceROS::advertiseStatus(const std::string& plugin_name)
{
    if( _status_services.count(plugin_name) > 0 ){
        return;
    }

    std::cout << "Advertised status port for plugin " << plugin_name << std::endl;

    _status_services[plugin_name] = _nh->advertiseService<XCM::status_serviceRequest, XCM::status_serviceResponse>
                                                (plugin_name + "_status",
                                                 boost::bind(&CommunicationInterfaceROS::callback_status,
                                                             this,
                                                             _1, _2,
                                                             plugin_name)
                                                );

    _plugin_status_map[plugin_name] = "";

}

bool XBot::CommunicationInterfaceROS::callback_status(XCM::status_serviceRequest& req, XCM::status_serviceResponse& res, const std::string& plugin_name)
{
    res.status = _plugin_status_map.at(plugin_name);
    return true;
}

bool XBot::CommunicationInterfaceROS::setPluginStatus(const std::string& plugin_name, const std::string& status)
{
    auto it = _plugin_status_map.find(plugin_name);
    if( it == _plugin_status_map.end() ){
        return false;
    }

    it->second = status;
    return true;
}



bool XBot::CommunicationInterfaceROS::advertiseCmd(const std::string& port_name)
{
    if( _services.count(port_name) > 0 ){
        return false;
    }

    _services[port_name] = _nh->advertiseService<XCM::cmd_serviceRequest, XCM::cmd_serviceResponse>
                                    (port_name,
                                     boost::bind(&CommunicationInterfaceROS::callback_cmd,
                                                 this,
                                                 _1, _2, port_name)
                                     );
    _msgs[port_name] = "";

    std::cout << "Advertised service " << port_name << std::endl;

    return true;
}


bool XBot::CommunicationInterfaceROS::advertiseMasterCommunicationInterface()
{
    // NOTE default port name for MasterCommunicationInterface

    _services[_master_communication_interface_port] = _nh->advertiseService<XCM::cmd_serviceRequest, XCM::cmd_serviceResponse>
                                        (_master_communication_interface_port,
                                         boost::bind(&CommunicationInterfaceROS::callback_master_communication_iface,
                                                     this,
                                                     _1, _2, _master_communication_interface_port)
                                         );
    _msgs[_master_communication_interface_port] = "";

    std::cout << "Advertised service " << _master_communication_interface_port << std::endl;

    return true;
}



bool CommunicationInterfaceROS::receiveFromSwitch(const std::string& port_name, std::string& message)
{
    ros::spinOnce();

    auto it = _msgs.find(port_name);

    if( it == _msgs.end() ) return false;

    message = it->second;
    if( message != "" ){
        it->second = "";
        return true;
    }
    else {
        return false;
    }
}

bool XBot::CommunicationInterfaceROS::receiveFromCmd(const std::string& port_name, std::string& message)
{
    ros::spinOnce();

    auto it = _msgs.find(port_name);

    if( it == _msgs.end() ) return false;

    message = it->second;
    if( message != "" ){
        it->second = "";
        return true;
    }
    else {
        return false;
    }
}

std::string CommunicationInterfaceROS::getPluginStatus(const std::string& plugin_name)
{
    return _plugin_status_map.at(plugin_name);
}


bool XBot::CommunicationInterfaceROS::receiveMasterCommunicationInterface(std::string& framework_name)
{
    ros::spinOnce();

    auto it = _msgs.find(_master_communication_interface_port);

    if( it == _msgs.end() ) return false;

    framework_name = it->second;
    if( framework_name != "" ) {
        it->second = "";
        return true;
    }
    else {
        return false;
    }
}



bool CommunicationInterfaceROS::computeAbsolutePath (  const std::string& input_path,
                                                 const std::string& middle_path,
                                                 std::string& absolute_path)
{
    // if not an absolute path
    if(!(input_path.at(0) == '/')) {
        // if you are working with the Robotology Superbuild
        const char* env_p = std::getenv("ROBOTOLOGY_ROOT");
        // check the env, otherwise error
        if(env_p) {
            std::string current_path(env_p);
            // default relative path when working with the superbuild
            current_path += middle_path;
            current_path += input_path;
            absolute_path = current_path;
            return true;
        }
        else {
            std::cerr << "ERROR in " << __func__ << " : the input path  " << input_path << " is neither an absolute path nor related with the robotology superbuild. Download it!" << std::endl;
            return false;
        }
    }
    // already an absolute path
    absolute_path = input_path;
    return true;
}




}