/// @file
/// SPDX-License-Identifier: GPL-3.0-or-later
/// @author Simon Heybrock
/// Copyright &copy; 2018 ISIS Rutherford Appleton Laboratory, NScD Oak Ridge
/// National Laboratory, and European Spallation Source ERIC.
#include "variable.h"
#include "counts.h"
#include "dataset.h"
#include "except.h"
#include "variable_view.h"

template <class T, class C> auto &requireT(C &concept) {
  try {
    return dynamic_cast<T &>(concept);
  } catch (const std::bad_cast &) {
    throw dataset::except::TypeError(
        "Expected item dtype " + dataset::to_string(T::static_dtype()) +
        ", got " + dataset::to_string(concept.dtype()) + '.');
  }
}

template <class T, class C> auto &require(C &concept) {
  try {
    return dynamic_cast<T &>(concept);
  } catch (const std::bad_cast &) {
    throw std::runtime_error(std::string("Cannot apply operation, requires ") +
                             T::name + " type.");
  }
}

template <template <class, class> class Op, class T1, class T2>
struct ArithmeticHelper {
  template <class InputView, class OutputView>
  static void apply(const OutputView &a, const InputView &b) {
    std::transform(a.begin(), a.end(), b.begin(), a.begin(), Op<T1, T2>());
  }
};

template <class T1, class T2> bool equal(const T1 &view1, const T2 &view2) {
  return std::equal(view1.begin(), view1.end(), view2.begin(), view2.end());
}

template <class T> class DataModel;
template <class T> class VariableConceptT;
template <class T> struct RebinHelper {
  // Special rebin version for rebinning inner dimension to a joint new coord.
  static void rebinInner(const Dim dim, const VariableConceptT<T> &oldT,
                         VariableConceptT<T> &newT,
                         const VariableConceptT<T> &oldCoordT,
                         const VariableConceptT<T> &newCoordT) {
    const auto &oldData = oldT.getSpan();
    auto newData = newT.getSpan();
    const auto oldSize = oldT.dimensions()[dim];
    const auto newSize = newT.dimensions()[dim];
    const auto count = oldT.dimensions().volume() / oldSize;
    const auto *xold = &*oldCoordT.getSpan().begin();
    const auto *xnew = &*newCoordT.getSpan().begin();
    // This function assumes that dimensions between coord and data either
    // match, or coord is 1D.
    const bool jointOld = oldCoordT.dimensions().ndim() == 1;
    const bool jointNew = newCoordT.dimensions().ndim() == 1;
#pragma omp parallel for
    for (gsl::index c = 0; c < count; ++c) {
      gsl::index iold = 0;
      gsl::index inew = 0;
      const gsl::index oldEdgeOffset = jointOld ? 0 : c * (oldSize + 1);
      const gsl::index newEdgeOffset = jointNew ? 0 : c * (newSize + 1);
      const auto oldOffset = c * oldSize;
      const auto newOffset = c * newSize;
      while ((iold < oldSize) && (inew < newSize)) {
        auto xo_low = xold[oldEdgeOffset + iold];
        auto xo_high = xold[oldEdgeOffset + iold + 1];
        auto xn_low = xnew[newEdgeOffset + inew];
        auto xn_high = xnew[newEdgeOffset + inew + 1];

        if ((std::isnan(xn_low) || std::isnan(xn_high)) || xn_high <= xo_low)
          inew++; /* old and new bins do not overlap */
        else if ((std::isnan(xo_low) || std::isnan(xo_high)) ||
                 xo_high <= xn_low)
          iold++; /* old and new bins do not overlap */
        else {
          // delta is the overlap of the bins on the x axis
          auto delta = xo_high < xn_high ? xo_high : xn_high;
          delta -= xo_low > xn_low ? xo_low : xn_low;

          auto owidth = xo_high - xo_low;
          newData[newOffset + inew] +=
              oldData[oldOffset + iold] * delta / owidth;

          if (xn_high > xo_high) {
            iold++;
          } else {
            inew++;
          }
        }
      }
    }
  }
};

template <typename T> struct RebinGeneralHelper {
  static void rebin(const Dim dim, const Variable &oldT, Variable &newT,
                    const Variable &oldCoordT, const Variable &newCoordT) {
    const auto oldSize = oldT.dimensions()[dim];
    const auto newSize = newT.dimensions()[dim];

    const auto *xold = oldCoordT.span<T>().data();
    const auto *xnew = newCoordT.span<T>().data();
    // This function assumes that dimensions between coord and data
    // coord is 1D.
    int iold = 0;
    int inew = 0;
    while ((iold < oldSize) && (inew < newSize)) {
      auto xo_low = xold[iold];
      auto xo_high = xold[iold + 1];
      auto xn_low = xnew[inew];
      auto xn_high = xnew[inew + 1];

      if ((std::isnan(xn_low) || std::isnan(xn_high)) || xn_high <= xo_low)
        inew++; /* old and new bins do not overlap */
      else if ((std::isnan(xo_low) || std::isnan(xo_high)) || xo_high <= xn_low)
        iold++; /* old and new bins do not overlap */
      else {
        // delta is the overlap of the bins on the x axis
        auto delta = xo_high < xn_high ? xo_high : xn_high;
        delta -= xo_low > xn_low ? xo_low : xn_low;

        auto owidth = xo_high - xo_low;
        newT(dim, inew) += oldT(dim, iold) * delta / owidth;
        if (xn_high > xo_high) {
          iold++;
        } else {
          inew++;
        }
      }
    }
  }
};

VariableConcept::VariableConcept(const Dimensions &dimensions)
    : m_dimensions(dimensions){};

// Some types such as Dataset support `+` (effectively appending table rows),
// but are not arithmetic.
class AddableVariableConcept : public VariableConcept {
public:
  static constexpr const char *name = "addable";
  using VariableConcept::VariableConcept;
  virtual VariableConcept &operator+=(const VariableConcept &other) = 0;
};

// This is also used for implementing operations for vector spaces, notably
// Eigen::Vector3d.
class ArithmeticVariableConcept : public AddableVariableConcept {
public:
  static constexpr const char *name = "arithmetic";
  using AddableVariableConcept::AddableVariableConcept;
  virtual VariableConcept &operator-=(const VariableConcept &other) = 0;
  virtual VariableConcept &operator*=(const VariableConcept &other) = 0;
  virtual VariableConcept &operator/=(const VariableConcept &other) = 0;
  /// Returns the absolute value (for scalars) or the norm (for vectors).
  virtual std::unique_ptr<VariableConcept> norm() const = 0;
};

