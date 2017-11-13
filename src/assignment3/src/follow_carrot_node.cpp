/*
 * Follow-the-carrot node
 */

#include <ros/ros.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/Pose.h>
#include <geometry_msgs/PoseWithCovariance.h>
#include <nav_msgs/Odometry.h>
#include <math.h>
#include <tf/transform_datatypes.h>
#include <angles/angles.h>
#include <nav_msgs/Path.h>
#include "pid.h"
#include <turtlesim/Spawn.h>
#include <tf/transform_listener.h>

const double lookAhead = 2.0;
ros::Publisher g_velPublisher;
geometry_msgs::Pose g_currentPose;
std::vector<geometry_msgs::PoseStamped, std::allocator<geometry_msgs::PoseStamped>> g_goals;
int g_goalCount = 0;

double rad2deg(double rad) {
    return (rad*(180/M_PI));
}

double deg2rad(double deg) {
    return (deg*M_PI/180);
}

double getAngleBetweenPoses2D(geometry_msgs::Pose p1, geometry_msgs::Pose p2) {
    float dx = p2.position.x - p1.position.x;
    float dy = p2.position.y - p1.position.y;
    return (atan2(dy, dx));
}

double getDistBetweenPoses2D(geometry_msgs::Pose p1, geometry_msgs::Pose p2) {
    float dx = p2.position.x - p1.position.x;
	float dy = p2.position.y - p1.position.y;
    return (sqrt(dx*dx+dy*dy));
}

void updatePose(const tf::StampedTransform &transform) {
	g_currentPose.position.x = transform.getOrigin().x();
	g_currentPose.position.y = transform.getOrigin().y();
	g_currentPose.position.z = transform.getOrigin().z();
	tf::quaternionTFToMsg(transform.getRotation(), g_currentPose.orientation);
}

geometry_msgs::Pose make2DPose(double x, double y) {
	geometry_msgs::Pose p;
	p.position.x = x;
	p.position.y = y;
	p.position.z = 0;
	return p;
}

std::vector<geometry_msgs::Pose> getCircleIntersectionsByY(geometry_msgs::Pose waypoint, geometry_msgs::Pose nextWaypoint, geometry_msgs::Pose robot, double distance) {
	// zie papier voor uitwerking
	double rpx = robot.position.x;
	double rpy = robot.position.y;

	double wx1 = waypoint.position.x;
	double wy1 = waypoint.position.y;

	double wx2 = nextWaypoint.position.x;
	double wy2 = nextWaypoint.position.y;

	
	double a = 1;	
	double b = -2*rpy;
	double c = rpy*rpy + wx1*wx1 - 2*rpx*wx1 + rpx*rpx - distance*distance;
	double D = (b*b - 4*a*c);

	if (D < 0.0) {
		return std::vector<geometry_msgs::Pose>();
	}
	
	double y1 = (-b + sqrt(D))/(2*a);
	double y2 = (-b - sqrt(D))/(2*a);

	if (D == 0.0) {
		std::vector<geometry_msgs::Pose> points {
			make2DPose(wx1, y1)
		};
		return points;
	}

	std::vector<geometry_msgs::Pose> points {
		make2DPose(wx1, y1),
		make2DPose(wx1, y2)
	};

	return points;
}

std::vector<geometry_msgs::Pose> getCircleIntersectionsByX(geometry_msgs::Pose waypoint, geometry_msgs::Pose nextWaypoint, geometry_msgs::Pose robot, double distance) {
	// zie bord voor uitwerking
	double rpx = robot.position.x;
	double rpy = robot.position.y;

	double wx1 = waypoint.position.x;
	double wy1 = waypoint.position.y;

	double wx2 = nextWaypoint.position.x;
	double wy2 = nextWaypoint.position.y;

	double dx = wx2 - wx1;
	double dy = wy2 - wy1;
	
	double a_l = dx == 0.0 ? 0.0 : dy/dx;
	double b_l = wy1 - a_l * wx1;
		
	double a = a_l*a_l + 1;	
	double b = -2*rpx + 2*a_l*(b_l-rpy);
	double c = rpx*rpx + (b_l - rpy)*(b_l - rpy) - distance*distance;
	double D = (b*b - 4*a*c);

	if (D < 0.0) {
		return std::vector<geometry_msgs::Pose>();
	}
	
	double x1 = (-b + sqrt(D))/(2*a);
	double x2 = (-b - sqrt(D))/(2*a);
	
	double y1 = a_l * x1 + b_l;
	double y2 = a_l * x2 + b_l;

	if (D == 0.0) {
		std::vector<geometry_msgs::Pose> points {
			make2DPose(x1, y1)
		};
		return points;
	}

	std::vector<geometry_msgs::Pose> points {
		make2DPose(x1, y1),
		make2DPose(x2, y2)
	};
	
	return points;
}
std::vector<geometry_msgs::Pose> getCircleIntersections(geometry_msgs::Pose waypoint, geometry_msgs::Pose nextWaypoint, geometry_msgs::Pose robot, double distance) {
	std::vector<geometry_msgs::Pose> intersections;

	ROS_INFO("Path x1: %f, x2: %f", waypoint.position.x, nextWaypoint.position.x);

	double diff = waypoint.position.x - nextWaypoint.position.x;
	if(fabs(diff) < 0.01) {
		intersections = getCircleIntersectionsByY(waypoint, nextWaypoint, robot, distance);
	} else {
		intersections = getCircleIntersectionsByX(waypoint, nextWaypoint, robot, distance);
	}
	return intersections;
}

