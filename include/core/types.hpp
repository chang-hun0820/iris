#pragma once
#include "estimator/correspondence_estimation_backprojection.hpp"
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/registration/correspondence_rejection_distance.h>

namespace vllm
{
using pcXYZ = pcl::PointCloud<pcl::PointXYZ>;
using pcNormal = pcl::PointCloud<pcl::Normal>;
using crrspEstimator = vllm::registration::CorrespondenceEstimationBackProjection<pcl::PointXYZ, pcl::PointXYZ, pcl::Normal>;
using crrspRejector = pcl::registration::CorrespondenceRejectorDistance;

}  // namespace vllm