class FloatingPointVariableConcept : public ArithmeticVariableConcept {
public:
  static constexpr const char *name = "floating-point";
  using ArithmeticVariableConcept::ArithmeticVariableConcept;
  /// Set x = value/x
  virtual VariableConcept &reciprocal_times(const double value) = 0;
  virtual std::unique_ptr<VariableConcept> sqrt() const = 0;
  virtual void rebin(const VariableConcept &old, const Dim dim,
                     const VariableConcept &oldCoord,
                     const VariableConcept &newCoord) = 0;
};

template <class T> class ViewModel;
template <class T> class AddableVariableConceptT;
template <class T> class ArithmeticVariableConceptT;
template <class T> class FloatingPointVariableConceptT;

template <class T, typename Enable = void> struct concept {
  using type = VariableConcept;
  using typeT = VariableConceptT<T>;
};
template <class T>
struct concept<T, std::enable_if_t<std::is_same<T, Dataset>::value>> {
  using type = AddableVariableConcept;
  using typeT = AddableVariableConceptT<T>;
};
template <class T> struct is_vector_space : std::false_type {};
template <class T, int Rows>
struct is_vector_space<Eigen::Matrix<T, Rows, 1>> : std::true_type {};
template <class T>
struct concept<T, std::enable_if_t<std::is_integral<T>::value ||
                                   is_vector_space<T>::value>> {
  using type = ArithmeticVariableConcept;
  using typeT = ArithmeticVariableConceptT<T>;
};
template <class T>
struct concept<T, std::enable_if_t<std::is_floating_point<T>::value>> {
  using type = FloatingPointVariableConcept;
  using typeT = FloatingPointVariableConceptT<T>;
};

template <class T> using concept_t = typename concept<T>::type;
template <class T> using conceptT_t = typename concept<T>::typeT;

/// Partially typed implementation of VariableConcept. This is a common base
/// class for DataModel<T> and ViewModel<T>. The former holds data in a
/// contiguous array, whereas the latter is a (potentially non-contiguous) view
/// into the former. This base class implements functionality that is common to
/// both, for a specific T.
template <class T> class VariableConceptT : public concept_t<T> {
public:
  VariableConceptT(const Dimensions &dimensions) : concept_t<T>(dimensions) {}

  DType dtype() const noexcept override { return ::dtype<T>; }
  static DType static_dtype() noexcept { return ::dtype<T>; }

  virtual gsl::span<T> getSpan() = 0;
  virtual gsl::span<T> getSpan(const Dim dim, const gsl::index begin,
                               const gsl::index end) = 0;
  virtual gsl::span<const T> getSpan() const = 0;
  virtual gsl::span<const T> getSpan(const Dim dim, const gsl::index begin,
                                     const gsl::index end) const = 0;
  virtual VariableView<T> getView(const Dimensions &dims) = 0;
  virtual VariableView<T> getView(const Dimensions &dims, const Dim dim,
                                  const gsl::index begin) = 0;
  virtual VariableView<const T> getView(const Dimensions &dims) const = 0;
  virtual VariableView<const T> getView(const Dimensions &dims, const Dim dim,
                                        const gsl::index begin) const = 0;
  virtual VariableView<const T> getReshaped(const Dimensions &dims) const = 0;
  virtual VariableView<T> getReshaped(const Dimensions &dims) = 0;

  std::unique_ptr<VariableConcept> makeView() const override {
    auto &dims = this->dimensions();
    return std::make_unique<ViewModel<decltype(getView(dims))>>(dims,
                                                                getView(dims));
  }

  std::unique_ptr<VariableConcept> makeView() override {
    if (this->isConstView())
      return const_cast<const VariableConceptT &>(*this).makeView();
    auto &dims = this->dimensions();
    return std::make_unique<ViewModel<decltype(getView(dims))>>(dims,
                                                                getView(dims));
  }

  std::unique_ptr<VariableConcept>
  makeView(const Dim dim, const gsl::index begin,
           const gsl::index end) const override {
    auto dims = this->dimensions();
    if (end == -1)
      dims.erase(dim);
    else
      dims.resize(dim, end - begin);
    return std::make_unique<ViewModel<decltype(getView(dims, dim, begin))>>(
        dims, getView(dims, dim, begin));
  }

  std::unique_ptr<VariableConcept> makeView(const Dim dim,
                                            const gsl::index begin,
                                            const gsl::index end) override {
    if (this->isConstView())
      return const_cast<const VariableConceptT &>(*this).makeView(dim, begin,
                                                                  end);
    auto dims = this->dimensions();
    if (end == -1)
      dims.erase(dim);
    else
      dims.resize(dim, end - begin);
    return std::make_unique<ViewModel<decltype(getView(dims, dim, begin))>>(
        dims, getView(dims, dim, begin));
  }

  std::unique_ptr<VariableConcept>
  reshape(const Dimensions &dims) const override {
    if (this->dimensions().volume() != dims.volume())
      throw std::runtime_error(
          "Cannot reshape to dimensions with different volume");
    return std::make_unique<ViewModel<decltype(getReshaped(dims))>>(
        dims, getReshaped(dims));
  }

  std::unique_ptr<VariableConcept> reshape(const Dimensions &dims) override {
    if (this->dimensions().volume() != dims.volume())
      throw std::runtime_error(
          "Cannot reshape to dimensions with different volume");
    return std::make_unique<ViewModel<decltype(getReshaped(dims))>>(
        dims, getReshaped(dims));
  }

  bool operator==(const VariableConcept &other) const override {
    const auto &dims = this->dimensions();
    if (dims != other.dimensions())
      return false;
    if (this->dtype() != other.dtype())
      return false;
    const auto &otherT = requireT<const VariableConceptT>(other);
    if (this->isContiguous()) {
      if (other.isContiguous() && dims.isContiguousIn(other.dimensions())) {
        return equal(getSpan(), otherT.getSpan());
      } else {
        return equal(getSpan(), otherT.getView(dims));
      }
    } else {
      if (other.isContiguous() && dims.isContiguousIn(other.dimensions())) {
        return equal(getView(dims), otherT.getSpan());
      } else {
        return equal(getView(dims), otherT.getView(dims));
      }
    }
  }

  void copy(const VariableConcept &other, const Dim dim,
            const gsl::index offset, const gsl::index otherBegin,
            const gsl::index otherEnd) override {
    auto iterDims = this->dimensions();
    const gsl::index delta = otherEnd - otherBegin;
    if (iterDims.contains(dim))
      iterDims.resize(dim, delta);

    const auto &otherT = requireT<const VariableConceptT>(other);
    auto otherView = otherT.getView(iterDims, dim, otherBegin);
    // Four cases for minimizing use of VariableView --- just copy contiguous
    // range where possible.
    if (this->isContiguous() && iterDims.isContiguousIn(this->dimensions())) {
      auto target = getSpan(dim, offset, offset + delta);
      if (other.isContiguous() && iterDims.isContiguousIn(other.dimensions())) {
        auto source = otherT.getSpan(dim, otherBegin, otherEnd);
        std::copy(source.begin(), source.end(), target.begin());
      } else {
        std::copy(otherView.begin(), otherView.end(), target.begin());
      }
    } else {
      auto view = getView(iterDims, dim, offset);
      if (other.isContiguous() && iterDims.isContiguousIn(other.dimensions())) {
        auto source = otherT.getSpan(dim, otherBegin, otherEnd);
        std::copy(source.begin(), source.end(), view.begin());
      } else {
        std::copy(otherView.begin(), otherView.end(), view.begin());
      }
    }
  }

  template <template <class, class> class Op, class OtherT = T>
  VariableConcept &apply(const VariableConcept &other) {
    const auto &dims = this->dimensions();
    try {
      const auto &otherT = requireT<const VariableConceptT<OtherT>>(other);
      if constexpr (std::is_same_v<T, OtherT>)
        if (this->getView(dims).overlaps(otherT.getView(dims))) {
          // If there is an overlap between lhs and rhs we copy the rhs before
          // applying the operation.
          const auto &data = otherT.getView(otherT.dimensions());
          DataModel<Vector<OtherT>> copy(
              other.dimensions(), Vector<OtherT>(data.begin(), data.end()));
          return apply<Op, OtherT>(copy);
        }

      if (this->isContiguous() && dims.contains(other.dimensions())) {
        if (other.isContiguous() && dims.isContiguousIn(other.dimensions())) {
          ArithmeticHelper<Op, T, OtherT>::apply(this->getSpan(),
                                                 otherT.getSpan());
        } else {
          ArithmeticHelper<Op, T, OtherT>::apply(this->getSpan(),
                                                 otherT.getView(dims));
        }
      } else if (dims.contains(other.dimensions())) {
        if (other.isContiguous() && dims.isContiguousIn(other.dimensions())) {
          ArithmeticHelper<Op, T, OtherT>::apply(this->getView(dims),
                                                 otherT.getSpan());
        } else {
          ArithmeticHelper<Op, T, OtherT>::apply(this->getView(dims),
                                                 otherT.getView(dims));
        }
      } else {
        // LHS has fewer dimensions than RHS, e.g., for computing sum. Use view.
        if (other.isContiguous() && dims.isContiguousIn(other.dimensions())) {
          ArithmeticHelper<Op, T, OtherT>::apply(
              this->getView(other.dimensions()), otherT.getSpan());
        } else {
          ArithmeticHelper<Op, T, OtherT>::apply(
              this->getView(other.dimensions()),
              otherT.getView(other.dimensions()));
        }
      }
    } catch (const std::bad_cast &) {
      throw std::runtime_error("Cannot apply arithmetic operation to "
                               "Variables: Underlying data types do not "
                               "match.");
    }
    return *this;
  }
};

