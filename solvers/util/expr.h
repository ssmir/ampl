/*
 A C++ interface to AMPL expression trees.

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

#ifndef SOLVERS_UTIL_EXPR_H_
#define SOLVERS_UTIL_EXPR_H_

#include <cassert>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>

extern "C" {
#include "solvers/nlp.h"
#include "solvers/opcode.hd"
}

namespace ampl {

class Expr;

namespace internal {

// Returns true if the non-null expression e is of type ExprT.
template <typename ExprT>
bool Is(Expr e);
}

// An expression.
// An Expr object represents a reference to an expression so
// it is cheap to construct and pass by value. A type safe way to
// process expressions of different types is by using ExprVisitor.
class Expr {
 public:
  // Expression kinds - one per each concrete expression class.
  enum Kind {
    // An unknown expression.
    UNKNOWN = 0,

    EXPR_START,

    // To simplify checks numeric expression kinds are in a consecutive range
    // [NUMERIC_START, NUMERIC_END].
    NUMERIC_START = EXPR_START,
    UNARY = NUMERIC_START,
    BINARY,
    VARARG,
    SUM,
    COUNT,
    IF,
    PLTERM,
    VARIABLE,
    NUMBEROF,
    NUMERIC_END,

    // CONSTANT belongs both to numeric and logical expressions therefore
    // the [NUMERIC_START, NUMERIC_END] and [LOGICAL_START, LOGICAL_END]
    // ranges overlap at CONSTANT = NUMERIC_END = LOGICAL_START.
    CONSTANT = NUMERIC_END,

    // To simplify checks logical expression kinds are in a consecutive range
    // [LOGICAL_START, LOGICAL_END].
    LOGICAL_START = CONSTANT,
    RELATIONAL,
    NOT,
    BINARY_LOGICAL,
    IMPLICATION,
    ITERATED_LOGICAL,
    ALLDIFF,
    LOGICAL_END = ALLDIFF,
    EXPR_END = LOGICAL_END
  };

 private:
  static const char *const OP_NAMES[N_OPS];
  static const Kind KINDS[N_OPS];

  // Returns the kind of this expression.
  Kind kind() const {
    assert(opcode() >= 0 && opcode() < N_OPS);
    return KINDS[opcode()];
  }

  void True() const {}
  typedef void (Expr::*SafeBool)() const;

  template <typename Impl, typename Result, typename LResult>
  friend class ExprVisitor;
  friend class ExprBuilder;
  friend class Driver;

  template <typename ExprT>
  static ExprT Create(Expr e) {
    assert(!e || internal::Is<ExprT>(e));
    ExprT expr;
    expr.expr_ = e.expr_;
    return expr;
  }

  // Constructs an Expr object representing a reference to an AMPL
  // expression e. Only a minimal check is performed when assertions are
  // enabled to make sure that the opcode is within the valid range.
  explicit Expr(expr *e) : expr_(e) {
    assert(!expr_ || (kind() >= EXPR_START && kind() <= EXPR_END));
  }

 protected:
  expr *expr_;

  template <typename ExprT>
  static ExprT Create(expr *e) { return Create<ExprT>(Expr(e)); }

  // An expression proxy used for implementing operator-> in iterators.
  template <typename ExprT>
  class Proxy {
   private:
    ExprT expr_;

   public:
    explicit Proxy(expr *e) : expr_(Create<ExprT>(e)) {}

    const ExprT *operator->() const { return &expr_; }
  };

  // An expression array iterator.
  template <typename ExprT>
  class ArrayIterator :
    public std::iterator<std::forward_iterator_tag, ExprT> {
   private:
    expr *const *ptr_;

   public:
    explicit ArrayIterator(expr *const *p = 0) : ptr_(p) {}

    ExprT operator*() const { return Create<ExprT>(*ptr_); }

    Proxy<ExprT> operator->() const {
      return Proxy<ExprT>(*ptr_);
    }

    ArrayIterator &operator++() {
      ++ptr_;
      return *this;
    }

    ArrayIterator operator++(int) {
      ArrayIterator it(*this);
      ++ptr_;
      return it;
    }

    bool operator==(ArrayIterator other) const { return ptr_ == other.ptr_; }
    bool operator!=(ArrayIterator other) const { return ptr_ != other.ptr_; }
  };

 public:
  // Constructs an Expr object representing a null reference to an AMPL
  // expression. The only operation permitted for such expression is
  // copying, assignment and check whether it is null using operator SafeBool.
  Expr() : expr_() {}

  // Returns a value convertible to bool that can be used in conditions but not
  // in comparisons and evaluates to "true" if this expression is not null
  // and "false" otherwise.
  // Example:
  //   void foo(Expr e) {
  //     if (e) {
  //       // Do something if e is not null.
  //     }
  //   }
  operator SafeBool() const { return expr_ ? &Expr::True : 0; }

  // Returns the operation code (opcode) of this expression which should be
  // non-null. The opcodes are defined in opcode.hd.
  int opcode() const {
    return reinterpret_cast<std::size_t>(expr_->op);
  }

  // Returns the operation name of this expression which should be non-null.
  const char *opname() const {
    assert(opcode() >= 0 && opcode() < N_OPS);
    return OP_NAMES[opcode()];
  }

  bool operator==(Expr other) const { return expr_ == other.expr_; }
  bool operator!=(Expr other) const { return expr_ != other.expr_; }

  template <typename ExprT>
  friend bool internal::Is(Expr e) {
    return e.kind() == ExprT::KIND;
  }

  // Casts an expression to type T. Returns a null expression if the cast
  // is not possible.
  template <typename ExprT>
  friend ExprT Cast(Expr e) {
    return internal::Is<ExprT>(e) ? Create<ExprT>(e) : ExprT();
  }

  // Recursively compares two expressions and returns true if they are equal.
  friend bool AreEqual(Expr e1, Expr e2);
};

namespace internal {
template <>
inline bool Is<Expr>(Expr e) {
  return e.kind() >= Expr::EXPR_START && e.kind() <= Expr::EXPR_END;
}
}

// A numeric expression.
class NumericExpr : public Expr {
 public:
  NumericExpr() {}
};

namespace internal {
template <>
inline bool Is<NumericExpr>(Expr e) {
  Expr::Kind k = e.kind();
  return k >= Expr::NUMERIC_START && k <= Expr::NUMERIC_END;
}
}

// A logical or constraint expression.
class LogicalExpr : public Expr {
 public:
  LogicalExpr() {}
};

namespace internal {
template <>
inline bool Is<LogicalExpr>(Expr e) {
  Expr::Kind k = e.kind();
  return k >= Expr::LOGICAL_START && k <= Expr::LOGICAL_END;
}
}

// A unary numeric expression.
// Examples: -x, sin(x), where x is a variable.
class UnaryExpr : public NumericExpr {
 public:
  static const Kind KIND = UNARY;

  UnaryExpr() {}

  // Returns the argument of this expression.
  NumericExpr arg() const { return Create<NumericExpr>(expr_->L.e); }
};

// A binary numeric expression.
// Examples: x / y, atan2(x, y), where x and y are variables.
class BinaryExpr : public NumericExpr {
 public:
  static const Kind KIND = BINARY;

  BinaryExpr() {}

  // Returns the left-hand side (the first argument) of this expression.
  NumericExpr lhs() const { return Create<NumericExpr>(expr_->L.e); }

  // Returns the right-hand side (the second argument) of this expression.
  NumericExpr rhs() const { return Create<NumericExpr>(expr_->R.e); }
};

// A numeric expression with a variable number of arguments.
// Example: min{i in I} x[i], where I is a set and x is a variable.
class VarArgExpr : public NumericExpr {
 private:
  static const de END;

 public:
  static const Kind KIND = VARARG;

  VarArgExpr() {}

  // An argument iterator.
  class iterator :
    public std::iterator<std::forward_iterator_tag, NumericExpr> {
   private:
    const de *de_;

    friend class VarArgExpr;

    explicit iterator(const de *d) : de_(d) {}

   public:
    iterator() : de_(&END) {}

    NumericExpr operator*() const { return Create<NumericExpr>(de_->e); }

    Proxy<NumericExpr> operator->() const {
      return Proxy<NumericExpr>(de_->e);
    }

    iterator &operator++() {
      ++de_;
      return *this;
    }

    iterator operator++(int) {
      iterator it(*this);
      ++de_;
      return it;
    }

    bool operator==(iterator other) const { return de_->e == other.de_->e; }
    bool operator!=(iterator other) const { return de_->e != other.de_->e; }
  };

  iterator begin() const {
    return iterator(reinterpret_cast<expr_va*>(expr_)->L.d);
  }

  iterator end() const {
    return iterator();
  }
};

// A sum expression.
// Example: sum{i in I} x[i], where I is a set and x is a variable.
class SumExpr : public NumericExpr {
 public:
  static const Kind KIND = SUM;

  SumExpr() {}

  typedef ArrayIterator<NumericExpr> iterator;

  iterator begin() const {
    return iterator(expr_->L.ep);
  }

  iterator end() const {
    return iterator(expr_->R.ep);
  }
};

// A count expression.
// Example: count{i in I} (x[i] >= 0), where I is a set and x is a variable.
class CountExpr : public NumericExpr {
 public:
  static const Kind KIND = COUNT;

  CountExpr() {}

  typedef ArrayIterator<LogicalExpr> iterator;

  iterator begin() const {
    return iterator(expr_->L.ep);
  }

  iterator end() const {
    return iterator(expr_->R.ep);
  }
};

// An if-then-else expression.
// Example: if x != 0 then y else z, where x, y and z are variables.
class IfExpr : public NumericExpr {
 public:
  static const Kind KIND = IF;

  IfExpr() {}

  LogicalExpr condition() const {
    return Create<LogicalExpr>(reinterpret_cast<expr_if*>(expr_)->e);
  }

  NumericExpr true_expr() const {
    return Create<NumericExpr>(reinterpret_cast<expr_if*>(expr_)->T);
  }

  NumericExpr false_expr() const {
    return Create<NumericExpr>(reinterpret_cast<expr_if*>(expr_)->F);
  }
};

// A piecewise-linear term.
// Example: <<0; -1, 1>> x, where x is a variable.
class PiecewiseLinearTerm : public NumericExpr {
 public:
  static const Kind KIND = PLTERM;

  PiecewiseLinearTerm() {}

  // Returns the number of slopes in this term.
  int num_slopes() const {
    assert(expr_->L.p->n >= 1);
    return expr_->L.p->n;
  }

  // Returns the number of breakpoints in this term.
  int num_breakpoints() const {
    return num_slopes() - 1;
  }

  // Returns the number of slopes in this term.
  double slope(int index) const {
    assert(index >= 0 && index < num_slopes());
    return expr_->L.p->bs[2 * index];
  }

  double breakpoint(int index) const {
    assert(index >= 0 && index < num_breakpoints());
    return expr_->L.p->bs[2 * index + 1];
  }

  int var_index() const {
    return reinterpret_cast<expr_v*>(expr_->R.e)->a;
  }
};

// A numeric constant.
// Examples: 42, -1.23e-4
class NumericConstant : public NumericExpr {
 public:
  static const Kind KIND = CONSTANT;

  NumericConstant() {}

  // Returns the value of this number.
  double value() const { return reinterpret_cast<expr_n*>(expr_)->v; }
};

namespace internal {
template <>
inline bool Is<NumericConstant>(Expr e) {
  return e.opcode() == OPNUM;
}
}

// A reference to a variable.
// Example: x
class Variable : public NumericExpr {
 public:
  static const Kind KIND = VARIABLE;

  Variable() {}

  // Returns the index of the referenced variable.
  int index() const { return expr_->a; }
};

namespace internal {
template <>
inline bool Is<Variable>(Expr e) {
  return e.opcode() == OPVARVAL;
}
}

// A numberof expression.
// Example: numberof 42 in ({i in I} x[i]),
// where I is a set and x is a variable.
class NumberOfExpr : public NumericExpr {
 public:
  static const Kind KIND = NUMBEROF;

  NumberOfExpr() {}

  NumericExpr target() const { return Create<NumericExpr>(*expr_->L.ep); }

  typedef ArrayIterator<NumericExpr> iterator;

  iterator begin() const {
    return iterator(expr_->L.ep + 1);
  }

  iterator end() const {
    return iterator(expr_->R.ep);
  }
};

// A logical constant.
// Examples: 0, 1
class LogicalConstant : public LogicalExpr {
 public:
  static const Kind KIND = CONSTANT;

  LogicalConstant() {}

  // Returns the value of this constant.
  bool value() const { return reinterpret_cast<expr_n*>(expr_)->v != 0; }
};

// A relational expression.
// Examples: x < y, x != y, where x and y are variables.
class RelationalExpr : public LogicalExpr {
 public:
  static const Kind KIND = RELATIONAL;

  RelationalExpr() {}

  // Returns the left-hand side (the first argument) of this expression.
  NumericExpr lhs() const { return Create<NumericExpr>(expr_->L.e); }

  // Returns the right-hand side (the second argument) of this expression.
  NumericExpr rhs() const { return Create<NumericExpr>(expr_->R.e); }
};

// A logical NOT expression.
// Example: not a, where a is a logical expression.
class NotExpr : public LogicalExpr {
 public:
  static const Kind KIND = NOT;

  NotExpr() {}

  // Returns the argument of this expression.
  LogicalExpr arg() const { return Create<LogicalExpr>(expr_->L.e); }
};

// A binary logical expression.
// Examples: a || b, a && b, where a and b are logical expressions.
class BinaryLogicalExpr : public LogicalExpr {
 public:
  static const Kind KIND = BINARY_LOGICAL;

  BinaryLogicalExpr() {}

  // Returns the left-hand side (the first argument) of this expression.
  LogicalExpr lhs() const { return Create<LogicalExpr>(expr_->L.e); }

  // Returns the right-hand side (the second argument) of this expression.
  LogicalExpr rhs() const { return Create<LogicalExpr>(expr_->R.e); }
};

// An implication expression.
// Example: a ==> b else c, where a, b and c are logical expressions.
class ImplicationExpr : public LogicalExpr {
 public:
  static const Kind KIND = IMPLICATION;

  ImplicationExpr() {}

  LogicalExpr condition() const {
    return Create<LogicalExpr>(reinterpret_cast<expr_if*>(expr_)->e);
  }

  LogicalExpr true_expr() const {
    return Create<LogicalExpr>(reinterpret_cast<expr_if*>(expr_)->T);
  }

  LogicalExpr false_expr() const {
    return Create<LogicalExpr>(reinterpret_cast<expr_if*>(expr_)->F);
  }
};

// An iterated logical expression.
// Example: exists{i in I} x[i] >= 0, where I is a set and x is a variable.
class IteratedLogicalExpr : public LogicalExpr {
 public:
  static const Kind KIND = ITERATED_LOGICAL;

  IteratedLogicalExpr() {}

  typedef ArrayIterator<LogicalExpr> iterator;

  iterator begin() const {
    return iterator(expr_->L.ep);
  }

  iterator end() const {
    return iterator(expr_->R.ep);
  }
};

// An alldiff expression.
// Example: alldiff{i in I} x[i], where I is a set and x is a variable.
class AllDiffExpr : public LogicalExpr {
 public:
  static const Kind KIND = ALLDIFF;

  AllDiffExpr() {}

  typedef ArrayIterator<NumericExpr> iterator;

  iterator begin() const {
    return iterator(expr_->L.ep);
  }

  iterator end() const {
    return iterator(expr_->R.ep);
  }
};

// A general error.
class Error : public std::runtime_error {
public:
  explicit Error(const std::string &message) : std::runtime_error(message) {}
};

// An exception that is thrown when an ASL expression not supported
// by the solver is encountered.
class UnsupportedExprError : public Error {
public:
  explicit UnsupportedExprError(const char *expr) :
    Error(std::string("unsupported expression: ") + expr) {}
};

// An exception that is thrown when an invalid numeric expression
// is encountered.
class InvalidNumericExprError : public Error {
public:
  explicit InvalidNumericExprError(NumericExpr e) :
    Error(std::string("invalid numeric expression: ") + e.opname()) {}
};

// An exception that is thrown when an invalid logical or constraint
// expression is encountered.
class InvalidLogicalExprError : public Error {
public:
  explicit InvalidLogicalExprError(LogicalExpr e) :
    Error(std::string("invalid logical expression: ") + e.opname()) {}
};

#define AMPL_DISPATCH(call) static_cast<Impl*>(this)->call

// An expression visitor.
// To use ExprVisitor define a subclass that implements some or all of the
// Visit* methods with the same signatures as the methods in ExprVisitor,
// for example, VisitDiv(BinaryExpr).
// Specify the subclass name as the Impl template parameter. Then calling
// ExprVisitor::Visit for some expression will dispatch to a Visit* method
// specific to the expression type. For example, if the expression is
// a division then VisitDiv(BinaryExpr) method of a subclass will be called.
// If the subclass doesn't contain a method with this signature, then
// a corresponding method of ExprVisitor will be called.
//
// Example:
//  class MyExprVisitor : public ExprVisitor<MyExprVisitor, double> {
//   public:
//    double VisitPlus(BinaryExpr e) { return Visit(e.lhs()) + Visit(e.rhs()); }
//    double VisitConstant(NumericConstant n) { return n.value(); }
//  };
//
// ExprVisitor uses the curiously recurring template pattern:
// http://en.wikipedia.org/wiki/Curiously_recurring_template_pattern
template <typename Impl, typename Result, typename LResult>
class ExprVisitor {
 public:
  Result Visit(NumericExpr e);
  LResult Visit(LogicalExpr e);

  Result VisitInvalidNumericExpr(NumericExpr e) {
    throw InvalidNumericExprError(e);
  }

  LResult VisitInvalidLogicalExpr(LogicalExpr e) {
    throw InvalidLogicalExprError(e);
  }

  Result VisitUnhandledNumericExpr(NumericExpr e) {
    throw UnsupportedExprError(e.opname());
  }

  LResult VisitUnhandledLogicalExpr(LogicalExpr e) {
    throw UnsupportedExprError(e.opname());
  }

  Result VisitPlus(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitMinus(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitMult(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitDiv(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitRem(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitPow(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitNumericLess(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitMin(VarArgExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitMax(VarArgExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitFloor(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitCeil(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitAbs(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitUnaryMinus(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitIf(IfExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitTanh(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitTan(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitSqrt(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitSinh(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitSin(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitLog10(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitLog(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitExp(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitCosh(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitCos(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitAtanh(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitAtan2(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitAtan(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitAsinh(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitAsin(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitAcosh(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitAcos(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitSum(SumExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitIntDiv(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitPrecision(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitRound(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitTrunc(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitCount(CountExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitNumberOf(NumberOfExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitPLTerm(PiecewiseLinearTerm t) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(t));
  }

  Result VisitConstExpPow(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitPow2(UnaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitConstBasePow(BinaryExpr e) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(e));
  }

  Result VisitNumericConstant(NumericConstant c) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(c));
  }

  Result VisitVariable(Variable v) {
    return AMPL_DISPATCH(VisitUnhandledNumericExpr(v));
  }

  LResult VisitOr(BinaryLogicalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitAnd(BinaryLogicalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitLess(RelationalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitLessEqual(RelationalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitEqual(RelationalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitGreaterEqual(RelationalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitGreater(RelationalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitNotEqual(RelationalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitNot(NotExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitAtLeast(RelationalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitAtMost(RelationalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitExactly(RelationalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitNotAtLeast(RelationalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitNotAtMost(RelationalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitNotExactly(RelationalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitForAll(IteratedLogicalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitExists(IteratedLogicalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitImplication(ImplicationExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitIff(BinaryLogicalExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitAllDiff(AllDiffExpr e) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(e));
  }

  LResult VisitLogicalConstant(LogicalConstant c) {
    return AMPL_DISPATCH(VisitUnhandledLogicalExpr(c));
  }
};

template <typename Impl, typename Result, typename LResult>
Result ExprVisitor<Impl, Result, LResult>::Visit(NumericExpr e) {
  // All expressions except OPNUMBEROFs, OPIFSYM, OPFUNCALL and OPHOL
  // are supported.
  switch (e.opcode()) {
  case OPPLUS:
    return AMPL_DISPATCH(VisitPlus(Expr::Create<BinaryExpr>(e)));
  case OPMINUS:
    return AMPL_DISPATCH(VisitMinus(Expr::Create<BinaryExpr>(e)));
  case OPMULT:
    return AMPL_DISPATCH(VisitMult(Expr::Create<BinaryExpr>(e)));
  case OPDIV:
    return AMPL_DISPATCH(VisitDiv(Expr::Create<BinaryExpr>(e)));
  case OPREM:
    return AMPL_DISPATCH(VisitRem(Expr::Create<BinaryExpr>(e)));
  case OPPOW:
    return AMPL_DISPATCH(VisitPow(Expr::Create<BinaryExpr>(e)));
  case OPLESS:
    return AMPL_DISPATCH(VisitNumericLess(Expr::Create<BinaryExpr>(e)));
  case MINLIST:
    return AMPL_DISPATCH(VisitMin(Expr::Create<VarArgExpr>(e)));
  case MAXLIST:
    return AMPL_DISPATCH(VisitMax(Expr::Create<VarArgExpr>(e)));
  case FLOOR:
    return AMPL_DISPATCH(VisitFloor(Expr::Create<UnaryExpr>(e)));
  case CEIL:
    return AMPL_DISPATCH(VisitCeil(Expr::Create<UnaryExpr>(e)));
  case ABS:
    return AMPL_DISPATCH(VisitAbs(Expr::Create<UnaryExpr>(e)));
  case OPUMINUS:
    return AMPL_DISPATCH(VisitUnaryMinus(Expr::Create<UnaryExpr>(e)));
  case OPIFnl:
    return AMPL_DISPATCH(VisitIf(Expr::Create<IfExpr>(e)));
  case OP_tanh:
    return AMPL_DISPATCH(VisitTanh(Expr::Create<UnaryExpr>(e)));
  case OP_tan:
    return AMPL_DISPATCH(VisitTan(Expr::Create<UnaryExpr>(e)));
  case OP_sqrt:
    return AMPL_DISPATCH(VisitSqrt(Expr::Create<UnaryExpr>(e)));
  case OP_sinh:
    return AMPL_DISPATCH(VisitSinh(Expr::Create<UnaryExpr>(e)));
  case OP_sin:
    return AMPL_DISPATCH(VisitSin(Expr::Create<UnaryExpr>(e)));
  case OP_log10:
    return AMPL_DISPATCH(VisitLog10(Expr::Create<UnaryExpr>(e)));
  case OP_log:
    return AMPL_DISPATCH(VisitLog(Expr::Create<UnaryExpr>(e)));
  case OP_exp:
    return AMPL_DISPATCH(VisitExp(Expr::Create<UnaryExpr>(e)));
  case OP_cosh:
    return AMPL_DISPATCH(VisitCosh(Expr::Create<UnaryExpr>(e)));
  case OP_cos:
    return AMPL_DISPATCH(VisitCos(Expr::Create<UnaryExpr>(e)));
  case OP_atanh:
    return AMPL_DISPATCH(VisitAtanh(Expr::Create<UnaryExpr>(e)));
  case OP_atan2:
    return AMPL_DISPATCH(VisitAtan2(Expr::Create<BinaryExpr>(e)));
  case OP_atan:
    return AMPL_DISPATCH(VisitAtan(Expr::Create<UnaryExpr>(e)));
  case OP_asinh:
    return AMPL_DISPATCH(VisitAsinh(Expr::Create<UnaryExpr>(e)));
  case OP_asin:
    return AMPL_DISPATCH(VisitAsin(Expr::Create<UnaryExpr>(e)));
  case OP_acosh:
    return AMPL_DISPATCH(VisitAcosh(Expr::Create<UnaryExpr>(e)));
  case OP_acos:
    return AMPL_DISPATCH(VisitAcos(Expr::Create<UnaryExpr>(e)));
  case OPSUMLIST:
    return AMPL_DISPATCH(VisitSum(Expr::Create<SumExpr>(e)));
  case OPintDIV:
    return AMPL_DISPATCH(VisitIntDiv(Expr::Create<BinaryExpr>(e)));
  case OPprecision:
    return AMPL_DISPATCH(VisitPrecision(Expr::Create<BinaryExpr>(e)));
  case OPround:
    return AMPL_DISPATCH(VisitRound(Expr::Create<BinaryExpr>(e)));
  case OPtrunc:
    return AMPL_DISPATCH(VisitTrunc(Expr::Create<BinaryExpr>(e)));
  case OPCOUNT:
    return AMPL_DISPATCH(VisitCount(Expr::Create<CountExpr>(e)));
  case OPNUMBEROF:
    return AMPL_DISPATCH(VisitNumberOf(Expr::Create<NumberOfExpr>(e)));
  case OPPLTERM:
    return AMPL_DISPATCH(VisitPLTerm(Expr::Create<PiecewiseLinearTerm>(e)));
  case OP1POW:
    return AMPL_DISPATCH(VisitConstExpPow(Expr::Create<BinaryExpr>(e)));
  case OP2POW:
    return AMPL_DISPATCH(VisitPow2(Expr::Create<UnaryExpr>(e)));
  case OPCPOW:
    return AMPL_DISPATCH(VisitConstBasePow(Expr::Create<BinaryExpr>(e)));
  case OPNUM:
    return AMPL_DISPATCH(VisitNumericConstant(
        Expr::Create<NumericConstant>(e)));
  case OPVARVAL:
    return AMPL_DISPATCH(VisitVariable(Expr::Create<Variable>(e)));
  default:
    return AMPL_DISPATCH(VisitInvalidNumericExpr(e));
  }
}

template <typename Impl, typename Result, typename LResult>
LResult ExprVisitor<Impl, Result, LResult>::Visit(LogicalExpr e) {
  switch (e.opcode()) {
  case OPOR:
    return AMPL_DISPATCH(VisitOr(Expr::Create<BinaryLogicalExpr>(e)));
  case OPAND:
    return AMPL_DISPATCH(VisitAnd(Expr::Create<BinaryLogicalExpr>(e)));
  case LT:
    return AMPL_DISPATCH(VisitLess(Expr::Create<RelationalExpr>(e)));
  case LE:
    return AMPL_DISPATCH(VisitLessEqual(Expr::Create<RelationalExpr>(e)));
  case EQ:
    return AMPL_DISPATCH(VisitEqual(Expr::Create<RelationalExpr>(e)));
  case GE:
    return AMPL_DISPATCH(VisitGreaterEqual(Expr::Create<RelationalExpr>(e)));
  case GT:
    return AMPL_DISPATCH(VisitGreater(Expr::Create<RelationalExpr>(e)));
  case NE:
    return AMPL_DISPATCH(VisitNotEqual(Expr::Create<RelationalExpr>(e)));
  case OPNOT:
    return AMPL_DISPATCH(VisitNot(Expr::Create<NotExpr>(e)));
  case OPATLEAST:
    return AMPL_DISPATCH(VisitAtLeast(Expr::Create<RelationalExpr>(e)));
  case OPATMOST:
    return AMPL_DISPATCH(VisitAtMost(Expr::Create<RelationalExpr>(e)));
  case OPEXACTLY:
    return AMPL_DISPATCH(VisitExactly(Expr::Create<RelationalExpr>(e)));
  case OPNOTATLEAST:
    return AMPL_DISPATCH(VisitNotAtLeast(Expr::Create<RelationalExpr>(e)));
  case OPNOTATMOST:
    return AMPL_DISPATCH(VisitNotAtMost(Expr::Create<RelationalExpr>(e)));
  case OPNOTEXACTLY:
    return AMPL_DISPATCH(VisitNotExactly(Expr::Create<RelationalExpr>(e)));
  case ANDLIST:
    return AMPL_DISPATCH(VisitForAll(Expr::Create<IteratedLogicalExpr>(e)));
  case ORLIST:
    return AMPL_DISPATCH(VisitExists(Expr::Create<IteratedLogicalExpr>(e)));
  case OPIMPELSE:
    return AMPL_DISPATCH(VisitImplication(Expr::Create<ImplicationExpr>(e)));
  case OP_IFF:
    return AMPL_DISPATCH(VisitIff(Expr::Create<BinaryLogicalExpr>(e)));
  case OPALLDIFF:
    return AMPL_DISPATCH(VisitAllDiff(Expr::Create<AllDiffExpr>(e)));
  case OPNUM:
    return AMPL_DISPATCH(VisitLogicalConstant(
        Expr::Create<LogicalConstant>(e)));
  default:
    return AMPL_DISPATCH(VisitInvalidLogicalExpr(e));
  }
}

#undef AMPL_DISPATCH
}

#endif  // SOLVERS_UTIL_EXPR_H_
