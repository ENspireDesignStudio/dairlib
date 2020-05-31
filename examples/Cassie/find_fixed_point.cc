#include <chrono>
#include <gflags/gflags.h>

#include "examples/Cassie/cassie_utils.h"
#include "multibody/kinematic/kinematic_evaluator_set.h"
#include "multibody/kinematic/world_point_evaluator.h"
#include "multibody/multibody_solvers.h"
#include "multibody/multibody_utils.h"
#include "multibody/multipose_visualizer.h"
#include "solvers/constraint_factory.h"

#include "drake/common/text_logging.h"
#include "drake/multibody/parsing/parser.h"
#include "drake/solvers/solve.h"
#include "drake/solvers/snopt_solver.h"

DEFINE_double(height, 1, "Fixed height");
DEFINE_double(mu, .5, "Coefficient of friction");
DEFINE_bool(linear_friction_cone, true, "Use linear or nonlinear Lorentz cone");

namespace dairlib {

int do_main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  srand((unsigned int) time(0));
  drake::logging::set_log_level("err");  // ignore warnings about joint limits

  std::string urdf = "examples/Cassie/urdf/cassie_fixed_springs.urdf";

  // Build plant
  drake::multibody::MultibodyPlant<double> plant(0);
  drake::multibody::MultibodyPlant<double> plant2(0);
  drake::multibody::Parser parser(&plant);
  std::string full_name = FindResourceOrThrow(urdf);
  parser.AddModelFromFile(full_name);
  plant.mutable_gravity_field().set_gravity_vector(-9.81 *
                                                   Eigen::Vector3d::UnitZ());
  plant.Finalize();

  multibody::KinematicEvaluatorSet<double> evaluators(plant);

  // Add loop closures
  auto left_loop = LeftLoopClosureEvaluator(plant);
  auto right_loop = RightLoopClosureEvaluator(plant);
  evaluators.add_evaluator(&left_loop);
  evaluators.add_evaluator(&right_loop);

  // Add contact points
  auto left_toe = LeftToe(plant);
  auto left_toe_evaluator = multibody::WorldPointEvaluator(plant,
      left_toe.first, left_toe.second, Eigen::Vector3d(0,0,1));
  evaluators.add_evaluator(&left_toe_evaluator);

  auto right_toe = RightToe(plant);
  auto right_toe_evaluator = multibody::WorldPointEvaluator(plant,
      right_toe.first, right_toe.second, Eigen::Vector3d(0,0,1));
  evaluators.add_evaluator(&right_toe_evaluator);

  auto left_heel = LeftHeel(plant);
  auto left_heel_evaluator = multibody::WorldPointEvaluator(plant,
      left_heel.first, left_heel.second, Eigen::Vector3d(0,0,1));
  evaluators.add_evaluator(&left_heel_evaluator);

  auto right_heel = RightHeel(plant);
  auto right_heel_evaluator = multibody::WorldPointEvaluator(plant,
      right_heel.first, right_heel.second, Eigen::Vector3d(0,0,1));
  evaluators.add_evaluator(&right_heel_evaluator);

  auto program = multibody::MultibodyProgram(plant, evaluators);

  auto positions_map = multibody::makeNameToPositionsMap(plant);
  auto q = program.AddPositionVariables();
  auto u = program.AddInputVariables();
  auto lambda = program.AddConstraintForceVariables();
  auto kinematic_constraint = program.AddKinematicConstraint(q);
  auto fp_constraint = program.AddFixedPointConstraint(q, u, lambda);
  program.AddJointLimitConstraints(q);

  // Fix floating base
  program.AddConstraint(q(positions_map.at("base_qw")) == 1);
  program.AddConstraint(q(positions_map.at("base_qx")) == 0);
  program.AddConstraint(q(positions_map.at("base_qy")) == 0);
  program.AddConstraint(q(positions_map.at("base_qz")) == 0);

  program.AddConstraint(q(positions_map.at("base_x")) == 0);
  program.AddConstraint(q(positions_map.at("base_y")) == 0);
  program.AddConstraint(q(positions_map.at("base_z")) == FLAGS_height);