// For operations with vector spaces we may have operations between a scalar and
// a vector, so we cannot use std::multiplies, which is available only for
// arguments of matching types.
template <class T1, class T2 = T1> struct plus {
  constexpr T1 operator()(const T1 &lhs, const T2 &rhs) const {
    return lhs + rhs;
  }
};
template <class T1, class T2 = T1> struct minus {
  constexpr T1 operator()(const T1 &lhs, const T2 &rhs) const {
    return lhs - rhs;
  }
};
template <class T1, class T2 = T1> struct multiplies {
  constexpr T1 operator()(const T1 &lhs, const T2 &rhs) const {
    return lhs * rhs;
  }
};
template <class T1, class T2 = T1> struct divides {
  constexpr T1 operator()(const T1 &lhs, const T2 &rhs) const {
    return lhs / rhs;
  }
};
template <class T1, class T2> struct norm_of_second_arg {
  constexpr T1 operator()(const T1 &, const T2 &rhs) const {
    // TODO Should we make a unary ArithmeticHelper::apply?
    if constexpr (is_vector_space<T2>::value)
      return rhs.norm();
    else
      return abs(rhs);
  }
};
template <class T1, class T2> struct sqrt_of_second_arg {
  constexpr T1 operator()(const T1 &, const T2 &rhs) const {
    // TODO Should we make a unary ArithmeticHelper::apply?
    return sqrt(rhs);
  }
};

template <class T> struct scalar_type { using type = T; };
template <class T, int Rows> struct scalar_type<Eigen::Matrix<T, Rows, 1>> {
  using type = T;
};
template <class T> using scalar_type_t = typename scalar_type<T>::type;

template <class T> class AddableVariableConceptT : public VariableConceptT<T> {
public:
  using VariableConceptT<T>::VariableConceptT;

  VariableConcept &operator+=(const VariableConcept &other) override {
    return this->template apply<plus>(other);
  }
};

template <class T>
class ArithmeticVariableConceptT : public AddableVariableConceptT<T> {
public:
  using AddableVariableConceptT<T>::AddableVariableConceptT;

  VariableConcept &operator-=(const VariableConcept &other) override {
    return this->template apply<minus>(other);
  }

  VariableConcept &operator*=(const VariableConcept &other) override {
    return this->template apply<multiplies, scalar_type_t<T>>(other);
  }

  VariableConcept &operator/=(const VariableConcept &other) override {
    return this->template apply<divides, scalar_type_t<T>>(other);
  }

  std::unique_ptr<VariableConcept> norm() const override {
    using ScalarT = scalar_type_t<T>;
    auto norm = std::make_unique<DataModel<Vector<ScalarT>>>(
        this->dimensions(), Vector<ScalarT>(this->dimensions().volume()));
    norm->template apply<norm_of_second_arg, T>(*this);
    return norm;
  }
};

template <class T1, class T2> struct ReciprocalTimes {
  T2 operator()(const T1 a, const T2 b) { return b / a; };
};

bool isMatchingOr1DBinEdge(const Dim dim, Dimensions edges,
                           const Dimensions &toMatch) {
  if (edges.ndim() == 1)
    return true;
  edges.resize(dim, edges[dim] - 1);
  return edges == toMatch;
}

template <class T>
class FloatingPointVariableConceptT : public ArithmeticVariableConceptT<T> {
public:
  using ArithmeticVariableConceptT<T>::ArithmeticVariableConceptT;

  VariableConcept &reciprocal_times(const double value) override {
    Variable other(Data::Value, {}, {value});
    return this->template apply<ReciprocalTimes>(other.data());
  }

  std::unique_ptr<VariableConcept> sqrt() const override {
    auto sqrt = std::make_unique<DataModel<Vector<T>>>(
        this->dimensions(), Vector<T>(this->dimensions().volume()));
    sqrt->template apply<sqrt_of_second_arg, T>(*this);
    return sqrt;
  }

