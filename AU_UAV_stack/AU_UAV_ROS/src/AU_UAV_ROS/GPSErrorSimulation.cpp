#include <AU_UAV_ROS/GPSErrorSimulation.h>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <cmath>

using namespace std;
#include "ros/package.h"

enum NOISE_TYPE {
  UNIFORM_RANDOM,
  WHITE_NOISE,
  COHERENT
};

// Constructor
AU_UAV_ROS::CGPSErrorSimulation::CGPSErrorSimulation()
{
  //    determine error direction
  //    initialize RND
  // Seed Random number generator
  srand(time(0));
  error_sign = (rand()>(RAND_MAX/2))?1:-1;
  gps_noise_type = get_env_value("AUROS_GPS_NOISE_TYPE");

  // Initialize libnoise values
  noise_counter=0;
}

// Function to generate a uniform random noise value
float AU_UAV_ROS::CGPSErrorSimulation::uniform_random_noise()
{
  // Uniform random number generator between 0.0 and 1.0
  // double unif_rand = (rand() - double(RAND_MAX)/2.0)/double(RAND_MAX);
  double unif_rand = (rand())/double(RAND_MAX);
  return unif_rand;
}


// Function to get value from environment
int AU_UAV_ROS::CGPSErrorSimulation::get_env_value(const char* ENV_VARIABLE)
{
  // Temporary variables for error checking
  char * pEnv;
  string sEnv;

  // Get the value from the environment
  pEnv = getenv (ENV_VARIABLE);
  // Check for value not set
  if (pEnv==NULL)
    sEnv = "0.0";
  else
    sEnv = pEnv;

  // Convert from ASCII to float
  return atoi(sEnv.c_str());
}

/* White noise Generation */
float AU_UAV_ROS::CGPSErrorSimulation::white_noise_value()
{
  /* Setup constants */
  const static int q = 15;
  const static float c1 = (1 << q) - 1;
  const static float c2 = ((int)(c1 / 3)) + 1;
  const static float c3 = 1.f / c1;
  
  /* random number in range 0 - 1 not including 1 */
  float random = 0.f;
  
  /* the white noise */
  float noise = 0.f;
  random = ((float)rand() / (float)(RAND_MAX + 1));
  noise = (2.f * ((random * c2) + (random * c2) + (random * c2)) - 3.f * (c2 - 1.f)) * c3;    
  
  return noise;
}

/* Use a moving average to smotth out the randomness and provide some hysteris */
double AU_UAV_ROS::CGPSErrorSimulation::calculate_moving_average(double new_value)
{
  static int count;
  static double total;

  // Increment the count
  count++;
  // Update total
  total += abs(new_value);

  // Return moving average
  return total/(double)count;  
}

/* White noise Generation */
float AU_UAV_ROS::CGPSErrorSimulation::coherent_noise_value()
{
  double noise = coherent_noise.GetValue((noise_counter++*0.01) + 1.25,
					 0.75,
					 0.50);
  return noise;
}

/* Introduce a GPS Correction */
/* Based on the table in edu-observator.org/gps/gps_accuracy */
/* We will approximate the error between the range o 0.0 to 10.0 */
float AU_UAV_ROS::CGPSErrorSimulation::correction()
{
  // Random number between 0.0 and 10.0;

  float value=0.0;

  switch(gps_noise_type)
    {
    case UNIFORM_RANDOM:
      value = calculate_moving_average(uniform_random_noise())*error_sign;
      break;
    case WHITE_NOISE:
      value = calculate_moving_average(white_noise_value())*error_sign;
      break;
    case COHERENT:
      value = coherent_noise_value();
      break;
    }
  FILE* fp = fopen((ros::package::getPath("AU_UAV_ROS")+"/scores/gps.cal").c_str(), "a");
  fprintf(fp, "GPS Noise Calculation noise:%f type:%d \n",
	  value,
	  error_sign);
  fclose(fp);
  return value;
}