void cbPath(const nav_msgs::Path::ConstPtr &msg) {
	g_goals = msg->poses;
	ROS_INFO("Path sz: %d", (int)g_goals.size());
	for (auto goal : g_goals) {
		//ROS_INFO("x: %lf, y: %lf", goal.pose.position.x, goal.pose.position.y);		
		geometry_msgs::PoseStamped::ConstPtr goalPtr(new geometry_msgs::PoseStamped(goal));
	}
	//getCircleIntersections(g_goals[0].pose, g_goals[1].pose, g_currentPose, lookAhead);
}

// there are only two points at most in points;
geometry_msgs::Pose getClosest(geometry_msgs::Pose goal, std::vector<geometry_msgs::Pose> points) {
	double closestDistance = 1337;
	geometry_msgs::Pose closestPose;
	for (auto point : points) {
		double dist = getDistBetweenPoses2D(goal, point);
		if (dist < closestDistance) {
			closestPose = point;
			closestDistance = dist;
		}
	}
	return closestPose;
}

void moveToNextPosition() {
	auto intersectionsToNextPoint = getCircleIntersections(g_goals[1].pose, g_goals[2].pose, g_currentPose, lookAhead);

	ROS_INFO("inttopoint: %d", (int)intersectionsToNextPoint.size());

	if(intersectionsToNextPoint.size() > 0) {
		g_goals.erase(g_goals.begin() + 1);
	}

	auto intersections = getCircleIntersections(g_goals[0].pose, g_goals[1].pose, g_currentPose, lookAhead);

	ROS_INFO("int: %d", (int)intersections.size());

	if (intersections.size() == 0) {
		// recalculate with bigger radius?
		double wx1 = g_goals[0].pose.position.x;
		double wy1 = g_goals[0].pose.position.y;
	
		double wx2 = g_goals[1].pose.position.x;
		double wy2 = g_goals[1].pose.position.y;
	
		double dx = wx2 - wx1;
		double dy = wy2 - wy1;
		double a1 = dx == 0.0 ? 0.0 : dy/dx;
		double b1 = wy1 - a1 * wx1;

		// perpendicular
		double a2 = -1/(a1);
		double b2 = g_currentPose.position.y - a2 * g_currentPose.position.x;
		
		double x = (b1-b2)/(a2-a1);
		double y = a2*x+b2;
		// point
		double newDist = getDistBetweenPoses2D(make2DPose(x, y), g_currentPose);
		intersections = getCircleIntersections(g_goals[0].pose, g_goals[1].pose, g_currentPose, newDist);
	}
	
	geometry_msgs::Pose closestToGoal = getClosest(g_goals[0].pose, intersections);
	

	// robot move to closestToGoal
	geometry_msgs::Twist vel_msg;
	vel_msg.linear.x = fmin(getDistBetweenPoses2D(g_currentPose, closestToGoal), 5.0);
	vel_msg.angular.z = fmin(getAngleBetweenPoses2D(g_currentPose, closestToGoal), 3.14);
	ROS_INFO("spd: %lf ang: %lf", vel_msg.linear.x, vel_msg.angular.z);
	g_velPublisher.publish(vel_msg);	
}

int main(int argc, char **argv) {
	ros::init(argc, argv, "follow_carrot_node");
	ros::NodeHandle n;
	
	ros::Subscriber subPath = n.subscribe("/plan", 100, cbPath);
	g_velPublisher = n.advertise<geometry_msgs::Twist>("/cmd_vel", 100);
	ros::Rate rate(100.0);
	tf::TransformListener listener;
	
	while (n.ok()) {
		tf::StampedTransform transform;
		try {
			listener.lookupTransform("/odom", "/base_link", ros::Time(0), transform);
			updatePose(transform);
			if (g_goals.size() > 0) {
				moveToNextPosition();			
			}
		}
		catch (tf::TransformException ex){
		  ROS_ERROR("%s",ex.what());
		  ros::Duration(1.0).sleep();
		}
		ros::spinOnce();
		rate.sleep();
	}
	ROS_INFO("houdoe");
	//ros::spin();
	return 0;
}
