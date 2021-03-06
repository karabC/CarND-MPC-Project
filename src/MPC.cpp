#include "MPC.h"
#include <cppad/cppad.hpp>
#include <cppad/ipopt/solve.hpp>
#include "Eigen-3.3/Eigen/Core"

using CppAD::AD;

// TODO: Set the timestep length and duration
size_t N = 10;
double dt = 0.1;
int latency_steps = 0.1 / dt;

// Define the position of the x,y, psi, veclocity, cte, epsi, delta and acceleration
size_t x_start = 0;
size_t y_start = x_start + N;
size_t psi_start = y_start + N;
size_t v_start = psi_start + N;
size_t cte_start = v_start + N;
size_t epsi_start = cte_start + N;
size_t delta_start = epsi_start + N;
size_t a_start = delta_start + N - 1;
// This value assumes the model presented in the classroom is used.
//
// It was obtained by measuring the radius formed by running the vehicle in the
// simulator around in a circle with a constant steering angle and velocity on a
// flat terrain.
//
// Lf was tuned until the the radius formed by the simulating the model
// presented in the classroom matched the previous radius.
//
// This is the length from front to CoG that has a similar radius.
const double Lf = 2.67;

class FG_eval {
 public:
  // Ideal Error
  const double ref_cte = 0; // ideal cross-track error
  const double ref_epsi = 0; // ideal heading error
  double ref_v = 100; // The reference velocity in m/s.
  // Parameter of cost function
  const double para_cte = 2000;    // Parameter for Cross Tracking Error
  const double para_epsi = 2000;    // Parameter for Orientation Error
  const double para_vec = 1;    // Parameter for Reference Velocity Penalization
  const double para_steering = 5;    // Parameter for Steering Control Penalization
  const double para_acceleration = 5;    // Parameter for Accleartion Control Penalization
  const double para_seq_delta = 200;    // Parameter for Delta Steering Control Penalization
  const double para_seq_a = 10;    // Parameter for cost Delta Acceleration Control Penalization


  // Fitted polynomial coefficients
  Eigen::VectorXd coeffs;
  FG_eval(Eigen::VectorXd coeffs) { this->coeffs = coeffs; }

