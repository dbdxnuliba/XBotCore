# XBotCore

XBotCore is the new software architecture to control ADVR robots: it uses Xenomai API to satisfy Real-Time requirements. 

Moreover it provides XDDP pipes communication with a Not-Real-Time communication API. 

## How to use WALKMAN robot with XBotCore and YARP

## *Current network topology*

### **WALKMAN RT PC**

*hostname*:         walkman-exp

*operating system*: Debian stretch

*local IP address*: 10.24.3.102

*description*:    
The WALKMAN RT PC is responsible for the RT communication with the robot (EtherCAT based).  
It has three ethernet connections:  
* internet connection
* connection with the robot EtherCAT slaves network
* connection with external control pc (YARP control modules)

*what to run*:      

* XBotCore
* yarpserver
* XBotYARP

*how to run them*:

Check the connection between the robot and the WALKMAN RT PC, power on the robot and have always the emergency stop in your hand.  
Open 3 terminal on WALKMAN RT PC:

* On terminal # 1 start XBotCore: it will start the motors using the control mode and the gains specified in the YAML config file passed as an argument.  
~$ **cd $XBOTCORE_ROOT**  
~$ **XBotCore configs/config_walkman.YAML**  

* On terminal # 2 start YARP name server: check that it is bind the the local address (10.24.3.102) 
~$ **yarpserver --write**  

* On terminal # 3 start XBotYARP: it will start the Not-Real-Time YARP communication with XBotCore opening all the YARP ports needed to communicate with the robot using YARP (like in gazebo with the robot model).  
~$ **cd $XBOTCORE_ROOT**  
~$ **XBotYARP configs/config_walkman.YAML**  







            
                    

