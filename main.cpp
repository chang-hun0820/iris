#include "aligner.hpp"
#include "bridge.hpp"
#include "pangolin_viewer.hpp"
#include "rejector_lpd.hpp"
#include "util.hpp"

#include <opencv2/core/eigen.hpp>
#include <opencv2/opencv.hpp>
#include <pcl/common/transforms.h>
#include <pcl/registration/correspondence_rejection_distance.h>
#include <popl.hpp>

using pcXYZ = pcl::PointCloud<pcl::PointXYZ>;

struct Config {
  Config(std::string yaml_file)
  {
    cv::FileStorage fs(yaml_file, cv::FileStorage::READ);
    {
      cv::Mat trans, normal, up;
      float s;
      fs["VLLM.t_init"] >> trans;
      fs["VLLM.normal_init"] >> normal;
      fs["VLLM.up_init"] >> up;
      fs["VLLM.s_init"] >> s;

      Eigen::Vector3f n, u, t;
      Eigen::Matrix3f R = Eigen::Matrix3f::Zero();
      cv::cv2eigen(normal, n);
      cv::cv2eigen(up, u);
      cv::cv2eigen(trans, t);

      n.normalize();
      u.normalize();
      R.row(2) = n;
      R.row(1) = (n.dot(u) * n - u).normalized();
      R.row(0) = R.row(1).cross(R.row(2));

      Eigen::Matrix4f T = Eigen::Matrix4f::Identity();
      T.topLeftCorner(3, 3) = s * R.transpose();
      T.topRightCorner(3, 1) = t;
      T_init = T;
      std::cout << T << std::endl;
    }

    fs["VLLM.normal_search_leaf"] >> normal_search_leaf;
    fs["VLLM.voxel_grid_leaf"] >> voxel_grid_leaf;
    fs["VLLM.pcd_file"] >> pcd_file;
    fs["VLLM.video_file"] >> video_file;
    fs["VLLM.gpd_size"] >> gpd_size;
    fs["VLLM.gpd_gain"] >> gpd_gain;
    fs["VLLM.iteration"] >> iteration;
    std::cout << "gpd_gain " << gpd_gain << std::endl;

    cv::Mat T = cv::Mat::eye(4, 4, CV_32FC1);
  }

  int iteration;
  float gpd_gain;
  int gpd_size;
  float normal_search_leaf;
  float voxel_grid_leaf;
  std::string pcd_file;
  std::string video_file;
  Eigen::Matrix4f T_init;
};