  void rebin(const VariableConcept &old, const Dim dim,
             const VariableConcept &oldCoord,
             const VariableConcept &newCoord) override {
    // Dimensions of *this and old are guaranteed to be the same.
    auto &oldT = requireT<const FloatingPointVariableConceptT>(old);
    auto &oldCoordT = requireT<const FloatingPointVariableConceptT>(oldCoord);
    auto &newCoordT = requireT<const FloatingPointVariableConceptT>(newCoord);
    const auto &dims = this->dimensions();
    if (dims.inner() == dim &&
        isMatchingOr1DBinEdge(dim, oldCoord.dimensions(), old.dimensions()) &&
        isMatchingOr1DBinEdge(dim, newCoord.dimensions(), dims)) {
      RebinHelper<T>::rebinInner(dim, oldT, *this, oldCoordT, newCoordT);
    } else {
      throw std::runtime_error(
          "TODO the new coord should be 1D or the same din as newCoord.");
    }
  }
};

template <class T>
auto makeSpan(T &model, const Dimensions &dims, const Dim dim,
              const gsl::index begin, const gsl::index end) {
  if (!dims.contains(dim) && (begin != 0 || end != 1))
    throw std::runtime_error("VariableConcept: Slice index out of range.");
  if (!dims.contains(dim) || dims[dim] == end - begin) {
    return gsl::make_span(model.data(), model.data() + model.size());
  }
  const gsl::index beginOffset = begin * dims.offset(dim);
  const gsl::index endOffset = end * dims.offset(dim);
  return gsl::make_span(model.data() + beginOffset, model.data() + endOffset);
}

/// Implementation of VariableConcept that holds data.
template <class T> class DataModel : public conceptT_t<typename T::value_type> {
  using value_type = std::remove_const_t<typename T::value_type>;

public:
  DataModel(const Dimensions &dimensions, T model)
      : conceptT_t<typename T::value_type>(std::move(dimensions)),
        m_model(std::move(model)) {
    if (this->dimensions().volume() != static_cast<gsl::index>(m_model.size()))
      throw std::runtime_error("Creating Variable: data size does not match "
                               "volume given by dimension extents");
  }

  gsl::span<value_type> getSpan() override {
    return gsl::make_span(m_model.data(), m_model.data() + size());
  }
  gsl::span<value_type> getSpan(const Dim dim, const gsl::index begin,
                                const gsl::index end) override {
    return makeSpan(m_model, this->dimensions(), dim, begin, end);
  }

  gsl::span<const value_type> getSpan() const override {
    return gsl::make_span(m_model.data(), m_model.data() + size());
  }
  gsl::span<const value_type> getSpan(const Dim dim, const gsl::index begin,
                                      const gsl::index end) const override {
    return makeSpan(m_model, this->dimensions(), dim, begin, end);
  }

  VariableView<value_type> getView(const Dimensions &dims) override {
    return makeVariableView(m_model.data(), 0, dims, this->dimensions());
  }
  VariableView<value_type> getView(const Dimensions &dims, const Dim dim,
                                   const gsl::index begin) override {
    gsl::index beginOffset = this->dimensions().contains(dim)
                                 ? begin * this->dimensions().offset(dim)
                                 : begin * this->dimensions().volume();
    return makeVariableView(m_model.data(), beginOffset, dims,
                            this->dimensions());
  }

  VariableView<const value_type>
  getView(const Dimensions &dims) const override {
    return makeVariableView(m_model.data(), 0, dims, this->dimensions());
  }
  VariableView<const value_type>
  getView(const Dimensions &dims, const Dim dim,
          const gsl::index begin) const override {
    gsl::index beginOffset = this->dimensions().contains(dim)
                                 ? begin * this->dimensions().offset(dim)
                                 : begin * this->dimensions().volume();
    return makeVariableView(m_model.data(), beginOffset, dims,
                            this->dimensions());
  }

  VariableView<const value_type>
  getReshaped(const Dimensions &dims) const override {
    return makeVariableView(m_model.data(), 0, dims, dims);
  }
  VariableView<value_type> getReshaped(const Dimensions &dims) override {
    return makeVariableView(m_model.data(), 0, dims, dims);
  }

  std::unique_ptr<VariableConcept> clone() const override {
    return std::make_unique<DataModel<T>>(this->dimensions(), m_model);
  }

  std::unique_ptr<VariableConcept>
  clone(const Dimensions &dims) const override {
    return std::make_unique<DataModel<T>>(dims, T(dims.volume()));
  }

  bool isContiguous() const override { return true; }
  bool isView() const override { return false; }
  bool isConstView() const override { return false; }

  gsl::index size() const override { return m_model.size(); }

  T m_model;
};

/// Implementation of VariableConcept that represents a view onto data.
template <class T>
class ViewModel
    : public conceptT_t<std::remove_const_t<typename T::element_type>> {
  using value_type = typename T::value_type;

  void requireMutable() const {
    if (isConstView())
      throw std::runtime_error(
          "View is const, cannot get mutable range of data.");
  }
  void requireContiguous() const {
    if (!isContiguous())
      throw std::runtime_error(
          "View is not contiguous, cannot get contiguous range of data.");
  }

public:
  ViewModel(const Dimensions &dimensions, T model)
      : conceptT_t<value_type>(std::move(dimensions)),
        m_model(std::move(model)) {
    if (this->dimensions().volume() != m_model.size())
      throw std::runtime_error("Creating Variable: data size does not match "
                               "volume given by dimension extents");
  }

  gsl::span<value_type> getSpan() override {
    requireMutable();
    requireContiguous();
    if constexpr (std::is_const<typename T::element_type>::value)
      return gsl::span<value_type>();
    else
      return gsl::make_span(m_model.data(), m_model.data() + size());
  }
  gsl::span<value_type> getSpan(const Dim dim, const gsl::index begin,
                                const gsl::index end) override {
    requireMutable();
    requireContiguous();
    if constexpr (std::is_const<typename T::element_type>::value) {
      static_cast<void>(dim);
      static_cast<void>(begin);
      static_cast<void>(end);
      return gsl::span<value_type>();
    } else {
      return makeSpan(m_model, this->dimensions(), dim, begin, end);
    }
  }

  gsl::span<const value_type> getSpan() const override {
    requireContiguous();
    return gsl::make_span(m_model.data(), m_model.data() + size());
  }
  gsl::span<const value_type> getSpan(const Dim dim, const gsl::index begin,
                                      const gsl::index end) const override {
    requireContiguous();
    return makeSpan(m_model, this->dimensions(), dim, begin, end);
  }

  VariableView<value_type> getView(const Dimensions &dims) override {
    requireMutable();
    if constexpr (std::is_const<typename T::element_type>::value) {
      static_cast<void>(dims);
      return VariableView<value_type>(nullptr, 0, {}, {});
    } else {
      return {m_model, dims};
    }
  }
  VariableView<value_type> getView(const Dimensions &dims, const Dim dim,
                                   const gsl::index begin) override {
    requireMutable();
    if constexpr (std::is_const<typename T::element_type>::value) {
      static_cast<void>(dim);
      static_cast<void>(begin);
      return VariableView<value_type>(nullptr, 0, {}, {});
    } else {
      return {m_model, dims, dim, begin};
    }
  }

  VariableView<const value_type>
  getView(const Dimensions &dims) const override {
    return {m_model, dims};
  }
  VariableView<const value_type>
  getView(const Dimensions &dims, const Dim dim,
          const gsl::index begin) const override {
    return {m_model, dims, dim, begin};
  }

  VariableView<const value_type>
  getReshaped(const Dimensions &dims) const override {
    return {m_model, dims};
  }
  VariableView<value_type> getReshaped(const Dimensions &dims) override {
    requireMutable();
    if constexpr (std::is_const<typename T::element_type>::value) {
      static_cast<void>(dims);
      return VariableView<value_type>(nullptr, 0, {}, {});
    } else {
      return {m_model, dims};
    }
  }

  std::unique_ptr<VariableConcept> clone() const override {
    return std::make_unique<ViewModel<T>>(this->dimensions(), m_model);
  }

  std::unique_ptr<VariableConcept> clone(const Dimensions &) const override {
    throw std::runtime_error("Cannot resize view.");
  }

  bool isContiguous() const override {
    return this->dimensions().isContiguousIn(m_model.parentDimensions());
  }
  bool isView() const override { return true; }
  bool isConstView() const override {
    return std::is_const<typename T::element_type>::value;
  }

  gsl::index size() const override { return m_model.size(); }

  T m_model;
};