  typedef CPPAD_TESTVECTOR(AD<double>) ADvector;
  void operator()(ADvector& fg, const ADvector& vars) {
    // initialize fg[0] as 0. (The cost function)
    fg[0] = 0;

    // The part of the cost based on the reference state.
    for (int i = 0; i < N; i++) {
      fg[0] += para_cte * CppAD::pow(vars[cte_start + i] - ref_cte, 2);
      fg[0] += para_epsi * CppAD::pow(vars[epsi_start + i] - ref_epsi, 2);
      fg[0] += para_vec * CppAD::pow(vars[v_start + i] - ref_v, 2);
    }

    // Minimize the use of actuators.
    for (int i = 0; i < N - 1; i++) {
      fg[0] += para_steering * CppAD::pow(vars[delta_start + i], 2);
      fg[0] += para_acceleration * CppAD::pow(vars[a_start + i], 2);
    }

    // Minimize the value gap between sequential actuations.
    for (int i = 0; i < N - 2; i++) {
      fg[0] += para_seq_delta * CppAD::pow(vars[delta_start + i + 1] - vars[delta_start + i], 2);
      fg[0] += para_seq_a * CppAD::pow(vars[a_start + i + 1] - vars[a_start + i], 2);
    }

    // Initial constraints
    fg[1 + x_start] = vars[x_start];
    fg[1 + y_start] = vars[y_start];
    fg[1 + psi_start] = vars[psi_start];
    fg[1 + v_start] = vars[v_start];
    fg[1 + cte_start] = vars[cte_start];
    fg[1 + epsi_start] = vars[epsi_start];

    // The rest of the constraints
    for (int i = 0; i < N - 1; i++) {
      // The state at time t+1 .
      AD<double> x1 = vars[x_start + i + 1];
      AD<double> y1 = vars[y_start + i + 1];
      AD<double> psi1 = vars[psi_start + i + 1];
      AD<double> v1 = vars[v_start + i + 1];
      AD<double> cte1 = vars[cte_start + i + 1];
      AD<double> epsi1 = vars[epsi_start + i + 1];

      // The state at time t.
      AD<double> x0 = vars[x_start + i];
      AD<double> y0 = vars[y_start + i];
      AD<double> psi0 = vars[psi_start + i];
      AD<double> v0 = vars[v_start + i];
      AD<double> cte0 = vars[cte_start + i];
      AD<double> epsi0 = vars[epsi_start + i];

      // Only consider the actuation at time t.
      AD<double> delta0 = vars[delta_start + i];
      AD<double> a0 = vars[a_start + i];

      // find position on trajectory where we should be and ideal heading, based on polynomial path
      AD<double> f0 = 0.0;
      AD<double> psides0 = 0.0;
      if (coeffs.size() == 4) {
        // cubic polynomial
        f0 = coeffs[0] + x0 * (coeffs[1] + x0 * (coeffs[2] + x0 * coeffs[3]));
        psides0 = CppAD::atan(coeffs[1] + x0 * (2 * coeffs[2] + x0 * 3 * coeffs[3]));
      }
      else {
        // quadratic polynomial
        f0 = coeffs[0] + x0 * (coeffs[1] + x0 * (coeffs[2]));
        psides0 = CppAD::atan(coeffs[1] + x0 * (2 * coeffs[2]));
      }

      // Constraints. Implied by our motion model.
      // Forces the predicted motion path to be close to the polynomial.
      // IPOPT assumes we are driving constraints to zero.
      //
      // The equations for the model:
      // x_[t+1] = x[t] + v[t] * cos(psi[t]) * dt
      // y_[t+1] = y[t] + v[t] * sin(psi[t]) * dt
      // psi_[t+1] = psi[t] + v[t] / Lf * delta[t] * dt
      // v_[t+1] = v[t] + a[t] * dt
      // cte[t+1] = f(x[t]) - y[t] + v[t] * sin(epsi[t]) * dt
      // epsi[t+1] = psi[t] - psides[t] + v[t] * delta[t] / Lf * dt
      fg[2 + x_start + i] = x1 - (x0 + v0 * CppAD::cos(psi0) * dt);
      fg[2 + y_start + i] = y1 - (y0 + v0 * CppAD::sin(psi0) * dt);
      fg[2 + psi_start + i] = psi1 - (psi0 - v0 * delta0 / Lf * dt); // simulator steering angle/delta is positive to turn left, hence minus sign
      fg[2 + v_start + i] = v1 - (v0 + a0 * dt);
      fg[2 + cte_start + i] = cte1 - ((f0 - y0) + (v0 * CppAD::sin(epsi0) * dt));
      fg[2 + epsi_start + i] = epsi1 - (psi0 - psides0 - v0 * delta0 / Lf * dt); // again delta is different sign from motion equations
    }
  }
};

//
// MPC class definition implementation.
//
MPC::MPC() {}
MPC::~MPC() {}

