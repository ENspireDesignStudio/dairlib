#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>
#include <drake/common/trajectories/piecewise_polynomial.h>
#include <drake/multibody/plant/multibody_plant.h>

#include "lcm/lcm_trajectory.h"
#include "systems/trajectory_optimization/hybrid_dircon.h"

namespace dairlib {

/// Used for saving/loading trajectories.
/// To save a LcmTrajectory object, create one or multiple
/// LcmTrajectory::Trajectory structs and wrap them using the LcmTrajectory
/// constructor. Finally call writeToFile() with the desired relative filepath
///
/// To load a saved LcmTrajectory object, call the loadFromFile() with relative
/// filepath of the previously saved LcmTrajectory object

/// DirconTrajectory contains three Trajectory objects: the state trajectory,
/// the input trajectory, and the decision variables

class DirconTrajectory : LcmTrajectory {
 public:
  DirconTrajectory(const drake::multibody::MultibodyPlant<double>& plant);

  DirconTrajectory(
      const drake::multibody::MultibodyPlant<double>& plant,
      const systems::trajectory_optimization::HybridDircon<double>& dircon,
      const drake::solvers::MathematicalProgramResult& result,
      const std::string& name, const std::string& description);

  drake::trajectories::PiecewisePolynomial<double> ReconstructInputTrajectory();
  drake::trajectories::PiecewisePolynomial<double> ReconstructStateTrajectory();

  void loadFromFile(const std::string& filepath);

  /// Write this trajectory to the specified file, add any additional
  /// trajectories BEFORE calling this method
  void writeToFile(const std::string& filepath);

  Eigen::MatrixXd GetStateSamples(int mode) {
    DRAKE_ASSERT(mode >= 0);
    DRAKE_ASSERT(mode < num_modes_);
    return x_[mode];
  }
  Eigen::MatrixXd GetStateDerivativeSamples(int mode) {
    DRAKE_ASSERT(mode >= 0);
    DRAKE_ASSERT(mode < num_modes_);
    return xdot_[mode];
  }
  Eigen::MatrixXd GetStateBreaks(int mode) {
    DRAKE_ASSERT(mode >= 0);
    DRAKE_ASSERT(mode < num_modes_);
    return state_breaks_[mode];
  }
  Eigen::MatrixXd GetInputSamples() { return u_; }
  Eigen::MatrixXd GetBreaks() { return h_; }
  Eigen::VectorXd GetDecisionVariables() {
    DRAKE_ASSERT(has_data_);
    return decision_vars_;
  }

 private:
  const drake::multibody::MultibodyPlant<double>& plant_;

  const int n_x_;
  const int n_u_;

  int num_modes_ = 0;
  std::vector<int> mode_lengths_;
  bool has_data_;
  Eigen::VectorXd decision_vars_;
  Eigen::VectorXd h_;
  Eigen::MatrixXd u_;
  std::vector<Eigen::MatrixXd> x_;
  std::vector<Eigen::MatrixXd> xdot_;
  std::vector<Eigen::VectorXd> state_breaks_;
};
}  // namespace dairlib