int main(int argc, char* argv[])
{
  // analyze arugments
  popl::OptionParser op("Allowed options");
  auto config_file_path = op.add<popl::Value<std::string>>("c", "config", "config file path");
  try {
    op.parse(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    std::cerr << std::endl;
    std::cerr << op << std::endl;
    exit(EXIT_FAILURE);
  }
  if (!config_file_path->is_set()) {
    std::cerr << "invalid arguments" << std::endl;
    exit(EXIT_FAILURE);
  }

  // create options
  Config config(config_file_path->value());

  // setup for LiDAR map
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_target = vllm::loadMapPointCloud(config.pcd_file, config.voxel_grid_leaf);
  pcl::PointCloud<pcl::Normal>::Ptr normals = vllm::estimateNormals(cloud_target, config.normal_search_leaf);

  // setup for OpenVSLAM
  BridgeOpenVSLAM bridge;
  bridge.setup(argc, argv, config.video_file);

  // setup for Viewer
  vllm::PangolinViewer pangolin_viewer;
  cv::namedWindow("OpenCV", cv::WINDOW_AUTOSIZE);

  // setup for Rejector
  vllm::GPD gpd(config.gpd_size, cloud_target, config.gpd_gain);
  vllm::CorrespondenceRejectorLpd rejector(gpd);

  Eigen::Matrix4f T_init = config.T_init;
  std::vector<Eigen::Vector3f> raw_trajectory;
  std::vector<Eigen::Vector3f> vllm_trajectory;

  Eigen::Matrix4f T_align = Eigen::Matrix4f::Identity();

  // == Main Loop ==
  while (true) {
    // Execute vSLAM
    bool success = bridge.execute();
    if (!success)
      break;

    // Get some information of vSLAM
    pcl::PointCloud<pcl::PointXYZ>::Ptr local_cloud(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointCloud<pcl::PointXYZ>::Ptr global_cloud(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointCloud<pcl::PointXYZ>::Ptr aligned_cloud(new pcl::PointCloud<pcl::PointXYZ>);
    bridge.getLandmarks(local_cloud, global_cloud);
    int state = static_cast<int>(bridge.getState());
    Eigen::Matrix4f camera_raw = bridge.getCameraPose().inverse().cast<float>();

    // Visualize by OpenCV
    cv::imshow("OpenCV", bridge.getFrame());
    int key = cv::waitKey(10);
    if (key == 'q') break;
    if (key == 'r') T_align = T_init;
    if (key == 's')
      while (cv::waitKey(0) == 's')
        ;

    // `2` means openvslam::tracking_state_t::Tracking
    if (state != 2 || local_cloud->empty()) {
      continue;
    }

    // Transform to subtract initial offset
    // pcl::transformPointCloud(*local_cloud, *aligned_cloud, T_init);
    // pcl::transformPointCloud(*global_cloud, *global_cloud, T_init);
    // camera = T_init * camera;

    // raw trajectory
    camera_raw = T_init * camera_raw;
    raw_trajectory.push_back(camera_raw.block(0, 3, 3, 1));
    pcl::transformPointCloud(*local_cloud, *local_cloud, T_init);
    pcl::transformPointCloud(*local_cloud, *aligned_cloud, T_align);

    Eigen::Matrix4f camera;

    for (int i = 0; i < config.iteration; i++) {
      // Get all correspodences
      pcl::Correspondences correspondences = vllm::getCorrespondences(aligned_cloud, cloud_target);
      // Reject invalid correspondeces
      correspondences = rejector.refineCorrespondences(correspondences, local_cloud);
      // Align pointclouds
      vllm::Aligner aligner;
      // T_align = aligner.estimate6DoF(T_align, *local_cloud, *cloud_target, correspondences, normals);
      T_align = aligner.estimate7DoF(T_align, *local_cloud, *cloud_target, correspondences, normals);
      // Integrate
      camera = T_align * camera_raw;
      pcl::transformPointCloud(*local_cloud, *aligned_cloud, T_align);

      // Visualize by Pangolin
      pangolin_viewer.clear();
      pangolin_viewer.drawGridLine();
      pangolin_viewer.drawString("state=" + std::to_string(state) + ", itr=" + std::to_string(i), {1.0f, 0.0f, 0.0f, 2.0f});
      // pangolin_viewer.drawPointCloud(local_cloud, {1.0f, 1.0f, 0.0f, 2.0f});
      pangolin_viewer.drawPointCloud(aligned_cloud, {1.0f, 1.0f, 0.0f, 2.0f});
      pangolin_viewer.drawPointCloud(cloud_target, {0.8f, 0.8f, 0.8f, 1.0f});
      pangolin_viewer.drawTrajectory(raw_trajectory, {1.0f, 0.0f, 1.0f, 3.0f});
      pangolin_viewer.drawTrajectory(vllm_trajectory, {1.0f, 0.0f, 0.0f, 3.0f});
      pangolin_viewer.drawCamera(camera, {1.0f, 1.0f, 1.0f, 1.0f});
      pangolin_viewer.drawCamera(camera_raw, {1.0f, 1.0f, 1.0f, 1.0f});
      pangolin_viewer.drawNormals(cloud_target, normals, {0.0f, 1.0f, 1.0f, 1.0f});
      // pangolin_viewer.drawCorrespondences(local_cloud, cloud_target, correspondences, {0.0f, 0.8f, 0.0f, 1.0f});
      pangolin_viewer.drawCorrespondences(aligned_cloud, cloud_target, correspondences, {1.0f, 0.0f, 0.0f, 1.0f});
      // pangolin_viewer.drawGPD(gpd);
      pangolin_viewer.swap();
    }
    std::cout << T_align << std::endl;
    vllm_trajectory.push_back(camera.block(0, 3, 3, 1));
  }

  return 0;
}