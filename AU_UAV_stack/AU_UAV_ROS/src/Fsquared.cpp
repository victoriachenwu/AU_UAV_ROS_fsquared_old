/*
Authors: Andrew Cunningham
		 Victoria Wu

Description:
		This is an implementation of the fsquared algorithm for collision avoidance and detection.
		For a description of each of these functions refer to Fsquared.h, comments in this file
		will be limited to comments on implementation


Date: 6/13/13


*/

//#include "AU_UAV_ROS/Fsquared.h"
#include "../../include/AU_UAV_ROS/Fsquared.h"

/*
 * findFieldAngle
 * Preconditions:	assumes valid planes
 * params:		me: plane that is potentially in enemy's field
 * 			enemy: plane that is producing field
 * returns:		field angle - angle between my bearing and location of enemy plane
 */
double Fsquared::findFieldAngle(PlaneObject& me, PlaneObject &enemy)	{

}

/* Assumptions:
 * 		Only calculates radially repuslive forces from enemy to "me"
 *
 * Pseudocode:
 * 		find "me" coordinates from enemy's POV
 *		check to see if these coordinates are within enemy's field
 *		if "me" is in the enemys field
 *			find the repulisve force
 *		else
 *			the repulsive force imparted by enemy has magnitude 0
 *
 * Variables:
 * 		fieldAngle: angle between the bearing of the plane generating the force to the location
 *		rMagnitude: magnitude of the repuslive force vector
 *		rAngle:		angle of repulsive force
 *		relativePosition: x and y difference in position between me and enemy from
 *						  enemy's POV
 *
 * TODO:
 * 		Add a feel function
 * 		Incorporate "right hand turn" rules
 */

mathVector calculateRepulsiveForce(PlaneObject &me, PlaneObject &enemy){
	double fieldAngle, rMagnitude, rAngle;
	Coordinates relativePosition;


	fieldAngle = findFieldAngle(me, enemy);
	//find "me" coordinates from enemy's POV
	relativePosition = findRelativePosition(me, enemy, fieldAngle);
	//determine whether or not "me" is in enemy's field
	insideEnemyField = inEnemyField(enemy, relativePosition);
	//if "me" is in enemy field
	if(insideEnemyField){
		//calculate the force exerted by the field on "me"
		rMagnitude = enemy.findMyFieldForceMagnitude(relativePosition);
		//calculate the angle of the force exerted by the field onto me
		rAngle = toCartesian(enemy.findAngle(me) - 180);
		mathVector repulsiveForceVector(rMagnitude, rAngle);
		return repulsiveForceVector;
	}
	//"me" is not in the enemy's field, return a vector with 0 magnitude (no contribution to force)
	else{
		mathVector repulsiveForceVector(0,0);
		return repulisveForceVector;
	}
}