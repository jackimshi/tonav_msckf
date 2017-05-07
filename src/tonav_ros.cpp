#include "tonav_ros.h"

#include <cv_bridge/cv_bridge.h>
#include <Eigen/Core>
#include <image_transport/image_transport.h>
#include <ros/ros.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/CameraInfo.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2_ros/transform_broadcaster.h>
#include <eigen_conversions/eigen_msg.h>
#include <opencv2/highgui/highgui.hpp>

#include "calibration.h"
#include "tonav.h"
#include "stats.h"

TonavRos::TonavRos() {
    current_camera_matrix_.setZero();
    current_distirtion_params_.setZero();
}

int TonavRos::run(int argc, char* argv[]) {
    ROS_INFO_STREAM("Tonav ROS - tomas789@gmail.com \n");
    ros::init(argc, argv, "tonav_ros");
//    if (!parseCommandLineParams(argc, argv)) {
//        printHelp();
//        return 1;
//    }
    if (variables_map_.count("help")) {
        printHelp();
        return 1;
    }
    ros::NodeHandle node_handle;
    
    //tf_broadcaster_.reset(new tf2_ros::TransformBroadcaster);
    //tf_buffer_.reset(new tf2_ros::Buffer);
   // tf_listener_.reset(new tf2_ros::TransformListener(*tf_buffer_));
    
    std::string imu_topic = "/imu0";
    std::string camera_topic = "/cam0/image_raw";
    std::string camerainfo_topic = "/cam0/camera_info";
    std::string calibration_file = "/home/exbot/catkin_ws/src/tonav/examples/calibration.yaml";
   // robot_base_link_ = variables_map_["robot_base_link"].as<std::string>();
    std::string twist_topic = variables_map_.count("twist") != 0
        ? variables_map_["twist"].as<std::string>()
        : "";
    
    ros::Subscriber camerainfo_subscriber = node_handle.subscribe(camerainfo_topic, 50, &TonavRos::cameraInfoCallback, this);
    
    image_transport::ImageTransport transport(node_handle);
    image_transport::Subscriber camera_subscriber = transport.subscribe(camera_topic, 5, &TonavRos::cameraCallback, this);
    
    ros::Subscriber imu_subscriber = node_handle.subscribe(imu_topic, 50, &TonavRos::imuCallback, this);
    
    ros::Subscriber twist_subscriber;
    if (!twist_topic.empty()) {
        twist_subscriber = node_handle.subscribe(twist_topic, 50, &TonavRos::twistCallback, this);
    }
    
    ros::Rate rate(15);
    while (true) {
        if (!camera_frame_id_.empty() && !imu_frame_id_.empty()) {
            is_ready_to_filter_ = true;
            camerainfo_subscriber.shutdown();
            break;
        }
        rate.sleep();
        ros::spinOnce();
    }
    
    image_publisher_.reset(new image_transport::Publisher(transport.advertise("tonav/image", 5)));
    
    std::shared_ptr<Calibration> calibration = Calibration::fromPath(calibration_file);
    
    
    Quaternion q_C_B = Quaternion::identity();
    Eigen::Vector3d p_B_C;
//    try {
//        geometry_msgs::TransformStamped body_to_camera_transform = tf_buffer_->lookupTransform(camera_frame_id_, imu_frame_id_, ros::Time(0));
//        double x = body_to_camera_transform.transform.rotation.x;
//        double y = body_to_camera_transform.transform.rotation.y;
//        double z = body_to_camera_transform.transform.rotation.z;
//        double w = body_to_camera_transform.transform.rotation.w;
//        q_C_B = Quaternion(x, y, z, w);
//        tf::vectorMsgToEigen(body_to_camera_transform.transform.translation, p_B_C);
//    } catch (tf2::TransformException& e) {
//        ROS_ERROR("%s", e.what());
//        return 1;
//    }
          q_C_B = Quaternion(0.00666398307551, -0.0079168224269, -0.701985972528, 0.712115587266);
          p_B_C<<-0.0111674199187,-0.0574640920022,0.0207586947896;
    calibration->setBodyToCameraRotation(q_C_B);
    //calibration->setBodyToCameraRotation(Quaternion(-0.5, -0.5, 0.5, -0.5));
    tonav_.reset(new Tonav(calibration, p_B_C));
    
    ros::spin();
    
    return 0;
}

