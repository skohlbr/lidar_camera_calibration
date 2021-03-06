#include <cstdlib>
#include <cstdio>
#include <math.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <utility> 

#include <Eigen/Dense>
#include <Eigen/SVD>
#include <Eigen/Geometry>

using namespace Eigen;

std::string pkg_loc = ros::package::getPath("lidar_camera_calibration");

std::pair<MatrixXd, MatrixXd> readArray()
{
	std::ifstream infile(pkg_loc + "/conf/points.txt");
	int num_points=0;

	infile >> num_points;

	ROS_ASSERT(num_points > 0);
	
	MatrixXd lidar(3,num_points), camera(3,num_points);
	
	std::cout << "Num points is:" << num_points << std::endl;
	
	for(int i=0; i<num_points; i++)
	{
		infile >> lidar(0,i) >> lidar(1,i) >> lidar(2,i);
	}
	for(int i=0; i<num_points; i++)
	{
		infile >> camera(0,i) >> camera(1,i) >> camera(2,i);
	}
	infile.close();

	// camera values are stored in variable 'lidar' and vice-versa
	// need to change this
	return std::pair<MatrixXd, MatrixXd>(lidar, camera);
	//return std::pair<MatrixXd, MatrixXd>(camera, lidar);
}

// calculates rotation and translation that transforms points in the lidar frame to the camera frame
Matrix4d calc_RT(MatrixXd lidar, MatrixXd camera)
{
	int num_points = lidar.cols();
	std::cout << "Number of points: " << num_points << std::endl;
	Vector3d mu_lidar, mu_camera;
	
	mu_lidar << 0.0, 0.0, 0.0;
	mu_camera << 0.0, 0.0, 0.0;

	for(int i=0; i<num_points; i++)
	{
		mu_lidar(0) += lidar(0,i);
		mu_lidar(1) += lidar(1,i);
		mu_lidar(2) += lidar(2,i);
	}
	for(int i=0; i<num_points; i++)
	{
		mu_camera(0) += camera(0,i);
		mu_camera(1) += camera(1,i);
		mu_camera(2) += camera(2,i);
	}

	mu_lidar = mu_lidar/num_points;
	mu_camera = mu_camera/num_points;

	std::cout << "mu_lidar: \n" << mu_lidar << std::endl;
	std::cout << "mu_camera: \n" << mu_camera << std::endl;

	MatrixXd lidar_centered = lidar.colwise() - mu_lidar;
	MatrixXd camera_centered = camera.colwise() - mu_camera;

	std::cout << "lidar_centered: \n" << lidar_centered << std::endl;
	std::cout << "camera_centered: \n" << camera_centered << std::endl;

	Matrix3d cov = camera_centered*lidar_centered.transpose();

	std::cout << cov << std::endl;

	JacobiSVD<MatrixXd> svd(cov, ComputeFullU | ComputeFullV);

	Matrix3d rotation;
	rotation = svd.matrixU() * svd.matrixV().transpose();
	if( rotation.determinant() < 0 )
	{
		Vector3d diag_correct;
		diag_correct << 1.0, 1.0, -1.0; 

		rotation = svd.matrixU() * diag_correct.asDiagonal() * svd.matrixV().transpose();
	}
	
	Vector3d translation = mu_camera - rotation*mu_lidar;


	Vector3d ea = rotation.eulerAngles(2, 1, 0);

	std::cout << "Rotation matrix: \n" << rotation << std::endl;
	//std::cout << "Rotation in Euler angles: \n" << ea*57.3 << std::endl;
	std::cout << "Translation: \n" << translation << std::endl;

	MatrixXd eltwise_error = (camera - ((rotation*lidar).colwise() + translation)).array().square().colwise().sum();
	double error = sqrt(eltwise_error.sum()/num_points);
	std::cout << "RMSE: " << error << std::endl;

	Matrix4d T;
	T.setIdentity(4,4);
	T.topLeftCorner(3, 3) = rotation;
	T.col(3).head(3) = translation;

	std::cout << "Rigid-body transformation: \n" << T << std::endl;
	return T;
}