Variable::Variable(const ConstVariableSlice &slice)
    : Variable(*slice.m_variable) {
  if (slice.m_view) {
    m_tag = slice.tag();
    m_name = slice.m_variable->m_name;
    setUnit(slice.unit());
    setDimensions(slice.dimensions());
    data().copy(slice.data(), Dim::Invalid, 0, 0, 1);
  }
}
Variable::Variable(const Variable &parent, const Dimensions &dims)
    : m_tag(parent.tag()), m_unit(parent.unit()), m_name(parent.m_name),
      m_object(parent.m_object->clone(dims)) {}

Variable::Variable(const ConstVariableSlice &parent, const Dimensions &dims)
    : m_tag(parent.tag()), m_unit(parent.unit()),
      m_object(parent.data().clone(dims)) {
  setName(parent.name());
}

Variable::Variable(const Variable &parent,
                   std::unique_ptr<VariableConcept> data)
    : m_tag(parent.tag()), m_unit(parent.unit()), m_name(parent.m_name),
      m_object(std::move(data)) {}

template <class T>
Variable::Variable(const Tag tag, const Unit unit, const Dimensions &dimensions,
                   T object)
    : m_tag(tag), m_unit{unit},
      m_object(std::make_unique<DataModel<T>>(std::move(dimensions),
                                              std::move(object))) {}

void Variable::setDimensions(const Dimensions &dimensions) {
  if (dimensions.volume() == m_object->dimensions().volume()) {
    if (dimensions != m_object->dimensions())
      data().m_dimensions = dimensions;
    return;
  }
  m_object = m_object->clone(dimensions);
}

template <class T> const Vector<underlying_type_t<T>> &Variable::cast() const {
  return requireT<const DataModel<Vector<underlying_type_t<T>>>>(*m_object)
      .m_model;
}

template <class T> Vector<underlying_type_t<T>> &Variable::cast() {
  return requireT<DataModel<Vector<underlying_type_t<T>>>>(*m_object).m_model;
}

#define INSTANTIATE(...)                                                       \
  template Variable::Variable(const Tag, const Unit, const Dimensions &,       \
                              Vector<underlying_type_t<__VA_ARGS__>>);         \
  template Vector<underlying_type_t<__VA_ARGS__>>                              \
      &Variable::cast<__VA_ARGS__>();                                          \
  template const Vector<underlying_type_t<__VA_ARGS__>>                        \
      &Variable::cast<__VA_ARGS__>() const;

INSTANTIATE(std::string)
INSTANTIATE(double)
INSTANTIATE(float)
INSTANTIATE(int64_t)
INSTANTIATE(int32_t)
INSTANTIATE(char)
INSTANTIATE(bool)
INSTANTIATE(std::pair<int64_t, int64_t>)
INSTANTIATE(ValueWithDelta<double>)
#if defined(_WIN32) || defined(__clang__) && defined(__APPLE__)
INSTANTIATE(gsl::index)
INSTANTIATE(std::pair<gsl::index, gsl::index>)
#endif
INSTANTIATE(boost::container::small_vector<gsl::index, 1>)
INSTANTIATE(boost::container::small_vector<double, 8>)
INSTANTIATE(std::vector<double>)
INSTANTIATE(std::vector<std::string>)
INSTANTIATE(std::vector<gsl::index>)
INSTANTIATE(Dataset)
INSTANTIATE(std::array<double, 3>)
INSTANTIATE(std::array<double, 4>)
INSTANTIATE(Eigen::Vector3d)

template <class T1, class T2> bool equals(const T1 &a, const T2 &b) {
  // Compare even before pointer comparison since data may be shared even if
  // names differ.
  if (a.name() != b.name())
    return false;
  if (a.unit() != b.unit())
    return false;
  // Deep comparison
  if (a.tag() != b.tag())
    return false;
  if (!(a.dimensions() == b.dimensions()))
    return false;
  return a.data() == b.data();
}

bool Variable::operator==(const Variable &other) const {
  return equals(*this, other);
}

bool Variable::operator==(const ConstVariableSlice &other) const {
  return equals(*this, other);
}

bool Variable::operator!=(const Variable &other) const {
  return !(*this == other);
}

bool Variable::operator!=(const ConstVariableSlice &other) const {
  return !(*this == other);
}

template <class T1, class T2> T1 &plus_equals(T1 &variable, const T2 &other) {
  // Addition with different Variable type is supported, mismatch of underlying
  // element types is handled in DataModel::operator+=.
  // Different name is ok for addition.
  dataset::expect::equals(variable.unit(), other.unit());
  // TODO How should attributes be handled?
  if (variable.dtype() != dtype<Dataset> || variable.isAttr()) {
    dataset::expect::contains(variable.dimensions(), other.dimensions());
    // Note: This will broadcast/transpose the RHS if required. We do not
    // support changing the dimensions of the LHS though!
    require<AddableVariableConcept>(variable.data()) += other.data();
  } else {
    if (variable.dimensions() == other.dimensions()) {
      using ConstViewOrRef =
          std::conditional_t<std::is_same<T2, Variable>::value,
                             const Vector<Dataset> &,
                             const VariableView<const Dataset>>;
      ConstViewOrRef otherDatasets = other.template cast<Dataset>();
      if (otherDatasets.size() > 0 &&
          otherDatasets[0].dimensions().count() != 1)
        throw std::runtime_error(
            "Cannot add Variable: Nested Dataset dimension must be 1.");
      auto datasets = variable.template cast<Dataset>();
      const Dim dim = datasets[0].dimensions().label(0);
#pragma omp parallel for
      for (gsl::index i = 0; i < static_cast<gsl::index>(datasets.size()); ++i)
        datasets[i] = concatenate(datasets[i], otherDatasets[i], dim);
    } else {
      throw std::runtime_error(
          "Cannot add Variables: Dimensions do not match.");
    }
  }
  return variable;
}