void TonavRos::setAllowedOptionsDescription() {
    options_description_.add_options()
        ("help,h", "print help message")
        ("calibration", po::value<std::string>(), "calibration file")
        ("image", po::value<std::string>(), "image topic")
        ("camerainfo", po::value<std::string>(), "camera_info topic")
        ("imu", po::value<std::string>(), "imu topic")
        ("robot_base_link", po::value<std::string>(), "robot base_link frame")
        ("twist", po::value<std::string>(), "twist topic for initialization")
    ;
}

bool TonavRos::parseCommandLineParams(int argc, char* argv[]) {
    setAllowedOptionsDescription();
    try {
        auto parsed = po::parse_command_line(argc, argv, options_description_);
        po::store(parsed, variables_map_);
    } catch (const po::error& e) {
        ROS_ERROR_STREAM(e.what());
        return false;
    }
    
    po::notify(variables_map_);
    
    if (variables_map_.count("image") == 0) {
        ROS_ERROR("Argument image is required");
        return false;
    } else if (variables_map_.count("image") > 1) {
        ROS_ERROR("Got multiple occurrences of image. Expected one.");
        return false;
    } else if (variables_map_.count("camerainfo") == 0) {
        ROS_ERROR("Argument camerainfo is required");
        return false;
    } else if (variables_map_.count("camerainfo") > 1) {
        ROS_ERROR("Got multiple occurrences of camerainfo. Expected one.");
        return false;
    } else if (variables_map_.count("imu") == 0) {
        ROS_ERROR("Argument imu is required");
        return false;
    } else if (variables_map_.count("imu") > 1) {
        ROS_ERROR("Got multiple imu of camerainfo. Expected one.");
        return false;
    } else if (variables_map_.count("calibration") == 0) {
        ROS_ERROR("Argument calibration is required");
        return false;
    } else if (variables_map_.count("calibration") > 1) {
        ROS_ERROR("Got multiple calibration of camerainfo. Expected one.");
        return false;
    } else if (variables_map_.count("robot_base_link") == 0) {
        ROS_ERROR("Argument robot_base_link is required");
        return false;
    } else if (variables_map_.count("robot_base_link") > 1) {
        ROS_ERROR("Got multiple occurrences of robot_base_link. Expected one.");
        return false;
    } else if (variables_map_.count("twist") > 1) {
        ROS_ERROR("Got multiple occurrences of twist. Expected one.");
        return false;
    }
    
    return true;
}

void TonavRos::printHelp() {
    ROS_INFO_STREAM(options_description_);
}

void TonavRos::cameraCallback(const sensor_msgs::ImageConstPtr &msg) {
    camera_callback_counter_ += 1;
    if (!is_ready_to_filter_) {
        return;
    }
//    ROS_INFO_STREAM("Camera Seq: [" << msg->header.seq << "]");
    try {
        const std_msgs::Header header = msg->header;
        double timestamp = getMessageTime(header.stamp);
        cv::Mat image = cv_bridge::toCvCopy(msg, msg->encoding)->image;
        tonav_->updateImage(timestamp, image);
    } catch (cv_bridge::Exception& e) {
        ROS_ERROR("Could not convert from '%s' to 'mono8'.", msg->encoding.c_str());
    }
}

