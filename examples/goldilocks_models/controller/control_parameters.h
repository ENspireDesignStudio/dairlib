#pragma once

namespace dairlib {
namespace goldilocks_models {

// The iteration # of the theta that you use. >=1
const int MODEL_ITER = 1;  // 30

const int LEFT_STANCE = 0;
const int RIGHT_STANCE = 1;
const int DOUBLE_STANCE = 2;
const int POST_LEFT_DOUBLE_STANCE = 3;
const int POST_RIGHT_DOUBLE_STANCE = 4;

const double LEFT_SUPPORT_DURATION = 0.4;//0.35;
const double RIGHT_SUPPORT_DURATION = 0.4;//0.35;
const double DOUBLE_SUPPORT_DURATION = 0.02;

const double STRIDE_LENGTH = 0.25;

}  // namespace goldilocks_models
}  // namespace dairlib