//
// Created by hannes on 2018-01-30.
//

#include <iostream>

#include <Eigen/Dense>
#include <entity/entity.h>
#include <kontiki/trajectories/linear_trajectory.h>
#include <kontiki/trajectories/uniform_r3_spline_trajectory.h>
#include <kontiki/trajectories/uniform_so3_spline_trajectory.h>
#include <kontiki/trajectories/split_trajectory.h>

#include <kontiki/trajectory_estimator.h>
#include <kontiki/measurements/position_measurement.h>
#include <kontiki/measurements/static_rscamera_measurement.h>
#include <kontiki/sensors/pinhole_camera.h>
#include <kontiki/sensors/atan_camera.h>
#include <kontiki/sfm/view.h>
#include <kontiki/sfm/landmark.h>
#include <kontiki/sfm/observation.h>
#include <entity/paramstore/empty_pstore.h>

using namespace kontiki;
using namespace kontiki::trajectories;
using namespace kontiki::sensors;
using namespace kontiki::measurements;

template<typename TrajectoryModel>
void do_trajectory(const type::Trajectory<TrajectoryModel, double> &trajectory) {
  std::cout << "from func, pos=" << trajectory.Position(0.3).transpose() << std::endl;
}

int main() {
  auto split = std::make_shared<SplitTrajectory>(0.4, 0.25);

  auto so3_spline = split->SO3Spline();
  auto r3_spline = split->R3Spline();

  for (int i=0; i < 12; ++i) {
    r3_spline->AppendKnot(Eigen::Vector3d::Random());
    so3_spline->AppendKnot(Eigen::Quaterniond::UnitRandom());
  }

  double t = 0.3;
  std::cout << "Pos: " << split->Position(t).transpose() << std::endl;
  std::cout << "orientation: " << split->Orientation(t).coeffs().transpose() << std::endl;

  TrajectoryEstimator<SplitTrajectory> estimator(split);

  auto camera = std::make_shared<PinholeCamera>(1920, 1080, 0.2);
  auto v1 = std::make_shared<View>(0, 0.);
  auto v2 = std::make_shared<View>(7, 2.0);
  auto lm = std::make_shared<Landmark>();
  auto ref = v1->CreateObservation(lm, 400, 400);
  auto obs = v2->CreateObservation(lm, 300, 200);
  lm->set_reference(ref);

  using MClass = StaticRsCameraMeasurement<PinholeCamera>;
  auto m = std::make_shared<MClass>(camera, obs);

  estimator.AddMeasurement(m);

//  auto m1 = std::make_shared<PositionMeasurement>(0.2, Eigen::Vector3d(1, 2, 3));
//  auto m2 = std::make_shared<PositionMeasurement>(0.45, Eigen::Vector3d(-1, 1, 0));
//  estimator.AddMeasurement<PositionMeasurement>(m1);
//  estimator.AddMeasurement<PositionMeasurement>(m2);

  auto summary = estimator.Solve();

  std::cout << summary.FullReport() << std::endl;
  return 0;
}