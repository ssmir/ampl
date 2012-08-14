/*
 Tests of the AMPL bindings for GNU Scientific Library.

 Copyright (C) 2012 AMPL Optimization LLC

 Permission to use, copy, modify, and distribute this software and its
 documentation for any purpose and without fee is hereby granted,
 provided that the above copyright notice appear in all copies and that
 both that the copyright notice and this permission notice and warranty
 disclaimer appear in supporting documentation.

 The author and AMPL Optimization LLC disclaim all warranties with
 regard to this software, including all implied warranties of
 merchantability and fitness.  In no event shall the author be liable
 for any special, indirect or consequential damages or any damages
 whatsoever resulting from loss of use, data or profits, whether in an
 action of contract, negligence or other tortious action, arising out
 of or in connection with the use or performance of this software.

 Author: Victor Zverovich
 */

#include <gsl/gsl_math.h>
#include <gsl/gsl_sf.h>

#include <functional>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cstring>

#include "gtest/gtest.h"
#include "tests/config.h"
// #define DEBUG_DIFFERENTIATOR
#include "tests/function.h"
#include "solvers/asl.h"

using std::string;
using std::vector;

using fun::BitSet;
using fun::DERIVS;
using fun::DerivativeBinder;
using fun::Differentiator;
using fun::Function;
using fun::FunctionInfo;
using fun::HES;
using fun::Tuple;
using fun::Type;

namespace {

// Converts error estimate returned by Diff into an absolute tolerance to
// be used in EXPECT_NEAR.
double ConvertErrorToTolerance(double error) {
  return error != 0 ? error * 1000 : 1e-10;
}

class Error {
 private:
  string str_;

 public:
  explicit Error(const string &s) : str_(s) {}
  operator const char*() const { return str_.c_str(); }
  const char* c_str() const { return str_.c_str(); }
  operator FunctionInfo::Result() const {
    return FunctionInfo::Result(str_.c_str());
  }
};

Error EvalError(const Function &f, const Tuple &args, const char *suffix = "") {
  std::ostringstream os;
  os << "can't evaluate " << f.name() << suffix << args;
  return Error(os.str());
}

Error NotIntError(const string &arg_name, double value = 0.5) {
  std::ostringstream os;
  os << "argument '" << arg_name
      << "' can't be represented as int, " << arg_name << " = " << value;
  return Error(os.str());
}

Error NotUIntError(const string &arg_name, double value) {
  std::ostringstream os;
  os << "argument '" << arg_name
      << "' can't be represented as unsigned int, " << arg_name << " = " << value;
  return Error(os.str());
}

struct NoDerivativeInfo : FunctionInfo {
  Result GetDerivative(const Function &, unsigned, const Tuple &) {
    return Result("derivatives are not provided");
  }
};

#define EXPECT_ERROR(expected_message, result) \
  EXPECT_STREQ(expected_message, (result).error())

// Check if the value returned by af is correct.
void CheckFunction(double value, const Function &f, const Tuple &args) {
  std::ostringstream os;
  os << "Checking if " << f.name() << args << " = " << value;
  SCOPED_TRACE(os.str());
  if (gsl_isnan(value))
    EXPECT_ERROR(EvalError(f, args), f(args));
  else
    EXPECT_EQ(value, f(args)) << f.name() << args;
}

typedef double (*FuncND)(int, double);

const double POINTS[] = {-5, -2, -1.23, -1, 0, 1, 1.23, 2, 5};
const size_t NUM_POINTS = sizeof(POINTS) / sizeof(*POINTS);

const double POINTS_FOR_N[] = {-2, -1, 0, 1, 2};
const size_t NUM_POINTS_FOR_N = sizeof(POINTS_FOR_N) / sizeof(*POINTS_FOR_N);

class GSLTest : public ::testing::Test {
 protected:
  ASL *asl;
  FunctionInfo info;  // Default function info.

  Differentiator diff;

  // Differentiator statistics.
  struct Stats {
    unsigned num_calls_;
    unsigned num_nans_;
    double min_error_;
    double max_error_;

    Stats() :
      num_calls_(0), num_nans_(0),
      min_error_(std::numeric_limits<double>::max()),
      max_error_(std::numeric_limits<double>::min()) {}

    ~Stats() {
      std::cout << "Called numerical differentiation "
          << num_calls_ << " times with " << num_nans_ << " "
          << (num_nans_ == 1 ? "NaN" : "NaNs")
          << " detected" << std::endl;
      std::cout << "Error min=" << min_error_;
      std::cout << "  max=" << max_error_ << std::endl;
    }
  };
  static Stats stats_;

