#include <drake/geometry/scene_graph.h>
#include <drake/multibody/parsing/parser.h>
#include <gflags/gflags.h>

#include "examples/Cassie/cassie_utils.h"
#include "lcm/dircon_saved_trajectory.h"
#include "lcm/lcm_trajectory.h"

#include "drake/multibody/plant/multibody_plant.h"

using drake::geometry::SceneGraph;
using drake::multibody::JacobianWrtVariable;
using drake::multibody::MultibodyPlant;
using drake::multibody::Parser;
using drake::systems::Context;
using drake::trajectories::PiecewisePolynomial;
using Eigen::Matrix3Xd;
using Eigen::MatrixXd;
using Eigen::Vector3d;
using Eigen::VectorXd;

DEFINE_string(trajectory_name, "",
              "File name where the optimal trajectory is stored.");
DEFINE_string(folder_path, "",
              "Folder path for where the trajectory names are stored");
DEFINE_int32(num_modes, 0, "Number of contact modes in the trajectory");
DEFINE_string(mode_name, "state_input_trajectory",
              "Base name of each trajectory");

namespace dairlib {

/// This program pre-computes the output trajectories (center of mass, pelvis
/// orientation, feet trajectories) for the OSC controller.
///

int DoMain() {
  // Drake system initialization
  drake::systems::DiagramBuilder<double> builder;
  SceneGraph<double>& scene_graph = *builder.AddSystem<SceneGraph>();
  scene_graph.set_name("scene_graph");
  MultibodyPlant<double> plant(1e-5);  // non-zero timestep to avoid continuous
  // model warnings
  Parser parser(&plant, &scene_graph);
  parser.AddModelFromFile(
      FindResourceOrThrow("examples/Cassie/urdf/cassie_fixed_springs.urdf"));
  plant.mutable_gravity_field().set_gravity_vector(-9.81 *
                                                   Eigen::Vector3d::UnitZ());
  plant.Finalize();

  std::unique_ptr<Context<double>> context = plant.CreateDefaultContext();

  int nq = plant.num_positions();
  int nv = plant.num_velocities();
  int nu = plant.num_actuators();
  int nx = plant.num_positions() + plant.num_velocities();

  auto l_toe_frame = &plant.GetBodyByName("toe_left").body_frame();
  auto r_toe_frame = &plant.GetBodyByName("toe_right").body_frame();
  auto world = &plant.world_frame();

  DirconTrajectory dircon_traj(FLAGS_folder_path + FLAGS_trajectory_name);
  PiecewisePolynomial<double> state_traj =
      dircon_traj.ReconstructStateTrajectory();

  VectorXd times = dircon_traj.GetBreaks();
  int n_points = times.size();

  std::cout << "knot points: " << n_points << std::endl;

  MatrixXd l_foot_points(6, n_points);
  MatrixXd r_foot_points(6, n_points);
  MatrixXd l_hip_points(6, n_points);
  MatrixXd r_hip_points(6, n_points);
  MatrixXd center_of_mass_points(6, n_points);
  MatrixXd pelvis_orientation(8, n_points);
  Vector3d zero_offset = Vector3d::Zero();

  for (unsigned int i = 0; i < times.size(); ++i) {
    VectorXd x_i = state_traj.value(times[i]);
    plant.SetPositionsAndVelocities(context.get(), x_i);
    center_of_mass_points.block(0, i, 3, 1) =
        plant.CalcCenterOfMassPosition(*context);
    Eigen::Ref<Eigen::MatrixXd> l_foot_pos_block =
        l_foot_points.block(0, i, 3, 1);
    Eigen::Ref<Eigen::MatrixXd> r_foot_pos_block =
        r_foot_points.block(0, i, 3, 1);
    plant.CalcPointsPositions(*context, *l_toe_frame, zero_offset, *world,
                              &l_foot_pos_block);
    plant.CalcPointsPositions(*context, *r_toe_frame, zero_offset, *world,
                              &r_foot_pos_block);

    pelvis_orientation.block(0, i, 4, 1) = x_i.head(4);
    pelvis_orientation.block(4, i, 4, 1) = x_i.segment(nq, 4);

    MatrixXd J_CoM(3, nv);
    MatrixXd J_l_foot(3, nv);
    MatrixXd J_r_foot(3, nv);
    //    MatrixXd J_pelvis_orientation(3, nv);
    plant.CalcJacobianCenterOfMassTranslationalVelocity(
        *context, JacobianWrtVariable::kV, *world, *world, &J_CoM);
    plant.CalcJacobianTranslationalVelocity(*context, JacobianWrtVariable::kV,
                                            *l_toe_frame, zero_offset, *world,
                                            *world, &J_l_foot);
    plant.CalcJacobianTranslationalVelocity(*context, JacobianWrtVariable::kV,
                                            *r_toe_frame, zero_offset, *world,
                                            *world, &J_r_foot);
    VectorXd v_i = x_i.tail(nv);
    center_of_mass_points.block(3, i, 3, 1) = J_CoM * v_i;
    l_foot_points.block(3, i, 3, 1) = J_l_foot * v_i;
    r_foot_points.block(3, i, 3, 1) = J_r_foot * v_i;
  }

  r_foot_points = r_foot_points - l_foot_points;
  l_foot_points = r_foot_points;
  l_foot_points.row(1) = -r_foot_points.row(1);
  l_foot_points.row(4) = -r_foot_points.row(4);

  auto lfoot_traj_block = LcmTrajectory::Trajectory();
  lfoot_traj_block.traj_name = "left_foot_trajectory";
  lfoot_traj_block.datapoints = l_foot_points;
  lfoot_traj_block.time_vector = times;
  lfoot_traj_block.datatypes = {"lfoot_x",    "lfoot_y",    "lfoot_z",
                                "lfoot_xdot", "lfoot_ydot", "lfoot_zdot"};

  auto rfoot_traj_block = LcmTrajectory::Trajectory();
  rfoot_traj_block.traj_name = "right_foot_trajectory";
  rfoot_traj_block.datapoints = r_foot_points;
  rfoot_traj_block.time_vector = times;
  rfoot_traj_block.datatypes = {"rfoot_x",    "rfoot_y",    "rfoot_z",
                                "rfoot_xdot", "rfoot_ydot", "rfoot_zdot"};

  auto com_traj_block = LcmTrajectory::Trajectory();
  com_traj_block.traj_name = "center_of_mass_trajectory";
  com_traj_block.datapoints = center_of_mass_points;
  com_traj_block.time_vector = times;
  com_traj_block.datatypes = {"com_x",    "com_y",    "com_z",
                              "com_xdot", "com_ydot", "com_zdot"};

  auto pelvis_orientation_block = LcmTrajectory::Trajectory();
  pelvis_orientation_block.traj_name = "pelvis_rot_trajectory";
  pelvis_orientation_block.datapoints = pelvis_orientation;
  pelvis_orientation_block.time_vector = times;
  pelvis_orientation_block.datatypes = {
      "pelvis_rotw",    "pelvis_rotx",    "pelvis_roty",    "pelvis_rotz",
      "pelvis_rotwdot", "pelvis_rotxdot", "pelvis_rotydot", "pelvis_rotzdot"};

  std::vector<LcmTrajectory::Trajectory> converted_trajectories = {
      lfoot_traj_block, rfoot_traj_block, com_traj_block,
      pelvis_orientation_block};
  std::vector<std::string> trajectory_names = {
      lfoot_traj_block.traj_name, rfoot_traj_block.traj_name,
      com_traj_block.traj_name, pelvis_orientation_block.traj_name};

  auto processed_traj = LcmTrajectory(converted_trajectories, trajectory_names,
                                      "walking_trajectory",
                                      "Output trajectories "
                                      "for Cassie walking");

  processed_traj.WriteToFile(FLAGS_folder_path + FLAGS_trajectory_name +
                             "_processed");
  return 0;
}

}  // namespace dairlib

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  dairlib::DoMain();
}