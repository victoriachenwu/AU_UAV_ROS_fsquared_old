/* 
|	Collision Avoidance Node
|
|	This node controls the collision avoidance algorithm, 
|	which is currently implementat of reactive inverse PN. 
*/


//standard C++ headers
#include <sstream>
#include <iostream>
#include <stdlib.h>
#include <queue>
#include <map>
#include <cmath>
#include <stdio.h>
#include <time.h>
#include <boost/lexical_cast.hpp>

//ROS headers
#include "ros/ros.h"
#include "AU_UAV_ROS/TelemetryUpdate.h"
#include "AU_UAV_ROS/GoToWaypoint.h"
#include "AU_UAV_ROS/RequestWaypointInfo.h"
#include "AU_UAV_ROS/standardDefs.h"
#include <tf/transform_broadcaster.h>
#include <visualization_msgs/Marker.h>
#include "AU_UAV_ROS/StartCollisionAvoidance.h"

//collision avoidance headers
#include "AU_UAV_ROS/planeObject.h"
#include "AU_UAV_ROS/standardFuncs.h"
#include "AU_UAV_ROS/ripna.h"


#define WEST_MOST_LONGITUDE -85.490356
#define NORTH_MOST_LATITUDE 32.606573

#define METERS_TO_LATITUDE (1.0/111200.0)

#define DEGREES_TO_RADIANS (M_PI/180.0)
#define RADIANS_TO_DEGREES (180.0/M_PI)

//publisher is global so callbacks can access it
ros::Publisher marker_pub;

/* ROS service clients for calling services from the coordinator */
ros::ServiceClient goToWaypointClient;
ros::ServiceClient requestWaypointInfoClient;

bool readyToStart = false;

/* Variables for the number of goToWaypoint services requested, 
the number of planes in the airspace, and the current planes ID */
int count;
std::map<int, AU_UAV_ROS::PlaneObject> planes; /* map of planes in the airspace.  The key is the plane id of the aircraft */