void TonavRos::cameraInfoCallback(const sensor_msgs::CameraInfoConstPtr &msg) {
    ROS_DEBUG_STREAM("CameraInfo Seq: [" << msg->header.seq << "]");
    
    camera_frame_id_ = msg->header.frame_id;
    
    for (std::size_t i = 0; i < 9; ++i) {
        std::size_t row = i / 3;
        std::size_t col = i % 3;
        current_camera_matrix_(row, col) = msg->K[i];
    }
    
    for (std::size_t i = 0; i < 5; ++i) {
        current_distirtion_params_(i, 0) = msg->D[i];
    }
}

void TonavRos::imuCallback(const sensor_msgs::ImuConstPtr &msg) {
    if (!is_ready_to_filter_) {
        imu_frame_id_ = msg->header.frame_id;
        return;
    }
    
    ROS_DEBUG_STREAM("Imu Seq: [" << msg->header.seq << "]");
    const std_msgs::Header header = msg->header;
    double timestamp = getMessageTime(header.stamp);
    bool was_updated = false;
    
    Eigen::Vector3d angular_velocity;
    angular_velocity << msg->angular_velocity.x, msg->angular_velocity.y, msg->angular_velocity.z;
    if (!angular_velocity.isZero(1e-12)) {
        was_updated = was_updated || tonav_->updateRotationRate(timestamp, angular_velocity);
    }
    
    Eigen::Vector3d linear_acceleration;
    linear_acceleration << msg->linear_acceleration.x, msg->linear_acceleration.y, msg->linear_acceleration.z;
    if (!linear_acceleration.isZero(1e-12)) {
        was_updated = was_updated || tonav_->updateAcceleration(timestamp, linear_acceleration);
    }
    
    if (was_updated) {
        ros::Duration duration(tonav_->time());
        publishResults(time_beginning_ + duration);
    }
}

void TonavRos::twistCallback(const geometry_msgs::TwistStampedPtr& msg) {
    if (!tonav_) {
        return;
    }
    Eigen::Vector3d velocity;
    velocity << msg->twist.linear.x, msg->twist.linear.y, msg->twist.linear.z;
    tonav_->initializer()->setVelocity(velocity);
}

double TonavRos::getMessageTime(ros::Time stamp) {
    if (time_beginning_.isZero()) {
        time_beginning_ = stamp;
    }
    ros::Duration duration = stamp - time_beginning_;
    return duration.toSec();
}

void TonavRos::publishResults(const ros::Time& time) {
    geometry_msgs::TransformStamped transform;
    Quaternion attitude = tonav_->getCurrentOrientation();
    Eigen::Vector3d position = tonav_->getCurrentPosition() / 50.0;
    Eigen::Vector3d velocity = tonav_->getCurrentVelocity();
//    transform.header.stamp = time;
//    transform.header.frame_id = "map";
//    transform.child_frame_id = "base_link";
//    transform.transform.translation.x = position(0, 0);
//    transform.transform.translation.y = position(1, 0);
//    transform.transform.translation.z = position(2, 0);
//    transform.transform.rotation.x = attitude.x();
//    transform.transform.rotation.y = attitude.y();
//    transform.transform.rotation.z = attitude.z();
//    transform.transform.rotation.w = attitude.w();
//    tf_broadcaster_->sendTransform(transform);
    std::cout<<"position       "<<position(0, 0)<<"   "<<position(1, 0)<<"   "<<position(2, 0)<<std::endl;
    const cv::Mat& img = tonav_->getCurrentImage();
    sensor_msgs::ImagePtr msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", img).toImageMsg();
    image_publisher_->publish(msg);
    
    Stats& stats = Stats::getGlobalInstance();
    stats["times"].add(time.toSec());
    stats["position_x"].add(position(0, 0));
    stats["position_y"].add(position(1, 0));
    stats["position_z"].add(position(2, 0));
    stats["velocity_x"].add(velocity(0, 0));
    stats["velocity_y"].add(velocity(1, 0));
    stats["velocity_z"].add(velocity(2, 0));
}