Variable Variable::operator-() const {
  // TODO This implementation only works for variables containing doubles and
  // will throw, e.g., for ints.
  auto copy(*this);
  copy *= -1.0;
  return copy;
}

Variable &Variable::operator+=(const Variable &other) & {
  return plus_equals(*this, other);
}
Variable &Variable::operator+=(const ConstVariableSlice &other) & {
  return plus_equals(*this, other);
}
Variable &Variable::operator+=(const double value) & {
  // TODO By not setting a unit here this operator is only usable if the
  // variable is dimensionless. Should we ignore the unit for scalar operations,
  // i.e., set the same unit as *this.unit()?
  Variable other(Data::Value, {}, {value});
  return plus_equals(*this, other);
}

template <class T1, class T2> T1 &minus_equals(T1 &variable, const T2 &other) {
  dataset::expect::equals(variable.unit(), other.unit());
  dataset::expect::contains(variable.dimensions(), other.dimensions());
  if (variable.tag() == Data::Events)
    throw std::runtime_error("Subtraction of events lists not implemented.");
  require<ArithmeticVariableConcept>(variable.data()) -= other.data();
  return variable;
}

Variable &Variable::operator-=(const Variable &other) & {
  return minus_equals(*this, other);
}
Variable &Variable::operator-=(const ConstVariableSlice &other) & {
  return minus_equals(*this, other);
}
Variable &Variable::operator-=(const double value) & {
  Variable other(Data::Value, {}, {value});
  return minus_equals(*this, other);
}

template <class T1, class T2> T1 &times_equals(T1 &variable, const T2 &other) {
  dataset::expect::contains(variable.dimensions(), other.dimensions());
  if (variable.tag() == Data::Events)
    throw std::runtime_error("Multiplication of events lists not implemented.");
  // setUnit is catching bad cases of changing units (if `variable` is a slice).
  variable.setUnit(variable.unit() * other.unit());
  require<ArithmeticVariableConcept>(variable.data()) *= other.data();
  return variable;
}

Variable &Variable::operator*=(const Variable &other) & {
  return times_equals(*this, other);
}
Variable &Variable::operator*=(const ConstVariableSlice &other) & {
  return times_equals(*this, other);
}
Variable &Variable::operator*=(const double value) & {
  Variable other(Data::Value, {}, {value});
  other.setUnit(units::dimensionless);
  return times_equals(*this, other);
}

template <class T1, class T2> T1 &divide_equals(T1 &variable, const T2 &other) {
  dataset::expect::contains(variable.dimensions(), other.dimensions());
  if (variable.tag() == Data::Events)
    throw std::runtime_error("Division of events lists not implemented.");
  // setUnit is catching bad cases of changing units (if `variable` is a slice).
  variable.setUnit(variable.unit() / other.unit());
  require<ArithmeticVariableConcept>(variable.data()) /= other.data();
  return variable;
}

Variable &Variable::operator/=(const Variable &other) & {
  return divide_equals(*this, other);
}
Variable &Variable::operator/=(const ConstVariableSlice &other) & {
  return divide_equals(*this, other);
}
Variable &Variable::operator/=(const double value) & {
  Variable other(Data::Value, {}, {value});
  other.setUnit(units::dimensionless);
  return divide_equals(*this, other);
}

template <class T> VariableSlice VariableSlice::assign(const T &other) const {
  // TODO Should mismatching tags be allowed, as long as the type matches?
  if (tag() != other.tag())
    throw std::runtime_error("Cannot assign to slice: Type mismatch.");
  // Name mismatch ok, but do not assign it.
  if (unit() != other.unit())
    throw std::runtime_error("Cannot assign to slice: Unit mismatch.");
  if (dimensions() != other.dimensions())
    throw dataset::except::DimensionMismatchError(dimensions(),
                                                  other.dimensions());
  data().copy(other.data(), Dim::Invalid, 0, 0, 1);
  return *this;
}

template VariableSlice VariableSlice::assign(const Variable &) const;
template VariableSlice VariableSlice::assign(const ConstVariableSlice &) const;

VariableSlice VariableSlice::operator+=(const Variable &other) const {
  return plus_equals(*this, other);
}
VariableSlice VariableSlice::operator+=(const ConstVariableSlice &other) const {
  return plus_equals(*this, other);
}
VariableSlice VariableSlice::operator+=(const double value) const {
  Variable other(Data::Value, {}, {value});
  other.setUnit(units::dimensionless);
  return plus_equals(*this, other);
}

VariableSlice VariableSlice::operator-=(const Variable &other) const {
  return minus_equals(*this, other);
}
VariableSlice VariableSlice::operator-=(const ConstVariableSlice &other) const {
  return minus_equals(*this, other);
}
VariableSlice VariableSlice::operator-=(const double value) const {
  Variable other(Data::Value, {}, {value});
  other.setUnit(units::dimensionless);
  return minus_equals(*this, other);
}

VariableSlice VariableSlice::operator*=(const Variable &other) const {
  return times_equals(*this, other);
}
VariableSlice VariableSlice::operator*=(const ConstVariableSlice &other) const {
  return times_equals(*this, other);
}
VariableSlice VariableSlice::operator*=(const double value) const {
  Variable other(Data::Value, {}, {value});
  other.setUnit(units::dimensionless);
  return times_equals(*this, other);
}

VariableSlice VariableSlice::operator/=(const Variable &other) const {
  return divide_equals(*this, other);
}
VariableSlice VariableSlice::operator/=(const ConstVariableSlice &other) const {
  return divide_equals(*this, other);
}
VariableSlice VariableSlice::operator/=(const double value) const {
  Variable other(Data::Value, {}, {value});
  other.setUnit(units::dimensionless);
  return divide_equals(*this, other);
}

bool ConstVariableSlice::operator==(const Variable &other) const {
  // Always use deep comparison (pointer comparison does not make sense since we
  // may be looking at a different section).
  return equals(*this, other);
}
bool ConstVariableSlice::operator==(const ConstVariableSlice &other) const {
  return equals(*this, other);
}

