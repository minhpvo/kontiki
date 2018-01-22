//
// Created by hannes on 2018-01-15.
//

#ifndef TASERV2_SPLINE_BASE_H
#define TASERV2_SPLINE_BASE_H

#include <Eigen/Dense>

#include "trajectory.h"
#include "dataholders/vectorholder.h"

namespace taser {
namespace trajectories {
namespace detail {
static const Eigen::Matrix4d M = (Eigen::Matrix4d() <<
                                                    1. / 6., 4. / 6.,  1. / 6., 0,
    -3. / 6.,       0,  3. / 6., 0,
    3. / 6., -6. / 6,  3. / 6., 0,
    -1. / 6.,   3./6., -3. / 6., 1./6.).finished();

static const Eigen::Matrix4d M_cumul = (Eigen::Matrix4d() <<
                                                          6. / 6.,  5. / 6.,  1. / 6., 0,
    0. / 6.,  3. / 6.,  3. / 6., 0,
    0. / 6., -3. / 6.,  3. / 6., 0,
    0. / 6.,  1. / 6., -2. / 6., 1. / 6.).finished();

struct SplineSegmentMeta : public MetaBase {
  double t0; // First valid time
  double dt; // Knot spacing
  size_t n; // Number of knots

  SplineSegmentMeta(double dt, double t0) :
      dt(dt),
      t0(t0),
      n(0) { };

  SplineSegmentMeta() :
      SplineSegmentMeta(1.0, 0.0) { };

  int NumParameters() const override {
    return n;
  }

  double MinTime() const {
      Validate();
      return t0;
  }

  double MaxTime() const {
    Validate();
    return t0 + (n-3) * dt;
  }

  void Validate() const {
    if (n < 4) {
      throw std::range_error("Spline had too few control points");
    }
  }
};

// FIXME: Splines consists of segments. We should thus implement segment views and then have a general wrapper to combine them.
struct SplineMeta : public MetaBase {

  std::vector<SplineSegmentMeta> segments;

  int NumParameters() const override {
    /*return std::accumulate(segments.begin(), segments.end(),
                           0, [](int n, SplineSegmentMeta& meta) {
          return n + meta.NumParameters();
        });*/
    int n = 0;
    for (auto &segment_meta : segments) {
      n += segment_meta.NumParameters();
    }
    return n;
  }

  double MinTime() const {
    if (segments.size() == 1)
      return segments[0].MinTime();
    else
      throw std::runtime_error("Concrete splines must have exactly one segment");
  }

  double MaxTime() const {
    if (segments.size() == 1)
      return segments[0].MaxTime();
    else
      throw std::runtime_error("Concrete splines must have exactly one segment");
  }
};

template<typename T>
class AlwaysTrueValidator {
 public:
  static void Validate(const T&) {};
};

template<
    typename T,
    typename _ControlPointType,
    int _ControlPointSize,
    typename _ControlPointValidator = AlwaysTrueValidator<_ControlPointType>>
class SplineSegmentViewBase : public ViewBase<T, SplineSegmentMeta> {
 public:
  using ViewBase<T, SplineSegmentMeta>::Meta;
  using ControlPointType = _ControlPointType;
  using ControlPointMap = Eigen::Map<ControlPointType>;
  using ControlPointValidator = _ControlPointValidator;
  const static int ControlPointSize = _ControlPointSize;
  // Inherit constructor
  using ViewBase<T, SplineSegmentMeta>::ViewBase;

  const ControlPointMap ControlPoint(int i) const {
    return ControlPointMap(this->holder_->Parameter(i));
  }

  ControlPointMap MutableControlPoint(int i) {
    return ControlPointMap(this->holder_->Parameter(i));
  }

  T t0() const {
    return T(this->meta_.t0);
  }

  T dt() const {
    return T(this->meta_.dt);
  }

  size_t NumKnots() const {
    return this->meta_.n;
  }

  double MinTime() const override {
    return this->meta_.MinTime();
  }

  double MaxTime() const override {
    return this->meta_.MaxTime();
  }

  void CalculateIndexAndInterpolationAmount(T t, int& i0, T& u) const {
    T s = (t - t0()) / dt();
    i0 = PotentiallyUnsafeFloor(s);
    u = s - T(i0);
  }

 protected:
  int PotentiallyUnsafeFloor(double x) const {
    return static_cast<int>(std::floor(x));
  }

  // This way of treating Jets are potentially unsafe, hence the function name
  template<typename Scalar, int N>
  int PotentiallyUnsafeFloor(const ceres::Jet<Scalar, N>& x) const {
    return static_cast<int>(std::floor(x.a));
  };
};


template<typename T, template<typename> typename SegmentView>
class SplineViewBase : public ViewBase<T, SplineMeta> {
  using Result = std::unique_ptr<TrajectoryEvaluation<T>>;
 public:
  using ControlPointType = typename SegmentView<T>::ControlPointType;
  using ControlPointMap = typename SegmentView<T>::ControlPointMap;
  using ControlPointValidator = typename SegmentView<T>::ControlPointValidator;
  const static int ControlPointSize = SegmentView<T>::ControlPointSize;

  // Inherit Constructor
  using ViewBase<T, SplineMeta>::ViewBase;

  Result Evaluate(T t, int flags) const override {
    int parameter_offset = 0;
    for (auto &segment_meta : this->meta_.segments) {
      const int N = segment_meta.NumParameters();

      if ((t >= segment_meta.MinTime()) && (t < segment_meta.MaxTime())) {
        return SegmentView<T>(this->holder_->Slice(parameter_offset, N), segment_meta).Evaluate(t, flags);
      }

      parameter_offset += N;
    }

    throw std::range_error("No segment found for time t");
  }