/* This function is run every time new telemetry information from any plane is recieved. With the new telemetry update, 
information about the plane is updated, including bearing, speed, current location, etc. Additionally, we check to see
if the UAV has reached its current destination, and, if so, update the destination of the UAV. After updating, the
calculateForces function is called to find a the new force acting on the UAV; from this new force, a next waypoint is
generated and forwarded to the coordinator. */
void telemetryCallback(const AU_UAV_ROS::TelemetryUpdate::ConstPtr &msg)
{	
	int planeID = msg->planeID;
	/* Instantiate services for use later, and get planeID*/
	AU_UAV_ROS::GoToWaypoint goToWaypointSrv;
	AU_UAV_ROS::GoToWaypoint goToWaypointSrv2;
	AU_UAV_ROS::RequestWaypointInfo requestWaypointInfoSrv;
	

	/* Request this plane's current normal destination */
	requestWaypointInfoSrv.request.planeID = planeID;
	requestWaypointInfoSrv.request.isAvoidanceWaypoint = false;
	requestWaypointInfoSrv.request.positionInQueue = 0;

	if (!requestWaypointInfoClient.call(requestWaypointInfoSrv)){
		ROS_ERROR("Did not receive a response from the coordinator");
		return;
	}

	/* If the plane has reached its current destination, move onto the 
	next destination waypoint. This does not set the destination
	of the plane object in the map "planes." */
	if (findDistance(msg->currentLatitude, msg->currentLongitude, 
					requestWaypointInfoSrv.response.latitude, 
					requestWaypointInfoSrv.response.longitude) < COLLISION_THRESHOLD){

		/* request next normal destination */
		requestWaypointInfoSrv.request.positionInQueue = 1;

		if (!requestWaypointInfoClient.call(requestWaypointInfoSrv)){
			ROS_ERROR("Did not recieve a response from the coordinator");
			return;
		}
	}


	/* If the plane is not in our map of planes and has destination
	waypoints, then add it as a new plane to our map of planes. 
	
	Else if the plane is not in our map of planes and does not
	have waypoints, return and do nothing more. */

	//added this to add robustness for real UAVs (because real UAVs don't return currentWaypointIndex of -1)
	if (planes.find(planeID) == planes.end() && msg->currentWaypointIndex != -1 || (planeID >= 32 && planeID <= 63 && planes.find(planeID) == planes.end())){ 

		ROS_ERROR("NEW PLNE INSERTED BY COL AV.");
		/* This is a new plane, so create a new planeObject and 
		give it the appropriate information */
		AU_UAV_ROS::PlaneObject newPlane(MPS_SPEED, *msg); 
		planes[planeID] = newPlane; /* put the new plane into the map */

		/* Update the destination of the PlaneObject with the value 
		found with the requestWaypointInfoSrv call */
		AU_UAV_ROS::waypoint newDest; 
		newDest.latitude = requestWaypointInfoSrv.response.latitude;
		newDest.longitude = requestWaypointInfoSrv.response.longitude;
		newDest.altitude = requestWaypointInfoSrv.response.altitude;

		planes[planeID].setDestination(newDest);
	}
	else if (planes.find(planeID) == planes.end()) 
		/* New plane without waypoint set */
		return; 
	

	/* Note: The requestWaypointInfo service returns a waypoint of 
	-1000, -1000 when the UAV cannot retrieve a destination from queue.

	If the plane has no waypoint to go to, put it far from all others.

	Or, if the plane does have a waypoint to go to, update the plane 
	with new position and destination received from requestWaypointInfoSrv
	response*/
	if (requestWaypointInfoSrv.response.latitude == -1000){ /* plane has no waypoints to go to */
		ROS_ERROR("NO MORE WAYPOINTS");
		/* Remove in real flights*/
		planes[planeID].setCurrentLoc(-1000,-1000,400);
		/* update the time of last update for this plane to acknowledge 
		it is still in the air */
		planes[planeID].updateTime(); 
		return; 
	}
	else{
		planes[planeID].update(*msg); /* update plane with new position */

		AU_UAV_ROS::waypoint newDest;

		newDest.latitude = requestWaypointInfoSrv.response.latitude;
		newDest.longitude = requestWaypointInfoSrv.response.longitude;
		newDest.altitude = requestWaypointInfoSrv.response.altitude;

		planes[planeID].setDestination(newDest); /* update plane destination */
	}


	/* This line of code calls the collision avoidance algorithm 
	and determines if there should be collision avoidance 
	maneuvers taken. Returns a waypointContainer which contains new waypoints for 
	the current plane and the threatening plane. If there is no threatening plane, 
	*/	
	AU_UAV_ROS::waypointContainer bothNewWaypoints = findNewWaypoint(planes[planeID], planes);
	
	/* If plane2 has a new waypoint to go to, then send it there!*/
	AU_UAV_ROS::waypoint newWaypoint = bothNewWaypoints.plane1WP;

	if (bothNewWaypoints.plane2ID >= 0) {
		AU_UAV_ROS::waypoint newWaypoint2 = bothNewWaypoints.plane2WP;
		goToWaypointSrv.request.planeID = bothNewWaypoints.plane2ID;
		goToWaypointSrv.request.latitude = newWaypoint2.latitude;
		goToWaypointSrv.request.longitude = newWaypoint2.longitude;
		goToWaypointSrv.request.altitude = newWaypoint2.altitude;
		goToWaypointSrv.request.isAvoidanceManeuver = true; 
		goToWaypointSrv.request.isNewQueue = true;
		ROS_ERROR("NEW WAYPOINT FOR SECOND PLANE 176 Col Av.");
		
		if (goToWaypointClient.call(goToWaypointSrv)){
			count++;
			ROS_INFO("Received response from service request %d", (count-1));
		}
		else{
			ROS_ERROR("Did not receive response");
		}
		
	}
	
	if ((requestWaypointInfoSrv.response.longitude == newWaypoint.longitude) 
		&& (requestWaypointInfoSrv.response.latitude == newWaypoint.latitude)) {
	return;
	}	

	/* Fill in goToWaypointSrv request with new waypoint information*/
	goToWaypointSrv.request.planeID = planeID;
	goToWaypointSrv.request.latitude = newWaypoint.latitude;
	goToWaypointSrv.request.longitude = newWaypoint.longitude;
	goToWaypointSrv.request.altitude = newWaypoint.altitude;
	goToWaypointSrv.request.isAvoidanceManeuver = true; 
	goToWaypointSrv.request.isNewQueue = true;
	ROS_ERROR("NEW WAYPOINT FOR FIRST PLANE 200 COL AV.");

	if (goToWaypointClient.call(goToWaypointSrv)){
		count++;
		ROS_INFO("Received response from service request %d", (count-1));
	}
	else{
		ROS_ERROR("Did not receive response");
	}
	
}

bool startCollisionAvoidance(AU_UAV_ROS::StartCollisionAvoidance::Request &req, AU_UAV_ROS::StartCollisionAvoidance::Response &res)
{
    //ROS_INFO(req.indicator);
    readyToStart = true;
    
    // we shouldn't ever have an error, so populate it with "passed"
    res.error = "passed";
    return true;
}

int main(int argc, char **argv) {	
	//standard ROS startup
	ros::init(argc, argv, "collisionAvoidance");
	ros::NodeHandle n;


        //init service to start this node        
	ros::ServiceServer startCollisionAvoidanceServer = n.advertiseService("start_collision_avoidance", startCollisionAvoidance);

	ros::Rate r(10); // 10 hz
        while (!readyToStart)
        {
        ros::spinOnce();
        r.sleep();
        }

	/* Subscribe to telemetry outputs and create clients for the goToWaypoint and requestWaypointInfo services. */
	ros::Subscriber sub = n.subscribe("telemetry", 1000, telemetryCallback);
	goToWaypointClient = n.serviceClient<AU_UAV_ROS::GoToWaypoint>("go_to_waypoint");
	requestWaypointInfoClient = n.serviceClient<AU_UAV_ROS::RequestWaypointInfo>("request_waypoint_info");

	//initialize counting
	count = 0;	
    
	//needed for ROS to wait for callbacks
	ros::spin();	

	return 0;
}