vector<double> MPC::Solve(Eigen::VectorXd state, Eigen::VectorXd coeffs) {
  bool ok = true;
  size_t i;
  typedef CPPAD_TESTVECTOR(double) Dvector;

  // Set the number of model variables (includes both states and inputs).
  size_t n_vars = N * 6 + (N - 1) * 2;
  // Set the number of constraints
  size_t n_constraints = 6 * N;

  // Initial value of the independent variables.
  // SHOULD BE 0 besides initial state.
  Dvector vars(n_vars);
  for (int i = 0; i < n_vars; i++) {
    vars[i] = 0;
  }


  double x = state[0];    // Unit: m
  double y = state[1];    // Unit: m
  double psi = state[2];  // Unit: rad
  double v = state[3];    // Unit: m/s
  double cte = state[4];  // Unit: m
  double epsi = state[5]; // Unit: rad

  vars[x_start] = x;
  vars[y_start] = y;
  vars[psi_start] = psi;
  vars[v_start] = v;
  vars[cte_start] = cte;
  vars[epsi_start] = epsi;

  Dvector vars_lowerbound(n_vars);
  Dvector vars_upperbound(n_vars);

  // Set as nearly unlimit
  for (int i = 0; i < delta_start; i++) {
    vars_lowerbound[i] = -1.0e19;
    vars_upperbound[i] = 1.0e19;
  }

  // The upper and lower limits of steering angle should be set to  [-25 degree to 25 degree]
  double max_steering_angle = 25.0;
  for (int i = delta_start; i < a_start; i++) {
    vars_lowerbound[i] = -max_steering_angle * M_PI / 180;
    vars_upperbound[i] = max_steering_angle * M_PI / 180;
  }


  // The upper and lower limits for acceleeration [-1, 1]
  double max_acceleration = 1.0;
  for (int i = a_start; i < n_vars; i++) {
    vars_lowerbound[i] = -max_acceleration;
    vars_upperbound[i] = max_acceleration;
  }

  // Lower and upper limits for the constraints
  // Should be 0 besides initial state.
  Dvector constraints_lowerbound(n_constraints);
  Dvector constraints_upperbound(n_constraints);
  for (int i = 0; i < n_constraints; i++) {
    constraints_lowerbound[i] = 0;
    constraints_upperbound[i] = 0;
  }

  constraints_lowerbound[x_start] = x;
  constraints_lowerbound[y_start] = y;
  constraints_lowerbound[psi_start] = psi;
  constraints_lowerbound[v_start] = v;
  constraints_lowerbound[cte_start] = cte;
  constraints_lowerbound[epsi_start] = epsi;

  constraints_upperbound[x_start] = x;
  constraints_upperbound[y_start] = y;
  constraints_upperbound[psi_start] = psi;
  constraints_upperbound[v_start] = v;
  constraints_upperbound[cte_start] = cte;
  constraints_upperbound[epsi_start] = epsi;
  // object that computes objective and constraints
  FG_eval fg_eval(coeffs);

  //
  // NOTE: You don't have to worry about these options
  //
  // options for IPOPT solver
  std::string options;
  // Uncomment this if you'd like more print information
  options += "Integer print_level  0\n";
  // NOTE: Setting sparse to true allows the solver to take advantage
  // of sparse routines, this makes the computation MUCH FASTER. If you
  // can uncomment 1 of these and see if it makes a difference or not but
  // if you uncomment both the computation time should go up in orders of
  // magnitude.
  options += "Sparse  true        forward\n";
  options += "Sparse  true        reverse\n";
  // NOTE: Currently the solver has a maximum time limit of 0.5 seconds.
  // Change this as you see fit.
  options += "Numeric max_cpu_time          0.5\n";

  // place to return solution
  CppAD::ipopt::solve_result<Dvector> solution;

  // solve the problem
  CppAD::ipopt::solve<Dvector, FG_eval>(
      options, vars, vars_lowerbound, vars_upperbound, constraints_lowerbound,
      constraints_upperbound, fg_eval, solution);

  // Check some of the solution values
  ok &= solution.status == CppAD::ipopt::solve_result<Dvector>::success;

  // Cost
  auto cost = solution.obj_value;
  std::cout << "Cost " << cost << std::endl;

  // Solution: First two values: actuator values. 
  vector<double> sol = { solution.x[delta_start] , solution.x[a_start] };

  // Solution: Remained value - lines printing
  for (int i = 0; i < N - 1; i++)
  {
      sol.push_back(solution.x[x_start + i + 1]);
      sol.push_back(solution.x[y_start + i + 1]);
  }

  return sol;
}
