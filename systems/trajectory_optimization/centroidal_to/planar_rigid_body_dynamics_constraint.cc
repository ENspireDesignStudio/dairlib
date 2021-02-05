//
// Created by brian on 1/31/21.
//

#include "planar_rigid_body_dynamics_constraint.h"


using dairlib::solvers::NonlinearConstraint;

using drake::AutoDiffXd;

using Eigen::Matrix3d;
using Eigen::VectorXd;
using VectorX = drake::VectorX<AutoDiffXd>;
using Quat = drake::Quaternion<AutoDiffXd>;
using Vector2 = drake::Vector2<AutoDiffXd>;
using Vector1 = drake::Vector1<AutoDiffXd>;

namespace dairlib {
namespace centroidal_to {
namespace planar {
PlanarRigidBodyDynamicsConstraint::PlanarRigidBodyDynamicsConstraint(
    const double I,
    const double mass,
    const double h,
    const int n_c
) : NonlinearConstraint<AutoDiffXd>(kLinearVars + kAngularVars + n_c * kStanceVars,
                                    2*(kLinearVars + kAngularVars) + n_c * (kForceVars + 2 * kStanceVars),
                                    VectorXd::Zero(
                                        kLinearVars + kAngularVars + n_c * kStanceVars),
                                    VectorXd::Zero(
                                        kLinearVars + kAngularVars + n_c * kStanceVars)),
    I_(I),
    mass_(mass),
    n_c_(n_c),
    h_(h) {}

/// Variable Ordering: X0, X1 ,P_1, ... P_nc {F0 F1}_1, ... {F0 F1}_nc,

void PlanarRigidBodyDynamicsConstraint::EvaluateConstraint(
    const Eigen::Ref<const drake::VectorX<drake::AutoDiffXd>> &x,
    drake::VectorX<drake::AutoDiffXd> *y) const {

  int n_x = kLinearVars + kAngularVars + n_c_ * kStanceVars;
  VectorX x0 = x.head(n_x);
  VectorX x1 = x.segment(n_x, n_x);

  std::vector<Vector2> f0;
  std::vector<Vector2> fc;
  std::vector<Vector2> f1;

  for (int i = 0; i < n_c_; i++) {
    f0.push_back(x.segment(2 * n_x + kForceVars * i, kForceDim));
    f1.push_back(x.segment(2 * n_x + kForceVars * i + kForceDim, kForceDim));
    fc.push_back(0.5 * f0.back() + 0.5 * f1.back());
  }

  // compact form of collocation constraint
  VectorX F0 = F(x0, f0);
  VectorX F1 = F(x1, f1);
  VectorX xc = 0.5 * (x0 + x1) - (h_ / 8) * (F1 - F0);
  VectorX Fc = (3 / (2 * h_)) * (x1 - x0) - (1 / 4) * (F0 + F1);

  *y = Fc - F(xc, fc);
}

VectorX PlanarRigidBodyDynamicsConstraint::F(VectorX x, std::vector<Vector2> forces) const {
  Eigen::Vector2d g;
  g << 0, -9.81;

  Vector2 force_sum = Vector2::Zero();
  Vector1 pxf_sum = Vector1::Zero();
  for (int i = 0; i < n_c_; i++) {
    Vector2 p = x.tail(kStanceVars);
    force_sum += forces[i];
    pxf_sum += p.head(1)*forces[i].tail(1) - p.tail(1) * forces[i].head(1);
  }

  VectorX f = VectorX::Zero(kStateVars + kStanceVars * n_c_);

  f.head(kLinearDim + kAngularDim) = x.segment(kLinearDim + kAngularDim,
                                               kLinearDim + kAngularDim);

  f.segment(kLinearDim + kAngularDim, kLinearDim) = (1/mass_) * force_sum + g;
  f.segment(kStateVars - kAngularDim, kAngularDim) = (1/I_)*pxf_sum;

  for (int i = 0; i < n_c_; i++) {
    f.segment(kStateVars + kStanceVars*i, kStanceVars) = -1*x.segment(kLinearDim + kAngularDim, kLinearDim);
  }

  return f;
}

}
}
}