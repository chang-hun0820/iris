#pragma once
#include "global_point_distribution.hpp"
#include "pangolin_cloud.hpp"
#include "system.hpp"
#include <pangolin/pangolin.h>
#include <pcl/correspondence.h>
#include <thread>

namespace vllm
{
struct Color {
  float r;
  float g;
  float b;
  float size;
  Color() { r = g = b = size = 1.0f; }
  Color(float r, float g, float b, float s) : r(r), g(g), b(b), size(s) {}
};

class PangolinViewer
{
private:
  std::shared_ptr<System> system_ptr = nullptr;

  pangolin::OpenGlRenderState makeCamera(
      const Eigen::Vector3f& from = Eigen::Vector3f(-2, 0, 5),
      const Eigen::Vector3f& to = Eigen::Vector3f(0, 0, 0),
      const pangolin::AxisDirection up = pangolin::AxisX);

  std::thread viewer_thread;

public:
  PangolinViewer(const std::shared_ptr<System>& system_ptr);
  PangolinViewer() : PangolinViewer(nullptr){};

  void init();

  ~PangolinViewer() = default;

  void swap() const { pangolin::FinishFrame(); }

  void clear()
  {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    d_cam.Activate(*s_cam);
  }

  void startLoop()
  {
    if (system_ptr == nullptr) {
      std::cout << "syste_ptr is nullptr" << std::endl;
      exit(EXIT_FAILURE);
    }
    viewer_thread = std::thread(&PangolinViewer::loop, this);
  }

  void loop()
  {
    init();
    while (true) {
      execute();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  int execute()
  {
    clear();

    drawGridLine();
    // drawString("VLLM", {1.0f, 1.0f, 0.0f, 3.0f});

    drawPointCloud(system_ptr->getTargetCloud(), {0.6f, 0.6f, 0.6f, 1.0f});
    if (*gui_target_normals)
      drawNormals(system_ptr->getTargetCloud(), system_ptr->getTargetNormals(), {0.0f, 1.0f, 1.0f, 1.0f}, 50);

    drawPointCloud(system_ptr->getAlignedCloud(), {1.0f, 1.0f, 0.0f, 2.0f});
    if (*gui_source_normals)
      drawNormals(system_ptr->getAlignedCloud(), system_ptr->getAlignedNormals(), {1.0f, 0.0f, 1.0f, 1.0f});

    if (*gui_raw_camera) {
      drawCamera(system_ptr->getRawCamera(), {1.0f, 0.0f, 1.0f, 1.0f});
      drawTrajectory(system_ptr->getRawTrajectory(), false, {1.0f, 0.0f, 1.0f, 1.0f});
    }

    drawTrajectory(system_ptr->getTrajectory(), true);
    drawCamera(system_ptr->getCamera(), {1.0f, 0.0f, 0.0f, 1.0f});

    // drawCorrespondences(system_ptr->getAlignedCloud(), system_ptr->getTargetCloud(),
    //     system_ptr->getCorrespondences(), {0.0f, 0.0f, 1.0f, 2.0f});


    // Eigen::Vector3d gain(*gui_scale_gain, *gui_pitch_gain, *gui_model_gain);
    // Eigen::Vector2d distance(*gui_distance_min, *gui_distance_max);
    // system_ptr->setGain(gain);
    // system_ptr->setSearchDistance(distance);
    // system_ptr->setRecollection(*gui_recollection);

    swap();

    // if (pangolin::Pushed(*gui_quit))
    //   return -1;

    // if (pangolin::Pushed(*gui_reset))
    //   system_ptr->requestReset();

    return 0;
  }

  // void drawGPD(const GPD& gpd) const;
  void drawGridLine() const;
  void drawString(const std::string& str, const Color& color) const;
  void drawTrajectory(const std::vector<Eigen::Vector3f>& trajectory, bool colorful, const Color& color = Color());
  void drawPointCloud(const pcl::PointCloud<pcl::PointXYZ>::Ptr& cloud, const Color& color) const;
  void drawCamera(const Eigen::Matrix4f& cam_pose, const Color& color) const;
  void drawNormals(
      const pcl::PointCloud<pcl::PointXYZ>::Ptr& cloud,
      const pcl::PointCloud<pcl::Normal>::Ptr& normals,
      const Color& color,
      int skip = 1) const;
  void drawCorrespondences(
      const pcl::PointCloud<pcl::PointXYZ>::Ptr& source,
      const pcl::PointCloud<pcl::PointXYZ>::Ptr& target,
      const pcl::CorrespondencesPtr& correspondences,
      const Color& color) const;

private:
  std::shared_ptr<pangolin::OpenGlRenderState> s_cam;
  std::shared_ptr<pangolin::Handler3D> handler;
  pangolin::View d_cam;

  // GUI variables
  std::shared_ptr<pangolin::Var<bool>> gui_raw_camera;
  std::shared_ptr<pangolin::Var<bool>> gui_source_normals;
  std::shared_ptr<pangolin::Var<bool>> gui_target_normals;
  std::shared_ptr<pangolin::Var<double>> gui_scale_gain;
  std::shared_ptr<pangolin::Var<double>> gui_pitch_gain;
  std::shared_ptr<pangolin::Var<double>> gui_model_gain;
  std::shared_ptr<pangolin::Var<double>> gui_distance_min;
  std::shared_ptr<pangolin::Var<double>> gui_distance_max;
  std::shared_ptr<pangolin::Var<unsigned int>> gui_recollection;
  std::shared_ptr<pangolin::Var<bool>> gui_quit;
  std::shared_ptr<pangolin::Var<bool>> gui_reset;

  void drawRectangular(const float x, const float y, const float z) const;
  void drawLine(
      const float x1, const float y1, const float z1,
      const float x2, const float y2, const float z2) const;

  void drawFrustum(const float w) const;

  // h[0,360],s[0,1],v[0,1]
  Eigen::Vector3f convertRGB(Eigen::Vector3f hsv);
};
}  // namespace vllm