bool ConstVariableSlice::operator!=(const Variable &other) const {
  return !(*this == other);
}
bool ConstVariableSlice::operator!=(const ConstVariableSlice &other) const {
  return !(*this == other);
}

Variable ConstVariableSlice::operator-() const {
  Variable copy(*this);
  return -copy;
}

void VariableSlice::setUnit(const Unit &unit) const {
  // TODO Should we forbid setting the unit altogether? I think it is useful in
  // particular since views onto subsets of dataset do not imply slicing of
  // variables but return slice views.
  if ((this->unit() != unit) &&
      (dimensions() != m_mutableVariable->dimensions()))
    throw std::runtime_error("Partial view on data of variable cannot be used "
                             "to change the unit.\n");
  m_mutableVariable->setUnit(unit);
}

template <class T>
const VariableView<const underlying_type_t<T>>
ConstVariableSlice::cast() const {
  using TT = underlying_type_t<T>;
  if (!m_view)
    return requireT<const DataModel<Vector<TT>>>(data()).getView(dimensions());
  if (m_view->isConstView())
    return requireT<const ViewModel<VariableView<const TT>>>(data()).m_model;
  // Make a const view from the mutable one.
  return {requireT<const ViewModel<VariableView<TT>>>(data()).m_model,
          dimensions()};
}

template <class T>
VariableView<underlying_type_t<T>> VariableSlice::cast() const {
  using TT = underlying_type_t<T>;
  if (m_view)
    return requireT<const ViewModel<VariableView<TT>>>(data()).m_model;
  return requireT<DataModel<Vector<TT>>>(data()).getView(dimensions());
}

#define INSTANTIATE_SLICEVIEW(...)                                             \
  template const VariableView<const underlying_type_t<__VA_ARGS__>>            \
  ConstVariableSlice::cast<__VA_ARGS__>() const;                               \
  template VariableView<underlying_type_t<__VA_ARGS__>>                        \
  VariableSlice::cast<__VA_ARGS__>() const;

INSTANTIATE_SLICEVIEW(double);
INSTANTIATE_SLICEVIEW(float);
INSTANTIATE_SLICEVIEW(int64_t);
INSTANTIATE_SLICEVIEW(int32_t);
INSTANTIATE_SLICEVIEW(char);
INSTANTIATE_SLICEVIEW(bool);
INSTANTIATE_SLICEVIEW(std::string);
INSTANTIATE_SLICEVIEW(boost::container::small_vector<double, 8>);
INSTANTIATE_SLICEVIEW(Dataset);
INSTANTIATE_SLICEVIEW(Eigen::Vector3d);

ConstVariableSlice Variable::operator()(const Dim dim, const gsl::index begin,
                                        const gsl::index end) const & {
  return {*this, dim, begin, end};
}

VariableSlice Variable::operator()(const Dim dim, const gsl::index begin,
                                   const gsl::index end) & {
  return {*this, dim, begin, end};
}

ConstVariableSlice Variable::reshape(const Dimensions &dims) const & {
  return {*this, dims};
}

VariableSlice Variable::reshape(const Dimensions &dims) & {
  return {*this, dims};
}

Variable Variable::reshape(const Dimensions &dims) && {
  Variable reshaped(std::move(*this));
  reshaped.setDimensions(dims);
  return reshaped;
}

Variable ConstVariableSlice::reshape(const Dimensions &dims) const {
  // In general a variable slice is not contiguous. Therefore we cannot reshape
  // without making a copy (except for special cases).
  Variable reshaped(*this);
  reshaped.setDimensions(dims);
  return reshaped;
}

// Note: The std::move here is necessary because RVO does not work for variables
// that are function parameters.
Variable operator+(Variable a, const Variable &b) {
  auto result = broadcast(std::move(a), b.dimensions());
  return result += b;
}
Variable operator-(Variable a, const Variable &b) {
  auto result = broadcast(std::move(a), b.dimensions());
  return result -= b;
}
Variable operator*(Variable a, const Variable &b) {
  auto result = broadcast(std::move(a), b.dimensions());
  return result *= b;
}
Variable operator/(Variable a, const Variable &b) {
  auto result = broadcast(std::move(a), b.dimensions());
  return result /= b;
}
Variable operator+(Variable a, const ConstVariableSlice &b) {
  auto result = broadcast(std::move(a), b.dimensions());
  return result += b;
}
Variable operator-(Variable a, const ConstVariableSlice &b) {
  auto result = broadcast(std::move(a), b.dimensions());
  return result -= b;
}
Variable operator*(Variable a, const ConstVariableSlice &b) {
  auto result = broadcast(std::move(a), b.dimensions());
  return result *= b;
}
Variable operator/(Variable a, const ConstVariableSlice &b) {
  auto result = broadcast(std::move(a), b.dimensions());
  return result /= b;
}
Variable operator+(Variable a, const double b) { return std::move(a += b); }
Variable operator-(Variable a, const double b) { return std::move(a -= b); }
Variable operator*(Variable a, const double b) { return std::move(a *= b); }
Variable operator/(Variable a, const double b) { return std::move(a /= b); }
Variable operator+(const double a, Variable b) { return std::move(b += a); }
Variable operator-(const double a, Variable b) { return -(b -= a); }
Variable operator*(const double a, Variable b) { return std::move(b *= a); }
Variable operator/(const double a, Variable b) {
  b.setUnit(Unit(units::dimensionless) / b.unit());
  require<FloatingPointVariableConcept>(b.data()).reciprocal_times(a);
  return std::move(b);
}

// Example of a "derived" operation: Implementation does not require adding a
// virtual function to VariableConcept.
std::vector<Variable> split(const Variable &var, const Dim dim,
                            const std::vector<gsl::index> &indices) {
  if (indices.empty())
    return {var};
  std::vector<Variable> vars;
  vars.emplace_back(var(dim, 0, indices.front()));
  for (gsl::index i = 0; i < static_cast<gsl::index>(indices.size()) - 1; ++i)
    vars.emplace_back(var(dim, indices[i], indices[i + 1]));
  vars.emplace_back(var(dim, indices.back(), var.dimensions()[dim]));
  return vars;
}