  template <typename F>
  double Diff(F f, double x, double *error = 0);

  void SetUp() {
    asl = ASL_alloc(ASL_read_f);
    i_option_ASL = "../solvers/gsl/libamplgsl.so";
    func_add(asl);
  }

  void TearDown() {
    ASL_free(&asl);
  }

  // Returns an AMPL function by name.
  Function GetFunction(const char *name, FunctionInfo *info = 0) const {
    func_info *fi = func_lookup(asl, name, 0);
    if (!fi)
      throw std::runtime_error(string("function not found: ") + name);
    return Function(asl, fi, info);
  }

  template <typename F>
  bool CheckDerivative(F f, const Function &af,
      unsigned var_index, const Tuple &args);

  static const unsigned NO_VAR = ~0u;

  void CheckSecondDerivatives(const Function &f,
      const Tuple &args, unsigned skip_var = NO_VAR);

  // Tests a function taking an integer and a double parameter.
  // test_x is a value of x where the function can be computed for very large
  // and very small n. If there is no such x or it is not known, then test_x
  // should be GSL_NAN.
  void TestFuncND(const Function &af, FuncND f,
      double test_x, const string &arg_name);

  template <typename F>
  void TestFunc(const Function &af, F f, Tuple &args, unsigned arg_index);

  template <typename F>
  void TestFunc(const Function &af, F f);
};

GSLTest::Stats GSLTest::stats_;

template <typename F>
double GSLTest::Diff(F f, double x, double *error) {
  ++stats_.num_calls_;
  double err = GSL_NAN;
  bool detected_nan = false;
  double deriv = diff(f, x, &err, &detected_nan);
  if (!gsl_isnan(deriv)) {
    stats_.min_error_ = std::min(stats_.min_error_, err);
    stats_.max_error_ = std::max(stats_.max_error_, err);
  }
  if (detected_nan)
    ++stats_.num_nans_;
  if (error)
    *error = err;
  return deriv;
}

// Checks if the value of the derivative returned by af agrees with the
// value returned by numerical differentiation of function f.
// var_index: index of the variable with respect to which to differentiate
// args: point at which the derivative is computed
template <typename F>
bool GSLTest::CheckDerivative(F f, const Function &af,
    unsigned var_index, const Tuple &args) {
  std::ostringstream os;
  os << "Checking d/dx" << var_index << " " << af.name() << " at " << args;
  SCOPED_TRACE(os.str());

  BitSet use_deriv(args.size(), false);
  use_deriv[var_index] = true;

  double error = 0;
  double x = args[var_index];
  FunctionInfo::Result deriv_result = af.GetDerivative(var_index, args);
  if (!deriv_result.error()) {
    double numerical_deriv = Diff(f, x, &error);
    double overridden_deriv = deriv_result.value();
    if (!gsl_isnan(overridden_deriv) && overridden_deriv != numerical_deriv) {
      std::cout << "Overriding d/dx" << var_index << " " << af.name()
        << " at " << args << ", computed = " << numerical_deriv
        << ", overridden = " << overridden_deriv << std::endl;
      numerical_deriv = overridden_deriv;
    }
    if (!gsl_isnan(numerical_deriv)) {
      double deriv = af(args, DERIVS, use_deriv).deriv(var_index);
      if (numerical_deriv != deriv)
        EXPECT_NEAR(numerical_deriv, deriv, ConvertErrorToTolerance(error));
      return true;
    }
  }

  Function::Result r = af(args, DERIVS, use_deriv);
  if (!gsl_isnan(f(x))) {
    if (deriv_result.error())
      EXPECT_ERROR(deriv_result.error(), r);
    else
      EXPECT_ERROR(EvalError(af, args, "'"), r);
  } else {
    EXPECT_TRUE(r.error() != nullptr);
  }
  return false;
}

// Checks if the values of the second partial derivatives returned by af
// agree with the values returned by numerical differentiation of the first
// partial derivatives.
// args: point at which the derivatives are computed
// skip_var: index of the variable with respect to which not to differentiate
void GSLTest::CheckSecondDerivatives(const Function &f,
    const Tuple &args, unsigned skip_var) {
  unsigned num_args = args.size();
  if (skip_var == NO_VAR) {
    for (unsigned i = 0; i < num_args; ++i) {
      if (f.GetDerivative(i, args).error()) {
        skip_var = i;
        break;
      }
    }
  }
  for (unsigned i = 0; i < num_args; ++i) {
    if (i == skip_var) continue;
    for (unsigned j = 0; j < num_args; ++j) {
      if (j == skip_var) continue;
      BitSet use_deriv(num_args, false);
      use_deriv[i] = true;
      use_deriv[j] = true;
      double error = 0;
      FunctionInfo::Result deriv_result = f.GetSecondDerivative(i, j, args);
      if (!deriv_result.error()) {
        double d = Diff(DerivativeBinder(f, j, i, args), args[i], &error);
        double overridden_deriv = deriv_result.value();
        if (!gsl_isnan(overridden_deriv) && overridden_deriv != d) {
          std::cout << "Overriding d/dx" << i << " d/dx" << j << " "
            << f.name() << " at " << args << ", computed = " << d
            << ", overridden = " << overridden_deriv << std::endl;
          d = overridden_deriv;
        }
        std::ostringstream os;
        os << "Checking if d/dx" << i << " d/dx" << j
          << " " << f.name() << " at " << args << " is " << d;
        SCOPED_TRACE(os.str());
        if (!gsl_isnan(d)) {
          unsigned ii = i, jj = j;
          if (ii > jj) std::swap(ii, jj);
          unsigned hes_index = ii * (2 * num_args - ii - 1) / 2 + jj;
          double actual_deriv = f(args, HES, use_deriv).hes(hes_index);
          if (d != actual_deriv)
            EXPECT_NEAR(d, actual_deriv, ConvertErrorToTolerance(error));
          continue;
        }
      }
      Function::Result r = f(args, HES, use_deriv);
      if (f(args, DERIVS, use_deriv).error())
        EXPECT_TRUE(r.error() != nullptr);
      else if (deriv_result.error())
        EXPECT_ERROR(deriv_result.error(), r);
      else
        EXPECT_ERROR(EvalError(f, args, "''"), r);
    }
  }
}

void GSLTest::TestFuncND(const Function &af, FuncND f,
    double test_x, const string &arg_name) {
  for (size_t i = 0; i != NUM_POINTS_FOR_N; ++i) {
    int n = POINTS_FOR_N[i];
    for (size_t j = 0; j != NUM_POINTS; ++j) {
      double x = POINTS[j];
      Tuple args(n, x);
      CheckFunction(f(n, x), af, args);
      string error("argument '" + arg_name + "' is not constant");
      EXPECT_ERROR(error.c_str(), af(args, DERIVS));
      EXPECT_ERROR(error.c_str(), af(args, HES));
      CheckDerivative(std::bind1st(std::ptr_fun(f), n), af, 1, args);
      CheckSecondDerivatives(af, args, 0);
    }
  }
  EXPECT_ERROR(("argument '" + arg_name + "' can't be represented as int, " +
      arg_name + " = 0.5").c_str(), af(Tuple(0.5, 0)));

  if (gsl_isnan(test_x))
    return;

  // These points are tested separately because of various problems, e.g.
  // gsl_sf_bessel_Jn(n, x) and gsl_sf_bessel_In(n, x) take too much time
  // (hang?) for n = INT_MIN and gsl_sf_bessel_Yn(n, x) returns different
  // values close to 0 when called different times for n = INT_MIN.

  BitSet use_deriv("01");
  EXPECT_ERROR(
      ("can't compute derivative: argument '" + arg_name + "' too small, " +
      arg_name + " = -2147483648").c_str(),
      af(Tuple(INT_MIN, test_x), DERIVS, use_deriv));
  EXPECT_ERROR(
      ("can't compute derivative: argument '" + arg_name + "' too large, " +
      arg_name + " = 2147483647").c_str(),
      af(Tuple(INT_MAX, test_x), DERIVS, use_deriv));
  EXPECT_TRUE(!gsl_isnan(af(Tuple(INT_MIN + 1, test_x), DERIVS,
      use_deriv).deriv(1)));
  EXPECT_TRUE(!gsl_isnan(af(Tuple(INT_MAX - 1, test_x), DERIVS,
      use_deriv).deriv(1)));

  EXPECT_ERROR(
      ("can't compute derivative: argument '" + arg_name + "' too small, " +
      arg_name + " = -2147483647").c_str(),
      af(Tuple(INT_MIN + 1, test_x), HES, use_deriv));
  EXPECT_ERROR(
      ("can't compute derivative: argument '" + arg_name + "' too large, " +
      arg_name + " = 2147483646").c_str(),
      af(Tuple(INT_MAX - 1, test_x), HES, use_deriv));
  EXPECT_TRUE(!gsl_isnan(af(Tuple(INT_MIN + 2, test_x), HES,
      use_deriv).hes(2)));
  EXPECT_TRUE(!gsl_isnan(af(Tuple(INT_MAX - 2, test_x), HES,
      use_deriv).hes(2)));
}

// Checks that the error is returned when trying to get a derivative
// with respect to an integer argument.
// Returns true if the argument is integer, false otherwise.
bool CheckArg(const Function &f,
    const Tuple &args, Type arg_type, unsigned arg_index) {
  if (arg_type == fun::DOUBLE)
    return false;
  std::ostringstream os;
  BitSet use_deriv(args.size(), false);
  use_deriv[arg_index] = true;
  os << "argument '" << f.GetArgName(arg_index) << "' is not constant";
  EXPECT_STREQ(os.str().c_str(), f(args, DERIVS, use_deriv).error());
  return true;
}

template <typename F>
void GSLTest::TestFunc(
    const Function &af, F f, Tuple &args, unsigned arg_index) {
  unsigned num_args = args.size();
  if (arg_index < num_args) {
    for (size_t i = 0; i != NUM_POINTS; ++i) {
      args[arg_index] = POINTS[i];
      TestFunc(af, f, args, arg_index + 1);
    }
    return;
  }
  for (unsigned i = 0; i < num_args; ++i) {
    if (f.GetArgType(i) == fun::INT &&
        static_cast<int>(args[i]) != args[i]) {
      EXPECT_STREQ(NotIntError(af.GetArgName(i), args[i]), af(args).error());
      return;
    }
    if (f.GetArgType(i) == fun::UINT &&
        static_cast<unsigned>(args[i]) != args[i]) {
      EXPECT_STREQ(NotUIntError(af.GetArgName(i), args[i]), af(args).error());
      return;
    }
  }
  CheckFunction(f(args), af, args);
  for (unsigned i = 0; i < num_args; ++i) {
    if (!CheckArg(af, args, f.GetArgType(i), i))
      CheckDerivative(BindAllButOne(f, args, i), af, i, args);
  }
  CheckSecondDerivatives(af, args);
}

template <typename F>
void GSLTest::TestFunc(const Function &af, F f) {
  unsigned num_args = fun::FunctionPointer(f).GetNumArgs();
  if (af.nargs() == static_cast<int>(num_args) - 1 &&
      fun::FunctionPointer(f).GetArgType(num_args - 1) == fun::UINT) {
    // If the last argument is a mode bind it to GSL_PREC_DOUBLE.
    Tuple args(Tuple::GetTupleWithSize(num_args - 1));
    TestFunc(af, BindOne(fun::FunctionPointer(f),
        GSL_PREC_DOUBLE, num_args - 1), args, 0);
  } else {
    Tuple args(Tuple::GetTupleWithSize(num_args));
    TestFunc(af, fun::FunctionPointer(f), args, 0);
  }
}

#define TEST_FUNC(name) TestFunc(GetFunction(#name, &info), name)

#define TEST_FUNC_ND(name, test_x, arg) \
  TestFuncND(GetFunction("gsl_" #name, &info), gsl_##name, test_x, #arg)

TEST_F(GSLTest, Elementary) {
  TEST_FUNC(gsl_log1p);
  TEST_FUNC(gsl_expm1);
  TEST_FUNC(gsl_hypot);
  TEST_FUNC(gsl_hypot3);
}

TEST_F(GSLTest, AiryA) {
  TEST_FUNC(gsl_sf_airy_Ai);
  TEST_FUNC(gsl_sf_airy_Ai_scaled);
}

TEST_F(GSLTest, AiryB) {
  TEST_FUNC(gsl_sf_airy_Bi);
  TEST_FUNC(gsl_sf_airy_Bi_scaled);
}

TEST_F(GSLTest, AiryZero) {
  FunctionInfo info;
  info.SetArgNames("s");
  TEST_FUNC(gsl_sf_airy_zero_Ai);
  TEST_FUNC(gsl_sf_airy_zero_Bi);
  TEST_FUNC(gsl_sf_airy_zero_Ai_deriv);
  TEST_FUNC(gsl_sf_airy_zero_Bi_deriv);
}

TEST_F(GSLTest, BesselJ) {
  TEST_FUNC(gsl_sf_bessel_J0);
  TEST_FUNC(gsl_sf_bessel_J1);
  TEST_FUNC_ND(sf_bessel_Jn, 0, n);
}

TEST_F(GSLTest, BesselY) {
  TEST_FUNC(gsl_sf_bessel_Y0);
  TEST_FUNC(gsl_sf_bessel_Y1);
  TEST_FUNC_ND(sf_bessel_Yn, 1, n);
}

TEST_F(GSLTest, BesselI) {
  TEST_FUNC(gsl_sf_bessel_I0);
  TEST_FUNC(gsl_sf_bessel_I1);
  TEST_FUNC_ND(sf_bessel_In, 0, n);
  TEST_FUNC(gsl_sf_bessel_I0_scaled);
  TEST_FUNC(gsl_sf_bessel_I1_scaled);
  TEST_FUNC_ND(sf_bessel_In_scaled, 0, n);
}

TEST_F(GSLTest, BesselK) {
  TEST_FUNC(gsl_sf_bessel_K0);
  TEST_FUNC(gsl_sf_bessel_K1);
  TEST_FUNC_ND(sf_bessel_Kn, 1, n);
  TEST_FUNC(gsl_sf_bessel_K0_scaled);
  TEST_FUNC(gsl_sf_bessel_K1_scaled);
  TEST_FUNC_ND(sf_bessel_Kn_scaled, 1, n);
}

TEST_F(GSLTest, Besselj) {
  TEST_FUNC(gsl_sf_bessel_j0);
  TEST_FUNC(gsl_sf_bessel_j1);
  TEST_FUNC(gsl_sf_bessel_j2);
  TEST_FUNC_ND(sf_bessel_jl, GSL_NAN, l);
}

TEST_F(GSLTest, Bessely) {
  TEST_FUNC(gsl_sf_bessel_y0);
  TEST_FUNC(gsl_sf_bessel_y1);
  TEST_FUNC(gsl_sf_bessel_y2);
  TEST_FUNC_ND(sf_bessel_yl, GSL_NAN, l);
}

TEST_F(GSLTest, Besseli) {
  TEST_FUNC(gsl_sf_bessel_i0_scaled);
  TEST_FUNC(gsl_sf_bessel_i1_scaled);
  TEST_FUNC(gsl_sf_bessel_i2_scaled);
  TEST_FUNC_ND(sf_bessel_il_scaled, GSL_NAN, l);
}

TEST_F(GSLTest, Besselk) {
  TEST_FUNC(gsl_sf_bessel_k0_scaled);
  TEST_FUNC(gsl_sf_bessel_k1_scaled);
  TEST_FUNC(gsl_sf_bessel_k2_scaled);
  TEST_FUNC_ND(sf_bessel_kl_scaled, GSL_NAN, l);
}

struct BesselFractionalOrderInfo : FunctionInfo {
  Result GetDerivative(const Function &f,
      unsigned var_index, const Tuple &args) {
    // Partial derivative with respect to nu is not provided.
    if (var_index == 0)
      return Result("argument 'nu' is not constant");
    // Computing gsl_sf_bessel_*nu'(nu, x) requires
    // gsl_sf_bessel_*nu(nu - 1, x) which doesn't work when the
    // first argument is non-negative, so nu should be >= 1.
    if (args[0] < 1)
      return EvalError(f, args, "'");
    return Result();
  }

  Result GetSecondDerivative(
      const Function &f, unsigned, unsigned, const Tuple &args) {
    // Computing gsl_sf_bessel_*nu''(nu, x) requires
    // gsl_sf_bessel_*nu(nu - 2, x) which doesn't work when the
    // first argument is non-negative, so nu should be >= 2.
    return args[0] < 2 ? EvalError(f, args, "''") : Result();
  }
};

TEST_F(GSLTest, BesselFractionalOrder) {
  BesselFractionalOrderInfo info;
  TEST_FUNC(gsl_sf_bessel_Jnu);
  TEST_FUNC(gsl_sf_bessel_Ynu);
  TEST_FUNC(gsl_sf_bessel_Inu);
  TEST_FUNC(gsl_sf_bessel_Inu_scaled);
  TEST_FUNC(gsl_sf_bessel_Knu);
  TEST_FUNC(gsl_sf_bessel_lnKnu);
  TEST_FUNC(gsl_sf_bessel_Knu_scaled);
}

TEST_F(GSLTest, BesselZero) {
  NoDerivativeInfo info;
  info.SetArgNames("s");
  TEST_FUNC(gsl_sf_bessel_zero_J0);
  TEST_FUNC(gsl_sf_bessel_zero_J1);
  info.SetArgNames("nu s");
  TEST_FUNC(gsl_sf_bessel_zero_Jnu);
}

struct ClausenFunctionInfo : FunctionInfo {
  Result GetDerivative(const Function &, unsigned, const Tuple &args) {
    return Result(args[0] == 0 ? GSL_POSINF : GSL_NAN);
  }
};

TEST_F(GSLTest, Clausen) {
  ClausenFunctionInfo info;
  TEST_FUNC(gsl_sf_clausen);
}

TEST_F(GSLTest, Hydrogenic) {
  TEST_FUNC(gsl_sf_hydrogenicR_1);
  NoDerivativeInfo info;
  info.SetArgNames("n l z r");
  TEST_FUNC(gsl_sf_hydrogenicR);
}

double gsl_sf_coulomb_CL(double L, double eta) {
  gsl_sf_result result = {};
  return gsl_sf_coulomb_CL_e(L, eta, &result) ? GSL_NAN : result.val;
}

TEST_F(GSLTest, Coulomb) {
  NoDerivativeInfo info;
  TEST_FUNC(gsl_sf_coulomb_CL);
}

TEST_F(GSLTest, Coupling3j) {
  double value = gsl_sf_coupling_3j(8, 20, 12, -2, 12, -10);
  EXPECT_NEAR(0.0812695955, value, 1e-5);
  Function f = GetFunction("gsl_sf_coupling_3j");
  Tuple args(8, 20, 12, -2, 12, -10);
  EXPECT_EQ(value, f(args));
  f(Tuple(0, 0, 0, 0, 0, 0));
  EXPECT_ERROR(NotIntError("two_ja"), f(Tuple(0.5, 0, 0, 0, 0, 0)));
  EXPECT_ERROR(NotIntError("two_jb"), f(Tuple(0, 0.5, 0, 0, 0, 0)));
  EXPECT_ERROR(NotIntError("two_jc"), f(Tuple(0, 0, 0.5, 0, 0, 0)));
  EXPECT_ERROR(NotIntError("two_ma"), f(Tuple(0, 0, 0, 0.5, 0, 0)));
  EXPECT_ERROR(NotIntError("two_mb"), f(Tuple(0, 0, 0, 0, 0.5, 0)));
  EXPECT_ERROR(NotIntError("two_mc"), f(Tuple(0, 0, 0, 0, 0, 0.5)));
  const char *error = "argument 'two_ja' is not constant";
  EXPECT_ERROR(error, f(args, DERIVS));
  EXPECT_ERROR(error, f(args, HES));
}

TEST_F(GSLTest, Coupling6j) {
  double value = gsl_sf_coupling_6j(2, 4, 6, 8, 10, 12);
  EXPECT_NEAR(0.0176295295, value, 1e-7);
  Function f = GetFunction("gsl_sf_coupling_6j");
  Tuple args(2, 4, 6, 8, 10, 12);
  EXPECT_EQ(value, f(args));
  EXPECT_TRUE(f(Tuple(0, 0, 0, 0, 0, 0)).error() == nullptr);
  EXPECT_ERROR(NotIntError("two_ja"), f(Tuple(0.5, 0, 0, 0, 0, 0)));
  EXPECT_ERROR(NotIntError("two_jb"), f(Tuple(0, 0.5, 0, 0, 0, 0)));
  EXPECT_ERROR(NotIntError("two_jc"), f(Tuple(0, 0, 0.5, 0, 0, 0)));
  EXPECT_ERROR(NotIntError("two_jd"), f(Tuple(0, 0, 0, 0.5, 0, 0)));
  EXPECT_ERROR(NotIntError("two_je"), f(Tuple(0, 0, 0, 0, 0.5, 0)));
  EXPECT_ERROR(NotIntError("two_jf"), f(Tuple(0, 0, 0, 0, 0, 0.5)));
  const char *error = "argument 'two_ja' is not constant";
  EXPECT_ERROR(error, f(args, DERIVS));
  EXPECT_ERROR(error, f(args, HES));
}

TEST_F(GSLTest, Coupling9j) {
  double value = gsl_sf_coupling_9j(6, 16, 18, 8, 20, 14, 12, 10, 4);
  EXPECT_NEAR(-0.000775648399, value, 1e-9);
  Function f = GetFunction("gsl_sf_coupling_9j");
  Tuple args(6, 16, 18, 8, 20, 14, 12, 10, 4);
  EXPECT_EQ(value, f(args));
  EXPECT_TRUE(f(Tuple(0, 0, 0, 0, 0, 0, 0, 0, 0)).error() == nullptr);
  EXPECT_ERROR(NotIntError("two_ja"), f(Tuple(0.5, 0, 0, 0, 0, 0, 0, 0, 0)));
  EXPECT_ERROR(NotIntError("two_jb"), f(Tuple(0, 0.5, 0, 0, 0, 0, 0, 0, 0)));
  EXPECT_ERROR(NotIntError("two_jc"), f(Tuple(0, 0, 0.5, 0, 0, 0, 0, 0, 0)));
  EXPECT_ERROR(NotIntError("two_jd"), f(Tuple(0, 0, 0, 0.5, 0, 0, 0, 0, 0)));
  EXPECT_ERROR(NotIntError("two_je"), f(Tuple(0, 0, 0, 0, 0.5, 0, 0, 0, 0)));
  EXPECT_ERROR(NotIntError("two_jf"), f(Tuple(0, 0, 0, 0, 0, 0.5, 0, 0, 0)));
  EXPECT_ERROR(NotIntError("two_jg"), f(Tuple(0, 0, 0, 0, 0, 0, 0.5, 0, 0)));
  EXPECT_ERROR(NotIntError("two_jh"), f(Tuple(0, 0, 0, 0, 0, 0, 0, 0.5, 0)));
  EXPECT_ERROR(NotIntError("two_ji"), f(Tuple(0, 0, 0, 0, 0, 0, 0, 0, 0.5)));
  const char *error = "argument 'two_ja' is not constant";
  EXPECT_ERROR(error, f(args, DERIVS));
  EXPECT_ERROR(error, f(args, HES));
}

TEST_F(GSLTest, Dawson) {
  TEST_FUNC(gsl_sf_dawson);
}

TEST_F(GSLTest, Debye) {
  TEST_FUNC(gsl_sf_debye_1);
  TEST_FUNC(gsl_sf_debye_2);
  TEST_FUNC(gsl_sf_debye_3);
  TEST_FUNC(gsl_sf_debye_4);
  TEST_FUNC(gsl_sf_debye_5);
  TEST_FUNC(gsl_sf_debye_6);
}

struct DilogFunctionInfo : FunctionInfo {
  Result GetDerivative(const Function &, unsigned, const Tuple &args) {
    return Result(args[0] == 1 ? GSL_POSINF : GSL_NAN);
  }
};

TEST_F(GSLTest, Dilog) {
  DilogFunctionInfo info;
  TEST_FUNC(gsl_sf_dilog);
}

TEST_F(GSLTest, EllInt) {
  TEST_FUNC(gsl_sf_ellint_Kcomp);
  TEST_FUNC(gsl_sf_ellint_Ecomp);
  TEST_FUNC(gsl_sf_ellint_Pcomp);
  TEST_FUNC(gsl_sf_ellint_F);
  TEST_FUNC(gsl_sf_ellint_E);
  {
    NoDerivativeInfo info;
    TEST_FUNC(gsl_sf_ellint_P);
    TEST_FUNC(gsl_sf_ellint_D);
    TEST_FUNC(gsl_sf_ellint_RC);
    TEST_FUNC(gsl_sf_ellint_RD);
    TEST_FUNC(gsl_sf_ellint_RF);
  }
  Function f = GetFunction("gsl_sf_ellint_RJ");
  for (size_t ix = 0; ix != NUM_POINTS; ++ix) {
    for (size_t iy = 0; iy != NUM_POINTS; ++iy) {
      for (size_t iz = 0; iz != NUM_POINTS; ++iz) {
        for (size_t ip = 0; ip != NUM_POINTS; ++ip) {
          double x = POINTS[ix], y = POINTS[iy];
          double z = POINTS[iz], p = POINTS[ip];
          Tuple args(x, y, z, p);
          CheckFunction(gsl_sf_ellint_RJ(x, y, z, p, GSL_PREC_DOUBLE), f, args);
          const char *error = "derivatives are not provided";
          EXPECT_ERROR(error, f(args, DERIVS));
          EXPECT_ERROR(error, f(args, HES));
        }
      }
    }
  }
}

TEST_F(GSLTest, Erf) {
  TEST_FUNC(gsl_sf_erf);
  TEST_FUNC(gsl_sf_erfc);
  TEST_FUNC(gsl_sf_log_erfc);
  TEST_FUNC(gsl_sf_erf_Z);
  TEST_FUNC(gsl_sf_erf_Q);
  TEST_FUNC(gsl_sf_hazard);
}

TEST_F(GSLTest, ExpInt) {
  TEST_FUNC(gsl_sf_expint_E1);
  TEST_FUNC(gsl_sf_expint_E2);
  TEST_FUNC_ND(sf_expint_En, GSL_NAN, n);
  TEST_FUNC(gsl_sf_expint_Ei);
  TEST_FUNC(gsl_sf_Shi);
  TEST_FUNC(gsl_sf_Chi);
  TEST_FUNC(gsl_sf_expint_3);
  TEST_FUNC(gsl_sf_Si);
  TEST_FUNC(gsl_sf_Ci);
  TEST_FUNC(gsl_sf_atanint);
}

TEST_F(GSLTest, FermiDirac) {
  TEST_FUNC(gsl_sf_fermi_dirac_m1);
  TEST_FUNC(gsl_sf_fermi_dirac_0);
  TEST_FUNC(gsl_sf_fermi_dirac_1);
  TEST_FUNC(gsl_sf_fermi_dirac_2);
  TEST_FUNC_ND(sf_fermi_dirac_int, GSL_NAN, j);
  {
    NoDerivativeInfo info;
    TEST_FUNC(gsl_sf_fermi_dirac_mhalf);
    TEST_FUNC(gsl_sf_fermi_dirac_half);
  }
  TEST_FUNC(gsl_sf_fermi_dirac_3half);
  TEST_FUNC(gsl_sf_fermi_dirac_inc_0);
}

struct LnGammaInfo : FunctionInfo {
  Result GetDerivative(const Function &af, unsigned , const Tuple &args) {
    double x = args[0];
    return x == -1 || x == -2 ? EvalError(af, args, "'") : Result();
  }
};

TEST_F(GSLTest, Gamma) {
  Function gamma = GetFunction("gsl_sf_gamma", &info);
  EXPECT_NEAR(-0.129354, gamma(Tuple(-0.5), DERIVS).deriv(), 1e-6);
  EXPECT_NEAR(-31.6778, gamma(Tuple(-0.5), HES).hes(), 1e-4);
  EXPECT_NEAR(1.19786e100, gamma(Tuple(71)), 1e95);
  EXPECT_TRUE(gsl_isinf(gamma(Tuple(1000))));
  TEST_FUNC(gsl_sf_gamma);
  {
    LnGammaInfo info;
    TEST_FUNC(gsl_sf_lngamma);
  }
  TEST_FUNC(gsl_sf_gammastar);
  TEST_FUNC(gsl_sf_gammainv);
}

TEST_F(GSLTest, Poch) {
  NoDerivativeInfo info;
  TEST_FUNC(gsl_sf_poch);
  TEST_FUNC(gsl_sf_lnpoch);
  TEST_FUNC(gsl_sf_pochrel);
}

struct GammaIncInfo : FunctionInfo {
  Result GetDerivative(
      const Function &af, unsigned var_index, const Tuple &args) {
    // Partial derivative with respect to a is not provided.
    if (var_index == 0)
      return Result("argument 'a' is not constant");
    if (args[1] == 0)
      return EvalError(af, args, "'");
    return Result();
  }
};

TEST_F(GSLTest, GammaInc) {
  {
    GammaIncInfo info;
    TEST_FUNC(gsl_sf_gamma_inc);
  }
  {
    NoDerivativeInfo info;
    TEST_FUNC(gsl_sf_gamma_inc_Q);
    TEST_FUNC(gsl_sf_gamma_inc_P);
  }
}

struct BetaInfo : FunctionInfo {
  Result GetDerivative(
      const Function &af, unsigned var_index, const Tuple &args) {
    if (gsl_isnan(gsl_sf_psi(args[0] + args[1])) || args[var_index] == 0)
      return EvalError(af, args, "'");
    return Result();
  }
};

TEST_F(GSLTest, Beta) {
  {
    BetaInfo info;
    TEST_FUNC(gsl_sf_beta);
  }
  TEST_FUNC(gsl_sf_lnbeta);
  {
    NoDerivativeInfo info;
    TEST_FUNC(gsl_sf_beta_inc);
  }
}

TEST_F(GSLTest, GegenPoly) {
  TEST_FUNC(gsl_sf_gegenpoly_1);
  TEST_FUNC(gsl_sf_gegenpoly_2);
  TEST_FUNC(gsl_sf_gegenpoly_3);

  NoDerivativeInfo info;
  info.SetArgNames("n");
  TEST_FUNC(gsl_sf_gegenpoly_n);
}

struct Hyperg0F1Info : FunctionInfo {
  Result GetDerivative(const Function &, unsigned var_index, const Tuple &) {
    // Partial derivative with respect to c is not provided.
    return Result(var_index == 0 ? "argument 'c' is not constant" : "");
  }
};

struct Hyperg1F1Info : FunctionInfo {
  Result GetDerivative(const Function &f, unsigned, const Tuple &args) {
    return args[1] <= 0 ? EvalError(f, args, "'") : Result();
  }
};

TEST_F(GSLTest, Hyperg) {
  {
    Hyperg0F1Info info;
    TEST_FUNC(gsl_sf_hyperg_0F1);
  }
  {
    Hyperg1F1Info info;
    info.SetArgNames("m n x");
    TEST_FUNC(gsl_sf_hyperg_1F1_int);
  }
}
}