  const ControlPointMap ControlPoint(int i) const {
    return this->ConcreteSegmentViewOrError().ControlPoint(i);
  }

  ControlPointMap MutableControlPoint(int i) {
    return this->ConcreteSegmentViewOrError().MutableControlPoint(i);
  }

  double MinTime() const override{
    return ConcreteSegmentViewOrError().MinTime();
  }

  double MaxTime() const override {
    return ConcreteSegmentViewOrError().MaxTime();
  }

  double t0() const {
    return ConcreteSegmentViewOrError().t0();
  }

  double dt() const {
    return ConcreteSegmentViewOrError().dt();
  }

  int NumKnots() const {
    return ConcreteSegmentViewOrError().NumKnots();
  }

  void CalculateIndexAndInterpolationAmount(T t, int& i0, T& u) const {
    return this->ConcreteSegmentViewOrError().CalculateIndexAndInterpolationAmount(t, i0, u);
  }

 protected:
  SegmentView<T> ConcreteSegmentViewOrError() const {
    if (this->meta_.segments.size() == 1)
      return SegmentView<T>(this->holder_, this->meta_.segments[0]);
    else
      throw std::logic_error("Concrete spline had multiple segments. This should not happen!");
  }

};


template<template<typename> typename ViewTemplate>
class SplinedTrajectoryBase : public TrajectoryBase<ViewTemplate> {
 protected:
  // Hidden constructor, not intended for user code
  SplinedTrajectoryBase(double dt, double t0, std::unique_ptr<ceres::LocalParameterization> local_parameterization) :
      TrajectoryBase<ViewTemplate>(new dataholders::VectorHolder<double>()),
      local_parameterization_(std::move(local_parameterization)) {
    this->meta_.segments.push_back(detail::SplineSegmentMeta(dt, t0));
  };

 public:
  using Meta = typename TrajectoryBase<ViewTemplate>::Meta;
  using ControlPointType = typename ViewTemplate<double>::ControlPointType;
  using ControlPointMap = typename ViewTemplate<double>::ControlPointMap;
  using ControlPointValidator = typename ViewTemplate<double>::ControlPointValidator;
  const static int ControlPointSize = ViewTemplate<double>::ControlPointSize;

  SplinedTrajectoryBase(double dt, double t0) :
    SplinedTrajectoryBase(dt, t0, nullptr) { };

  SplinedTrajectoryBase(double dt) :
      SplinedTrajectoryBase(dt, 0.0) { };

  SplinedTrajectoryBase() :
      SplinedTrajectoryBase(1.0) { };

  double t0() const {
    return this->AsView().t0();
  }

  double dt() const {
    return this->AsView().dt();
  }

  size_t NumKnots() const {
    return this->AsView().NumKnots();
  }

  void AddToProblem(ceres::Problem& problem,
                    const time_init_t &times,
                    Meta& meta,
                    std::vector<double*> &parameter_blocks,
                    std::vector<size_t> &parameter_sizes) const {

    auto view = this->AsView();
    double master_dt = view.dt();
    double master_t0 = view.t0();
    int current_segment_start = 0;
    int current_segment_end = -1; // Negative signals no segment created yet

    // Times are guaranteed to be sorted correctly and t2 >= t1
    for (auto tt : times) {

      int i1, i2;
      double u_notused;
      view.CalculateIndexAndInterpolationAmount(tt.first, i1, u_notused);
      view.CalculateIndexAndInterpolationAmount(tt.second, i2, u_notused);

      // Create new segment, or extend the current one
      if (i1 > current_segment_end) {
        double segment_t0 = master_t0 + master_dt * i1;
        meta.segments.push_back(detail::SplineSegmentMeta(master_dt, segment_t0));
        current_segment_start = i1;
      }
      else {
        i1 = current_segment_end + 1;
      }

      auto& current_segment_meta = meta.segments.back();

      // Add parameters and update currently active segment meta
      for (int i=i1; i < (i2 + 4); ++i) {
        auto ptr = this->holder_->Parameter(i);
        size_t size = ControlPointSize;
        parameter_blocks.push_back(ptr);
        parameter_sizes.push_back(size);
        problem.AddParameterBlock(ptr, size, this->local_parameterization_.get());
        current_segment_meta.n += 1;
      }

      current_segment_end = current_segment_start + current_segment_meta.n - 1;
    } // for times
  }

  ControlPointMap MutableControlPoint(size_t i) {
    return this->AsView().MutableControlPoint(i);
  }

  const ControlPointMap ControlPoint(size_t i) const {
    return this->AsView().ControlPoint(i);
  }

  void AppendKnot(const ControlPointType& cp) {
    ControlPointValidator::Validate(cp); // Or throw exception
    auto i = this->holder_->AddParameter(ControlPointSize);
    this->AsView().MutableControlPoint(i) = cp;
    // FIXME: Should check for single segment or give error
    this->meta_.segments[0].n += 1;
  }

  void ExtendTo(double t, const ControlPointType& fill_value) {
    while ((this->NumKnots() < 4) || (this->MaxTime() < t)) {
      this->AppendKnot(fill_value);
    }
  }

  const detail::SplineSegmentMeta& ConcreteSegmentMetaOrError() const {
    if (this->meta_.segments.size() == 1)
      return this->meta_.segments[0];
    else
      throw std::logic_error("Concrete spline had multiple segments. This should not happen!");
  }

 protected:
  std::unique_ptr<ceres::LocalParameterization> local_parameterization_;
};


} // namespace detail
} // namespace trajectories
} // namespace taser

#endif //TASERV2_SPLINE_BASE_H