  // Add symmetry constraints, and zero roll/pitch on the hip
  program.AddConstraint(q(positions_map.at("knee_left")) ==
      q(positions_map.at("knee_right")));
  program.AddConstraint(q(positions_map.at("hip_pitch_left")) ==
      q(positions_map.at("hip_pitch_right")));
  program.AddConstraint(q(positions_map.at("hip_roll_left")) == 0);
  program.AddConstraint(q(positions_map.at("hip_roll_right")) == 0);
  program.AddConstraint(q(positions_map.at("hip_yaw_right")) == 0);
  program.AddConstraint(q(positions_map.at("hip_yaw_left")) == 0);

  // Add some contact force constraints: linear version
  double mu = FLAGS_mu;
  if (FLAGS_linear_friction_cone) {
    int num_linear_faces = 40; // try lots of faces!
    program.AddConstraint(solvers::CreateLinearFrictionConstraint(mu,
        num_linear_faces), lambda.segment(2, 3));
    program.AddConstraint(solvers::CreateLinearFrictionConstraint(mu,
        num_linear_faces), lambda.segment(5, 3));
    program.AddConstraint(solvers::CreateLinearFrictionConstraint(mu,
        num_linear_faces), lambda.segment(8, 3));
    program.AddConstraint(solvers::CreateLinearFrictionConstraint(mu,
        num_linear_faces), lambda.segment(11, 3));
  } else {
    // Add some contact force constraints: Lorentz version
    program.AddConstraint(solvers::CreateConicFrictionConstraint(mu),
        lambda.segment(2, 3));
    program.AddConstraint(solvers::CreateConicFrictionConstraint(mu),
        lambda.segment(5, 3));
    program.AddConstraint(solvers::CreateConicFrictionConstraint(mu),
        lambda.segment(8, 3));
    program.AddConstraint(solvers::CreateConicFrictionConstraint(mu),
        lambda.segment(11, 3));
  }
 
  // Add minimum normal forces on all contact points
  program.AddConstraint(lambda(4) >= 20);
  program.AddConstraint(lambda(7) >= 20);
  program.AddConstraint(lambda(10) >= 20);
  program.AddConstraint(lambda(13) >= 20);

  // Set initial guess/cost for q using a vaguely neutral position
  Eigen::VectorXd q_guess = Eigen::VectorXd::Zero(plant.num_positions());
  q_guess(0) = 1; //quaternion
  q_guess(positions_map.at("base_z")) = FLAGS_height;
  q_guess(positions_map.at("hip_pitch_left")) = 1;
  q_guess(positions_map.at("knee_left")) = -2;
  q_guess(positions_map.at("ankle_joint_left")) = 2;
  q_guess(positions_map.at("toe_left")) = -2;
  q_guess(positions_map.at("hip_pitch_right")) = 1;
  q_guess(positions_map.at("knee_right")) = -2;
  q_guess(positions_map.at("ankle_joint_right")) = 2;
  q_guess(positions_map.at("toe_right")) = -2;

  // Perturb positions
  q_guess += .1*Eigen::VectorXd::Random(plant.num_positions());

  // Only cost in this program: u^T u
  program.AddQuadraticCost(u.dot(1.0 * u));

  // Random guess, except for the positions
  Eigen::VectorXd guess = Eigen::VectorXd::Random(program.num_vars());
  guess.head(plant.num_positions()) = q_guess;

  auto visualizer = multibody::MultiposeVisualizer(urdf, 1);

  // Draw initial guess
  visualizer.DrawPoses(q_guess);

  auto start = std::chrono::high_resolution_clock::now();
  const auto result = drake::solvers::Solve(program, guess);
       auto finish = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = finish - start;
  std::cout << "Solve time:" << elapsed.count() << std::endl;

  std::cout << to_string(result.get_solution_result()) << std::endl;
  std::cout << "Cost:" << result.get_optimal_cost() << std::endl;

  // Draw final pose
  visualizer.DrawPoses(result.GetSolution(q));

  return 0;
}

}  // namespace dairlib

int main(int argc, char* argv[]) { return dairlib::do_main(argc, argv); }