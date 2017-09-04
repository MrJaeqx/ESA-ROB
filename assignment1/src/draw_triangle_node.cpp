#include "ros/ros.h"
#include "std_msgs/String.h"

void cmdCallback(const std_msgs::String::ConstPtr& msg) {
	
	ROS_INFO("I heard: [%s]", msg->data.c_str());
}

int main(int argc, char **argv) {
	ros::init(argc, argv, "listener");
	ros::NodeHandle n;
	ros::Subscriber sub = n.subscribe("cmd", 1000, cmdCallback);
	ros::spin();
	return 0;
}