std::vector<std::string> split_by_space(std::string str)
{
	std::string buf; // Have a buffer string
	std::stringstream ss(str); // Insert the string into a stream

	std::vector<std::string> tokens; // Create vector to hold our words

	while (ss >> buf)
	    tokens.push_back(buf);

	return tokens;
}

Vector3d convert_to_vec(std::vector<std::string> str)
{
	Vector3d retval;
	retval.setZero();

	for(int i = 0; i<3; i++)
	{
		retval(i) = atof(str[i].c_str());
	}

	return retval;
}

void readArucoPose()
{
	std::vector<Matrix4d> marker_pose;

	std::ifstream infile(pkg_loc + "/conf/transform.txt");

	std::string line;    
	while (std::getline(infile, line))
	{

		std::cout << "In readArucoPose(): " << line << std::endl;
		std::size_t pos_t = line.find("Txyz");
		std::size_t pos_r = line.find("Rxyz");

		std::string t3, r3;
		t3 = line.substr(pos_t+5, pos_r-pos_t-5);
		r3 = line.substr(pos_r+5, line.length());

		std::cout << t3 << " -- " << r3 << std::endl;

		Vector3d trans, rot;
		trans = convert_to_vec(split_by_space(t3));
		rot = convert_to_vec(split_by_space(r3));

		std::cout << "\n" << trans << "\n" << rot << std::endl;

		
		Transform<double,3,Affine> aa;
		aa = AngleAxis<double>(rot.norm(), rot/rot.norm());

		Matrix4d g;
		g.setIdentity(4,4);
		//std::cout << "Rot matrix is: \n" << aa*g << std::endl;
		g = aa*g;

		Matrix4d T;
		T.setIdentity(4,4);
		T.topLeftCorner(3, 3) = g.topLeftCorner(3,3);//.transpose();
		T.col(3).head(3) = trans;

		marker_pose.push_back(T);

		std::cout << "transformation matrix is: \n" << T << std::endl;
	}

	infile.close();

	//std::vector<std::vector<std::pair<float, float> > > marker_coordinates;
	infile.open(pkg_loc + "/conf/marker_coordinates.txt");
	std::ofstream outfile(pkg_loc + "/conf/points.txt", std::ios_base::app);

	int num_of_markers;
	infile >> num_of_markers;

	for(int i=0; i<num_of_markers; i++)
	{
		float temp;
		std::vector<float> board;
		//std::vector<std::pair<float, float> > corner_points;
		for(int j=0; j<5; j++)
		{
			infile >> temp;
			board.push_back(temp/100.0);
		}
		float la, ba;
		la=board[4]/2+board[2];
    	ba=board[4]/2+board[3];

    	/*corner_points.push_back(std::make_pair(ba, 		   board[0]-la));
    	corner_points.push_back(std::make_pair(ba-board[1], board[0]-la));
    	corner_points.push_back(std::make_pair(ba-board[1], -la 		  ));
    	corner_points.push_back(std::make_pair(ba, 		   -la 		  ));*/

    	Matrix4d points_board;
    	points_board << ba, 		 0, board[0]-la, 1,
    					ba-board[1], 0, board[0]-la, 1,
    					ba-board[1], 0, -la, 		 1,
    					ba, 		 0, -la, 		 1;

    	/*std::cout << "Points in before transform: \n" << points_board << std::endl;*/

    	points_board = marker_pose[i]*(points_board.transpose());

    	std::cout << "Board number: " << i+1 << "\n";
    	std::cout << "P1: " << ba << " " << board[0]-la << "\n";
    	std::cout << "P2: " << ba-board[1] << " " << board[0]-la << "\n";
    	std::cout << "P3: " << ba-board[1] << " " << -la << "\n";
    	std::cout << "P4: " << ba << " " << -la << "\n\n";

    	std::cout << "Points in camera frame: \n" << points_board << std::endl;

    	//marker_coordinates.push_back(corner_points);

    	
    	for(int k=0; k < 4; k++)
    	{
    		outfile << points_board(0,k) << " " << points_board(1,k) << " " << points_board(2,k) <<  "\n";
    	}
    	
	}
	outfile.close();
	infile.close();
}


void find_transformation()
{
	readArucoPose();
	std::pair<MatrixXd, MatrixXd> point_clouds = readArray();
	Matrix4d T = calc_RT(point_clouds.first, point_clouds.second);
}