Variable concatenate(const Variable &a1, const Variable &a2, const Dim dim) {
  if (a1.tag() != a2.tag())
    throw std::runtime_error(
        "Cannot concatenate Variables: Data types do not match.");
  if (a1.unit() != a2.unit())
    throw std::runtime_error(
        "Cannot concatenate Variables: Units do not match.");
  if (a1.name() != a2.name())
    throw std::runtime_error(
        "Cannot concatenate Variables: Names do not match.");
  const auto &dims1 = a1.dimensions();
  const auto &dims2 = a2.dimensions();
  // TODO Many things in this function should be refactored and moved in class
  // Dimensions.
  // TODO Special handling for edge variables.
  for (const auto &dim1 : dims1.labels()) {
    if (dim1 != dim) {
      if (!dims2.contains(dim1))
        throw std::runtime_error(
            "Cannot concatenate Variables: Dimensions do not match.");
      if (dims2[dim1] != dims1[dim1])
        throw std::runtime_error(
            "Cannot concatenate Variables: Dimension extents do not match.");
    }
  }
  auto size1 = dims1.count();
  auto size2 = dims2.count();
  if (dims1.contains(dim))
    size1--;
  if (dims2.contains(dim))
    size2--;
  // This check covers the case of dims2 having extra dimensions not present in
  // dims1.
  // TODO Support broadcast of dimensions?
  if (size1 != size2)
    throw std::runtime_error(
        "Cannot concatenate Variables: Dimensions do not match.");

  auto out(a1);
  auto dims(dims1);
  gsl::index extent1 = 1;
  gsl::index extent2 = 1;
  if (dims1.contains(dim))
    extent1 += dims1[dim] - 1;
  if (dims2.contains(dim))
    extent2 += dims2[dim] - 1;
  if (dims.contains(dim))
    dims.resize(dim, extent1 + extent2);
  else
    dims.add(dim, extent1 + extent2);
  out.setDimensions(dims);

  out.data().copy(a1.data(), dim, 0, 0, extent1);
  out.data().copy(a2.data(), dim, extent1, 0, extent2);

  return out;
}

Variable rebin(const Variable &var, const Variable &oldCoord,
               const Variable &newCoord) {
  dataset::expect::countsOrCountsDensity(var);
  const Dim dim = coordDimension[newCoord.tag().value()];
  if (var.unit() == units::counts ||
      var.unit() == units::counts * units::counts) {
    auto dims = var.dimensions();
    dims.resize(dim, newCoord.dimensions()[dim] - 1);
    Variable rebinned(var, dims);
    if (rebinned.dimensions().inner() == dim) {
      require<FloatingPointVariableConcept>(rebinned.data())
          .rebin(var.data(), dim, oldCoord.data(), newCoord.data());
    } else {
      if (newCoord.dimensions().ndim() != 1 ||
          oldCoord.dimensions().ndim() != 1)
        throw std::runtime_error(
            "Not inner rebin works only for 1d coordinates for now.");
      switch (rebinned.dtype()) {
      case dtype<double>:
        RebinGeneralHelper<double>::rebin(dim, var, rebinned, oldCoord,
                                          newCoord);
        break;
      case dtype<float>:
        RebinGeneralHelper<float>::rebin(dim, var, rebinned, oldCoord,
                                         newCoord);
        break;
      default:
        throw std::runtime_error(
            "Rebinning is possible only for double and float types.");
      }
    }
    return rebinned;
  } else {
    // TODO This will currently fail if the data is a multi-dimensional density.
    // Would need a conversion that converts only the rebinned dimension.
    // TODO This could be done more efficiently without a temporary Dataset.
    Dataset density;
    density.insert(oldCoord);
    density.insert(var);
    auto cnts = counts::fromDensity(std::move(density), dim)
                    .erase(var.tag(), var.name());
    Dataset rebinnedCounts;
    rebinnedCounts.insert(newCoord);
    rebinnedCounts.insert(rebin(cnts, oldCoord, newCoord));
    return counts::toDensity(std::move(rebinnedCounts), dim)
        .erase(var.tag(), var.name());
  }
}

Variable permute(const Variable &var, const Dim dim,
                 const std::vector<gsl::index> &indices) {
  auto permuted(var);
  for (size_t i = 0; i < indices.size(); ++i)
    permuted.data().copy(var.data(), dim, i, indices[i], indices[i] + 1);
  return permuted;
}

Variable filter(const Variable &var, const Variable &filter) {
  if (filter.dimensions().ndim() != 1)
    throw std::runtime_error(
        "Cannot filter variable: The filter must by 1-dimensional.");
  const auto dim = filter.dimensions().labels()[0];
  auto mask = filter.get(Coord::Mask);

  const gsl::index removed = std::count(mask.begin(), mask.end(), 0);
  if (removed == 0)
    return var;

  auto out(var);
  auto dims = out.dimensions();
  dims.resize(dim, dims[dim] - removed);
  out.setDimensions(dims);

  gsl::index iOut = 0;
  // Note: Could copy larger chunks of applicable for better(?) performance.
  // Note: This implementation is inefficient, since we need to cast to concrete
  // type for *every* slice. Should be combined into a single virtual call.
  for (gsl::index iIn = 0; iIn < mask.size(); ++iIn)
    if (mask[iIn])
      out.data().copy(var.data(), dim, iOut++, iIn, iIn + 1);
  return out;
}

Variable sum(const Variable &var, const Dim dim) {
  auto summed(var);
  auto dims = summed.dimensions();
  dims.erase(dim);
  // setDimensions zeros the data
  summed.setDimensions(dims);
  require<ArithmeticVariableConcept>(summed.data()) += var.data();
  return summed;
}

Variable mean(const Variable &var, const Dim dim) {
  auto summed = sum(var, dim);
  double scale = 1.0 / static_cast<double>(var.dimensions()[dim]);
  return summed * Variable(Data::Value, {}, {scale});
}

Variable norm(const Variable &var) {
  return {var, require<const ArithmeticVariableConcept>(var.data()).norm()};
}

Variable sqrt(const Variable &var) {
  Variable result(
      var, require<const FloatingPointVariableConcept>(var.data()).sqrt());
  result.setUnit(sqrt(var.unit()));
  return result;
}

Variable broadcast(Variable var, const Dimensions &dims) {
  if (var.dimensions().contains(dims))
    return std::move(var);
  auto newDims = var.dimensions();
  for (const auto label : dims.labels()) {
    if (newDims.contains(label))
      dataset::expect::dimensionMatches(newDims, label, dims[label]);
    else
      newDims.add(label, dims[label]);
  }
  Variable result(var);
  result.setDimensions(newDims);
  result.data().copy(var.data(), Dim::Invalid, 0, 0, 1);
  return result;
}

void swap(Variable &var, const Dim dim, const gsl::index a,
          const gsl::index b) {
  const Variable tmp = var(dim, a);
  var(dim, a).assign(var(dim, b));
  var(dim, b).assign(tmp);
}

Variable reverse(Variable var, const Dim dim) {
  const auto size = var.dimensions()[dim];
  for (gsl::index i = 0; i < size / 2; ++i)
    swap(var, dim, i, size - i - 1);
  return std::move(var);
}

template <>
VariableView<const double> getView<double>(const Variable &var,
                                           const Dimensions &dims) {
  return requireT<const VariableConceptT<double>>(var.data()).getView(dims);
}
