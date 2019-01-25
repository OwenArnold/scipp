/// @file
/// SPDX-License-Identifier: GPL-3.0-or-later
/// @author Simon Heybrock
/// Copyright &copy; 2018 ISIS Rutherford Appleton Laboratory, NScD Oak Ridge
/// National Laboratory, and European Spallation Source ERIC.
#include <numeric>
#include <set>

#include "range/v3/algorithm.hpp"
#include "range/v3/view/zip.hpp"

#include "dataset.h"
#include "tag_util.h"

Dataset::Dataset(const ConstDatasetSlice &view) {
  for (const auto &var : view)
    insert(var);
}

ConstDatasetSlice Dataset::operator[](const std::string &name) const & {
  return ConstDatasetSlice(*this, name);
}

DatasetSlice Dataset::operator[](const std::string &name) & {
  return DatasetSlice(*this, name);
}

ConstDatasetSlice Dataset::operator()(const Dim dim, const gsl::index begin,
                                      const gsl::index end) const & {
  return ConstDatasetSlice(*this)(dim, begin, end);
}

DatasetSlice Dataset::operator()(const Dim dim, const gsl::index begin,
                                 const gsl::index end) & {
  return DatasetSlice(*this)(dim, begin, end);
}

ConstVariableSlice Dataset::operator()(const Tag tag,
                                       const std::string &name) const & {
  return ConstVariableSlice(m_variables[find(tag, name)]);
}

VariableSlice Dataset::operator()(const Tag tag, const std::string &name) & {
  return VariableSlice(m_variables[find(tag, name)]);
}

void Dataset::insert(Variable variable) {
  if (variable.isCoord() && count(*this, variable.tag()))
    throw std::runtime_error("Attempt to insert duplicate coordinate.");
  if (!variable.isCoord()) {
    for (const auto &item : m_variables)
      if (item.tag() == variable.tag() && item.name() == variable.name())
        throw std::runtime_error(
            "Attempt to insert data of same type with duplicate name.");
  }
  // TODO special handling for special variables types like
  // Data::Histogram (either prevent adding, or extract into underlying
  // variables).
  mergeDimensions(variable.dimensions(),
                  coordDimension[variable.tag().value()]);
  m_variables.push_back(std::move(variable));
}

// T can be Dataset or Slice.
template <class T>
bool contains(const T &dataset, const Tag tag, const std::string &name) {
  for (gsl::index i = 0; i < dataset.size(); ++i)
    if (dataset[i].tag() == tag && dataset[i].name() == name)
      return true;
  return false;
}

bool Dataset::contains(const Tag tag, const std::string &name) const {
  return ::contains(*this, tag, name);
}

void Dataset::erase(const Tag tag, const std::string &name) {
  const auto it = m_variables.begin() + find(tag, name);
  const auto dims = it->dimensions();
  m_variables.erase(it);
  for (const auto dim : dims.labels()) {
    bool found = false;
    for (const auto &var : m_variables)
      if (var.dimensions().contains(dim))
        found = true;
    if (!found)
      m_dimensions.erase(dim);
  }
}

Dataset Dataset::extract(const std::string &name) {
  Dataset subset;
  for (auto it = m_variables.begin(); it != m_variables.end();) {
    if (it->name() == name) {
      subset.insert(*it);
      it = m_variables.erase(it);
    } else {
      ++it;
    }
  }
  if (subset.size() == 0)
    throw std::runtime_error(
        "Dataset::extract(): No matching variable found in Dataset.");
  return subset;
}

void Dataset::merge(const Dataset &other) {
  for (const auto &var : other) {
    if (var.isCoord() && contains(var.tag(), var.name())) {
      if (var != operator()(var.tag(), var.name()))
        throw std::runtime_error("Cannot merge: Coordinates do not match.");
    } else {
      insert(var);
    }
  }
}

gsl::index Dataset::find(const Tag tag, const std::string &name) const {
  return ::find(*this, tag, name);
}

void Dataset::mergeDimensions(const Dimensions &dims, const Dim coordDim) {
  for (gsl::index i = 0; i < dims.count(); ++i) {
    const auto dim = dims.label(i);
    auto size = dims.size(i);
    bool found = false;
    for (gsl::index j = 0; j < m_dimensions.count(); ++j) {
      if (m_dimensions.label(j) == dim) {
        if (m_dimensions.size(j) == size) {
          found = true;
          break;
        }
        // coordDim is `Dim::Invalid` if there is no coordinate dimension.
        if (dim == coordDim) {
          if (m_dimensions.size(j) == size - 1) {
            // This is an edge coordinate, merge reduced dimension.
            --size;
            found = true;
            break;
          }
          throw std::runtime_error(
              "Cannot insert variable into Dataset: Variable is a dimension "
              "coordiante, but the dimension length matches neither as default "
              "coordinate nor as edge coordinate.");
        } else {
          if (m_dimensions.size(j) == size + 1) {
            // If the dataset so far contains only edge variables for this
            // dimension, shrink its size.
            bool canShrink = true;
            for (const auto &var : m_variables) {
              if (var.dimensions().contains(dim) &&
                  coordDimension[var.tag().value()] != dim)
                canShrink = false;
            }
            if (canShrink) {
              m_dimensions.resize(dim, size);
              found = true;
              break;
            }
          }
          throw std::runtime_error(
              "Cannot insert variable into Dataset: Dimensions do not match.");
        }
      }
    }
    // TODO Add after checking all so we can give strong exception guarantee.
    if (!found) {
      m_dimensions.add(dim, size);
    }
  }
}

bool Dataset::operator==(const Dataset &other) const {
  return (m_dimensions == other.m_dimensions) &&
         (m_variables == other.m_variables);
}

VariableSlice DatasetSlice::operator()(const Tag tag, const std::string &name) {
  return VariableSlice(operator[](find(*this, tag, name)));
}

namespace aligned {
// Helpers to define a pointer to aligned memory.
// alignas cannot be used like this, e.g., clang rejects it. Need to find
// another way.
// template <class T> using type alignas(32) = T;
template <class T> using type = T;
template <class T> using ptr = type<T> *;

// Using restrict does not seem to help much?
#define RESTRICT __restrict
void multiply(const gsl::index size, ptr<double> RESTRICT v1,
              ptr<double> RESTRICT e1, ptr<const double> RESTRICT v2,
              ptr<const double> RESTRICT e2) {
  for (gsl::index i = 0; i < size; ++i) {
    e1[i] = e1[i] * (v2[i] * v2[i]) + e2[i] * (v1[i] * v1[i]);
    v1[i] *= v2[i];
  }
}
} // namespace aligned

class ConstVariableGroup {
public:
  ConstVariableGroup(std::initializer_list<const ConstVariableSlice *> vars)
      : m_vars(vars.begin(), vars.end()) {}
  virtual ~ConstVariableGroup() = default;

  std::vector<const ConstVariableSlice *> m_vars;
};

template <class T> class VariableGroup {
public:
  VariableGroup(std::initializer_list<T *> vars)
      : m_vars(vars.begin(), vars.end()) {}
  virtual ~VariableGroup() = default;

  virtual void operator+=(const ConstVariableGroup &other) const {
    *m_vars[0] += *other.m_vars[0];
  }
  virtual void operator-=(const ConstVariableGroup &other) const {
    *m_vars[0] -= *other.m_vars[0];
  }
  virtual void operator*=(const ConstVariableGroup &other) const {
    *m_vars[0] *= *other.m_vars[0];
  }

  std::vector<T *> m_vars;
};

template <class T> class ValueWithError : public VariableGroup<T> {
public:
  ValueWithError(T &value, T &error) : VariableGroup<T>{&value, &error} {
    if (this->m_vars.size() != 2)
      throw std::runtime_error("Value without uncertainty.");
    assert(m_vars[0].tag() == Data::Value);
    assert(m_vars[1].tag() == Data::Variance);
  }

  void operator+=(const ConstVariableGroup &other) const override {
    *this->m_vars[0] += *other.m_vars[0];
    *this->m_vars[1] += *other.m_vars[1];
  }
  void operator-=(const ConstVariableGroup &other) const override {
    *this->m_vars[0] -= *other.m_vars[0];
    *this->m_vars[1] += *other.m_vars[1];
  }
  void operator*=(const ConstVariableGroup &other) const override {
    auto &var1 = *this->m_vars[0];
    auto &var2 = *other.m_vars[0];
    auto &error1 = *this->m_vars[1];
    auto &error2 = *other.m_vars[1];
    if ((var1.dimensions() == var2.dimensions()) &&
        (var1.dimensions() == error1.dimensions()) &&
        (var1.dimensions() == error2.dimensions())) {
      // Optimization if all dimensions match, avoiding allocation of
      // temporaries and redundant streaming from memory of large array.
      error1.setUnit(var2.unit() * var2.unit() * error1.unit() +
                     var1.unit() * var1.unit() * error2.unit());
      var1.setUnit(var1.unit() * var2.unit());

      // TODO We are working with VariableSlice here, so get<> returns a
      // view, not a span, i.e., it is less efficient. May need to do this
      // differently for optimal performance.
      auto v1 = var1.template get<Data::Value>();
      const auto v2 = var2.template get<const Data::Value>();
      auto e1 = error1.template get<Data::Variance>();
      const auto e2 = error2.template get<const Data::Variance>();
      // TODO Need to ensure that data is contiguous!
      aligned::multiply(v1.size(), v1.data(), e1.data(), v2.data(), e2.data());
    } else {
      // TODO: Catch errors from unit propagation here and give a better error
      // message?
      var1.assign(error1 * (var2 * var2) + error2 * (var1 * var1));
      var1 *= var2;
    }
  }
};

/// Unified implementation for any in-place binary operation that requires
/// adding variances (+= and -=).
template <class Op, class T1, class T2>
T1 &binary_op_equals(Op op, T1 &dataset, const T2 &other) {
  std::set<std::string> names;
  for (const auto &var2 : other)
    if (var2.isData())
      names.insert(var2.name());

  for (const auto &var2 : other) {
    // Handling of missing variables:
    // - Skip if this contains more (automatic by having enclosing loop over
    //   other instead of *this).
    // - Fail if other contains more.
    try {
      auto var1 = dataset(var2.tag(), var2.name());
      if (var1.isCoord()) {
        // Coordinate variables must match
        // Strictly speaking we should allow "equivalent" coordinates, i.e.,
        // match only after projecting out any constant dimensions.
        if (!(var1 == var2))
          throw std::runtime_error(
              "Coordinates of datasets do not match. Cannot "
              "perform binary operation.");
        // TODO We could improve sharing here magically, but whether this is
        // beneficial would depend on the shared reference count in var1 and
        // var2: var1 = var2;
      } else if (var1.isData()) {
        // Data variables are added
        if (var1.tag() == Data::Variance{})
          var1 += var2;
        else
          op(var1, var2);
      } else {
        // Attribute variables are added
        // TODO Does it make sense to do this only if mismatched?
        if (var1 != var2)
          var1 += var2;
      }
    } catch (const dataset::except::VariableNotFoundError &) {
      // Note that this is handled via name, i.e., there may be values and
      // variances, i.e., two variables.
      if (var2.isData() && names.size() == 1) {
        // Only a single (named) variable in RHS, subtract from all.
        // Not a coordinate, subtract from all.
        gsl::index count = 0;
        for (auto var1 : dataset) {
          if (var1.tag() == var2.tag()) {
            ++count;
            if (var1.tag() == Data::Variance{})
              var1 += var2;
            else
              op(var1, var2);
          }
        }
        if (count == 0)
          throw std::runtime_error("Right-hand-side in binary operation "
                                   "contains variable type that is not present "
                                   "in left-hand-side.");
      } else {
        throw std::runtime_error("Right-hand-side in binary operation contains "
                                 "variable that is not present in "
                                 "left-hand-side.");
      }
    }
  }
  return dataset;
}

template <class T1, class T2> T1 &times_equals(T1 &dataset, const T2 &other) {
  // See operator+= for additional comments.
  for (const auto &var2 : other) {
    gsl::index index;
    try {
      index = find(dataset, var2.tag(), var2.name());
    } catch (const std::runtime_error &) {
      throw std::runtime_error("Right-hand-side in addition contains variable "
                               "that is not present in left-hand-side.");
    }
    if (var2.tag() == Data::Variance{}) {
      try {
        find(dataset, Data::Value{}, var2.name());
        find(other, Data::Value{}, var2.name());
      } catch (const std::runtime_error &) {
        throw std::runtime_error("Cannot multiply datasets that contain a "
                                 "variance but no corresponding value.");
      }
    }
    auto var1 = dataset[index];
    if (var1.isCoord()) {
      // Coordinate variables must match
      if (!(var1 == var2))
        throw std::runtime_error(
            "Coordinates of datasets do not match. Cannot perform addition");
    } else if (var1.isData()) {
      // Data variables are added
      if (var2.tag() == Data::Value{}) {
        if (count(dataset, Data::Variance{}, var2.name()) !=
            count(other, Data::Variance{}, var2.name()))
          throw std::runtime_error("Either both or none of the operands must "
                                   "have a variance for their values.");
        if (count(dataset, Data::Variance{}, var2.name()) != 0) {
          auto error_index1 = find(dataset, Data::Variance{}, var2.name());
          auto error_index2 = find(other, Data::Variance{}, var2.name());
          auto error1 = dataset[error_index1];
          const auto &error2 = other[error_index2];

          const ValueWithError<VariableSlice> vars1(var1, error1);
          const ConstVariableGroup vars2{&var2, &error2};
          vars1 *= vars2;
        } else {
          // No variance found, continue without.
          var1 *= var2;
        }
      } else if (var2.tag() == Data::Variance{}) {
        // Do nothing, math for variance is done when processing corresponding
        // value.
      } else {
        var1 *= var2;
      }
    }
  }
  return dataset;
}

Dataset Dataset::operator-() const {
  auto copy(*this);
  copy *= -1.0;
  return copy;
}

Dataset &Dataset::operator+=(const Dataset &other) {
  return binary_op_equals(
      [](VariableSlice &a, const ConstVariableSlice &b) { return a += b; },
      *this, other);
}
Dataset &Dataset::operator+=(const ConstDatasetSlice &other) {
  return binary_op_equals(
      [](VariableSlice &a, const ConstVariableSlice &b) { return a += b; },
      *this, other);
}
Dataset &Dataset::operator+=(const double value) {
  for (auto &var : m_variables)
    if (var.tag() == Data::Value{})
      var += value;
  return *this;
}

Dataset &Dataset::operator-=(const Dataset &other) {
  return binary_op_equals(
      [](VariableSlice &a, const ConstVariableSlice &b) { return a -= b; },
      *this, other);
}
Dataset &Dataset::operator-=(const ConstDatasetSlice &other) {
  return binary_op_equals(
      [](VariableSlice &a, const ConstVariableSlice &b) { return a -= b; },
      *this, other);
}
Dataset &Dataset::operator-=(const double value) {
  for (auto &var : m_variables)
    if (var.tag() == Data::Value{})
      var -= value;
  return *this;
}

Dataset &Dataset::operator*=(const Dataset &other) {
  return times_equals(*this, other);
}
Dataset &Dataset::operator*=(const ConstDatasetSlice &other) {
  return times_equals(*this, other);
}
Dataset &Dataset::operator*=(const double value) {
  for (auto &var : m_variables)
    if (var.tag() == Data::Value{})
      var *= value;
    else if (var.tag() == Data::Variance{})
      var *= value * value;
  return *this;
}

bool ConstDatasetSlice::contains(const Tag tag, const std::string &name) const {
  return ::contains(*this, tag, name);
}

template <class T1, class T2> T1 &assign(T1 &dataset, const T2 &other) {
  for (const auto &var2 : other) {
    gsl::index index;
    try {
      index = find(dataset, var2.tag(), var2.name());
    } catch (const std::runtime_error &) {
      throw std::runtime_error(
          "Right-hand-side in assignment contains variable "
          "that is not present in left-hand-side.");
    }
    auto var1 = dataset[index];
    if (var1.isCoord()) {
      if (!(var1 == var2))
        throw std::runtime_error(
            "Coordinates of datasets do not match. Cannot assign.");
    } else if (var1.isData()) {
      // Data variables are assigned
      var1.assign(var2);
    } else {
      // Attribute variables are assigned
      if (var1 != var2)
        var1 += var2;
    }
  }
  return dataset;
}

Dataset ConstDatasetSlice::operator-() const {
  Dataset copy(*this);
  return -copy;
}

DatasetSlice DatasetSlice::assign(const Dataset &other) {
  return ::assign(*this, other);
}
DatasetSlice DatasetSlice::assign(const ConstDatasetSlice &other) {
  return ::assign(*this, other);
}

DatasetSlice DatasetSlice::operator+=(const Dataset &other) {
  return binary_op_equals(
      [](VariableSlice &a, const ConstVariableSlice &b) { return a += b; },
      *this, other);
}
DatasetSlice DatasetSlice::operator+=(const ConstDatasetSlice &other) {
  return binary_op_equals(
      [](VariableSlice &a, const ConstVariableSlice &b) { return a += b; },
      *this, other);
}
DatasetSlice DatasetSlice::operator+=(const double value) {
  for (auto var : *this)
    if (var.tag() == Data::Value{})
      var += value;
  return *this;
}

DatasetSlice DatasetSlice::operator-=(const Dataset &other) {
  return binary_op_equals(
      [](VariableSlice &a, const ConstVariableSlice &b) { return a -= b; },
      *this, other);
}
DatasetSlice DatasetSlice::operator-=(const ConstDatasetSlice &other) {
  return binary_op_equals(
      [](VariableSlice &a, const ConstVariableSlice &b) { return a -= b; },
      *this, other);
}
DatasetSlice DatasetSlice::operator-=(const double value) {
  for (auto var : *this)
    if (var.tag() == Data::Value{})
      var -= value;
  return *this;
}

DatasetSlice DatasetSlice::operator*=(const Dataset &other) {
  return times_equals(*this, other);
}
DatasetSlice DatasetSlice::operator*=(const ConstDatasetSlice &other) {
  return times_equals(*this, other);
}
DatasetSlice DatasetSlice::operator*=(const double value) {
  for (auto var : *this)
    if (var.tag() == Data::Value{})
      var *= value;
    else if (var.tag() == Data::Variance{})
      var *= value * value;
  return *this;
}

Dataset operator+(Dataset a, const Dataset &b) { return a += b; }
Dataset operator-(Dataset a, const Dataset &b) { return a -= b; }
Dataset operator*(Dataset a, const Dataset &b) { return a *= b; }
Dataset operator+(Dataset a, const ConstDatasetSlice &b) { return a += b; }
Dataset operator-(Dataset a, const ConstDatasetSlice &b) { return a -= b; }
Dataset operator*(Dataset a, const ConstDatasetSlice &b) { return a *= b; }
Dataset operator+(Dataset a, const double b) { return a += b; }
Dataset operator-(Dataset a, const double b) { return a -= b; }
Dataset operator*(Dataset a, const double b) { return a *= b; }
Dataset operator+(const double a, Dataset b) { return b += a; }
Dataset operator-(const double a, Dataset b) { return -(b -= a); }
Dataset operator*(const double a, Dataset b) { return b *= a; }

std::vector<Dataset> split(const Dataset &d, const Dim dim,
                           const std::vector<gsl::index> &indices) {
  std::vector<Dataset> out(indices.size() + 1);
  for (const auto &var : d) {
    if (var.dimensions().contains(dim)) {
      auto vars = split(var, dim, indices);
      for (size_t i = 0; i < out.size(); ++i)
        out[i].insert(vars[i]);
    } else {
      for (auto &o : out)
        o.insert(var);
    }
  }
  return out;
}

Dataset concatenate(const Dataset &d1, const Dataset &d2, const Dim dim) {
  // Match type and name, drop missing?
  // What do we have to do to check and compute the resulting dimensions?
  // - If dim is in m_dimensions, *some* of the variables contain it. Those that
  //   do not must then be identical (do not concatenate) or we could
  //   automatically broadcast? Yes!?
  // - If dim is new, concatenate variables if different, copy if same.
  // We will be doing deep comparisons here, it would be nice if we could setup
  // sharing, but d1 and d2 are const, is there a way...? Not without breaking
  // thread safety? Could cache cow_ptr for future sharing setup, done by next
  // non-const op?
  Dataset out;
  for (gsl::index i1 = 0; i1 < d1.size(); ++i1) {
    const auto &var1 = d1[i1];
    const auto &var2 = d2(var1.tag(), var1.name());
    // TODO may need to extend things along constant dimensions to match shapes!
    if (var1.dimensions().contains(dim)) {
      const auto extent = d1.dimensions()[dim];
      if (var1.dimensions()[dim] == extent)
        out.insert(concatenate(var1, var2, dim));
      else {
        // Variable contains bin edges, check matching first/last boundary,
        // do not duplicate joint boundary.
        const auto extent2 = var2.dimensions()[dim];
        if (extent2 == d2.dimensions()[dim])
          throw std::runtime_error(
              "Cannot concatenate: Second variable is not an edge variable.");
        if (var1(dim, extent) != var2(dim, 0))
          throw std::runtime_error("Cannot concatenate: Last bin edge of first "
                                   "edge variable does not match first bin "
                                   "edge of second edge variable.");
        out.insert(concatenate(var1, var2(dim, 1, extent2), dim));
      }
    } else {
      if (var1 == var2) {
        out.insert(var1);
      } else {
        if (d1.dimensions().contains(dim)) {
          // Variable does not contain dimension but Dataset does, i.e.,
          // Variable is constant. We need to extend it before concatenating.
          throw std::runtime_error("TODO");
        } else {
          // Creating a new dimension
          out.insert(concatenate(var1, var2, dim));
        }
      }
    }
  }
  return out;
}

Dataset convert(const Dataset &d, const Dim from, const Dim to) {
  static_cast<void>(to);
  // How to convert? There are several cases:
  // 1. Tof conversion as Mantid's ConvertUnits.
  // 2. Axis conversion as Mantid's ConvertSpectrumAxis.
  // 3. Conversion of multiple dimensions simultaneuously, e.g., to Q, which
  //    cannot be done here since it affects more than one input and output
  //    dimension. Should we have a variant that accepts a list of dimensions
  //    for input and output?
  // 4. Conversion from 1 to N or N to 1, e.g., Dim::Spectrum to X and Y pixel
  //    index.
  if (!d.dimensions().contains(from))
    throw std::runtime_error(
        "Dataset does not contain the dimension requested for conversion.");
  // Can Dim::Spectrum be converted to anything? Should we require a matching
  // coordinate when doing a conversion? This does not make sense:
  // auto converted = convert(dataset, Dim::Spectrum, Dim::Tof);
  // This does if we can lookup the TwoTheta, make axis here, or require it?
  // Should it do the reordering? Is sorting separately much less efficient?
  // Dim::Spectrum is discrete, Dim::TwoTheta is in principle contiguous. How to
  // handle that? Do we simply want to sort instead? Discrete->contiguous can be
  // handled by binning? Or is Dim::TwoTheta implicitly also discrete?
  // auto converted = convert(dataset, Dim::Spectrum, Dim::TwoTheta);
  // This is a *derived* coordinate, no need to store it explicitly? May even be
  // prevented?
  // MDZipView<const Coord::TwoTheta>(dataset);
  return d;
}

Dataset rebin(const Dataset &d, const Variable &newCoord) {
  Dataset out;
  if (!newCoord.isCoord())
    throw std::runtime_error(
        "The provided rebin coordinate is not a coordinate variable.");
  const auto dim = coordDimension[newCoord.tag().value()];
  if (dim == Dim::Invalid)
    throw std::runtime_error(
        "The provided rebin coordinate is not a dimension coordinate.");
  const auto &newDims = newCoord.dimensions();
  if (!newDims.contains(dim))
    throw std::runtime_error("The provided rebin coordinate lacks the "
                             "dimension corresponding to the coordinate.");
  if (!isContinuous(dim))
    throw std::runtime_error(
        "The provided rebin coordinate is not a continuous coordinate.");
  const auto &oldCoord = d(Tag(newCoord.tag().value()));
  const auto &oldDims = oldCoord.dimensions();
  const auto &datasetDims = d.dimensions();
  if (!oldDims.contains(dim))
    throw std::runtime_error("Existing coordinate to be rebined lacks the "
                             "dimension corresponding to the new coordinate.");
  if (oldDims[dim] != datasetDims[dim] + 1)
    throw std::runtime_error("Existing coordinate to be rebinned is not a bin "
                             "edge coordinate. Use `resample` instead of rebin "
                             "or convert to histogram data first.");
  for (gsl::index i = 0; i < newDims.ndim(); ++i) {
    const auto newDim = newDims.label(i);
    if (newDim == dim)
      continue;
    if (datasetDims.contains(newDim)) {
      if (datasetDims[newDim] != newDims.shape()[i])
        throw std::runtime_error(
            "Size mismatch in auxiliary dimension of new coordinate.");
    }
  }
  // TODO check that input as well as output coordinate are sorted in rebin
  // dimension.
  for (const auto &var : d) {
    if (!var.dimensions().contains(dim)) {
      out.insert(var);
    } else if (var.tag() == newCoord.tag()) {
      out.insert(newCoord);
    } else {
      out.insert(rebin(var, oldCoord, newCoord));
    }
  }
  return out;
}

Dataset histogram(const Variable &var, const Variable &coord) {
  // TODO Is there are more generic way to find "histogrammable" data, not
  // specific to (neutron) events? Something like Data::ValueVector, i.e., any
  // data variable that contains a vector of values at each point?
  const auto &events = var.get<const Data::Events>();
  // TODO This way of handling events (and their units) as nested Dataset feels
  // a bit unwieldy. Would it be a better option to store TOF (or any derived
  // values) as simple vectors in Data::Events? There would be a separate
  // Data::PulseTimes (and Data::EventWeights). This can then be of arbitrary
  // type, unit conversion is reflected in the unit of Data::Events. The
  // implementation of `histogram` would then also be simplified since we do not
  // need to distinguish between Data::Tof, etc. (which we are anyway not doing
  // currently).
  dataset::expect::equals(events[0](Data::Tof{}).unit(), coord.unit());

  // TODO Can we reuse some code for bin handling from MDZipView?
  const auto binDim = coordDimension[coord.tag().value()];
  const gsl::index nBin = coord.dimensions()[binDim] - 1;
  Dimensions dims = var.dimensions();
  // Note that the event list contains, e.g, time-of-flight values, but *not* as
  // a coordinate. Therefore, it should not depend on, e.g., Dim::Tof.
  if (dims.contains(binDim))
    throw std::runtime_error(
        "Data to histogram depends on histogram dimension.");
  for (const auto &dim : coord.dimensions().labels()) {
    if (dim != binDim) {
      dataset::expect::dimensionMatches(dims, dim, coord.dimensions()[dim]);
    }
  }

  dims.addInner(binDim, nBin);
  const gsl::index nextEdgeOffset = coord.dimensions().offset(binDim);

  Dataset hist;
  hist.insert(coord);
  hist.insert<Data::Value>(var.name(), dims);

  // Counts has outer dimensions as input, with a new inner dimension given by
  // the binning dimensions. We iterate over all dimensions as a flat array.
  auto counts = hist.get<Data::Value>(var.name());
  gsl::index cur = 0;
  // The helper `getView` allows us to ignore the tag of coord, as long as the
  // underlying type is `double`. We view the edges with the same dimensions as
  // the output. This abstracts the differences between either a shared binning
  // axis or a potentially different binning for each event list.
  // TODO Need to add a branch for the `float` case.
  const auto edges = getView<double>(coord, dims);
  auto edge = edges.begin();
  for (const auto &eventList : events) {
    const auto tofs = eventList.get<const Data::Tof>();
    if (!std::is_sorted(tofs.begin(), tofs.end()))
      throw std::runtime_error(
          "TODO: Histograms can currently only be created from sorted data.");
    auto left = *edge;
    auto begin = std::lower_bound(tofs.begin(), tofs.end(), left);
    for (gsl::index bin = 0; bin < nBin; ++bin) {
      // The iterator cannot see the last edge, we must add the offset to the
      // memory location, *not* to the iterator.
      const auto right = *(&*edge + nextEdgeOffset);
      if (right < left)
        throw std::runtime_error(
            "Coordinate used for binning is not increasing.");
      const auto end = std::upper_bound(begin, tofs.end(), right);
      counts[cur] = std::distance(begin, end);
      begin = end;
      left = right;
      ++edge;
      ++cur;
    }
  }

  // TODO Would need to add handling for weighted events etc. here.
  hist.insert<Data::Variance>(var.name(), dims, counts.begin(), counts.end());
  return hist;
}

Dataset histogram(const Dataset &d, const Variable &coord) {
  Dataset hist;
  for (const auto &var : d)
    if (var.tag() == Data::Events{})
      hist.merge(histogram(var, coord));
  if (hist.size() == 0)
    throw std::runtime_error("Dataset does not contain any variables with "
                             "event data, cannot histogram.");
  return hist;
}

// We can specialize this to switch to a more efficient variant when sorting
// datasets that represent events lists, using ZipView.
template <class Tag> struct Sort {
  static Dataset apply(const Dataset &d, const std::string &name) {
    auto const_axis = d.get<const Tag>(name);
    if (d(Tag{}, name).dimensions().count() != 1)
      throw std::runtime_error("Axis for sorting must be 1-dimensional.");
    const auto sortDim = d(Tag{}, name).dimensions().label(0);
    if (const_axis.size() != d.dimensions()[sortDim])
      throw std::runtime_error("Axis for sorting cannot be a bin-edge axis.");
    if (std::is_sorted(const_axis.begin(), const_axis.end()))
      return d;

    Dataset sorted;
    Variable axisVar = d(Tag{}, name);
    auto axis = axisVar.template get<Tag>();
    std::vector<gsl::index> indices(axis.size());
    std::iota(indices.begin(), indices.end(), 0);
    auto view = ranges::view::zip(axis, indices);
    using ranges::sort;
    sort(view.begin(), view.end(), [](const auto &a, const auto &b) {
      return std::get<0>(a) < std::get<0>(b);
    });
    // Joint code for all tags, extract into function to reduce instantiated
    // code size?
    for (const auto &var : d) {
      if (!var.dimensions().contains(sortDim))
        sorted.insert(var);
      else if (var.tag() == Tag{} && var.name() == name)
        sorted.insert(axisVar);
      else
        sorted.insert(permute(var, sortDim, indices));
    }
    return sorted;
  }
};

Dataset sort(const Dataset &d, const Tag t, const std::string &name) {
  // Another helper `callForSortableTag` that could be added in tag_util.h would
  // allow for generic support for all valid tags. Would simply need to filter
  // all tags based on whether Tag::type has `operator<`, using some meta
  // programming.
  return Call<Coord::RowLabel, Coord::X, Data::Value>::apply<Sort>(t, d, name);
}

Dataset filter(const Dataset &d, const Variable &select) {
  if (select.dimensions().ndim() != 1)
    throw std::runtime_error(
        "Cannot filter variable: The filter must by 1-dimensional.");
  const auto dim = select.dimensions().labels()[0];

  Dataset filtered;
  for (auto var : d)
    if (var.dimensions().contains(dim))
      filtered.insert(filter(var, select));
    else
      filtered.insert(var);
  return filtered;
}

Dataset sum(const Dataset &d, const Dim dim) {
  Dataset summed;
  for (auto var : d) {
    if (var.dimensions().contains(dim)) {
      if (var.isData())
        summed.insert(sum(var, dim));
    } else {
      summed.insert(var);
    }
  }
  return summed;
}

Dataset mean(const Dataset &d, const Dim dim) {
  // TODO This is a naive mean not taking into account the axis. Should this do
  // something smarter for unevenly spaced data?
  for (auto var : d) {
    const Dim coordDim = coordDimension[var.tag().value()];
    if (coordDim != Dim::Invalid && coordDim != dim) {
      if (var.dimensions().contains(dim))
        throw std::runtime_error(
            std::string("Cannot compute mean along ") +
            dataset::to_string(dim).c_str() +
            ": Dimension coordinate for dimension " +
            dataset::to_string(coordDim).c_str() +
            " depends also on the dimension. Rebin to common axis first.");
    }
  }
  Dataset m;
  for (auto var : d) {
    if (var.dimensions().contains(dim)) {
      if (var.isData()) {
        if (var.tag() == Data::Variance{}) {
          // Standard deviation of the mean has an extra 1/sqrt(N). Note that
          // this is not included by the stand-alone mean(Variable), since that
          // would be confusing.
          double scale = 1.0 / sqrt(static_cast<double>(var.dimensions()[dim]));
          m.insert(mean(var, dim) * Variable(Data::Value{}, {}, {scale}));
        } else {
          m.insert(mean(var, dim));
        }
      }
    } else {
      m.insert(var);
    }
  }
  return m;
}

Dataset integrate(const Dataset &d, const Dim dim) {
  for (auto var : d) {
    const Dim coordDim = coordDimension[var.tag().value()];
    if (coordDim != Dim::Invalid && coordDim != dim) {
      if (var.dimensions().contains(dim))
        throw std::runtime_error(
            std::string("Cannot compute mean along ") +
            dataset::to_string(dim).c_str() +
            ": Dimension coordinate for dimension " +
            dataset::to_string(coordDim).c_str() +
            " depends also on the dimension. Rebin to common axis first.");
    }
  }
  for (auto var : d) {
    const Dim coordDim = coordDimension[var.tag().value()];
    if (coordDim == dim) {
      const auto size = var.dimensions()[dim];
      if (size != d.dimensions()[dim] + 1)
        throw std::runtime_error("Cannot integrate: Implemented only for "
                                 "histogram data (requires bin-edge "
                                 "coordinate.");
      const auto range = concatenate(var(dim, 0), var(dim, size - 1), dim);
      const auto integral = rebin(d, range);
      // TODO Unless unit is "counts" we need to multiply by the interval
      // length. To fix this properly we need support for non-count data in
      // `rebin`.
      // Return slice to automatically drop `dim` and corresponding coordinate.
      return integral(dim, 0);
    }
  }
  throw std::runtime_error(
      "Integration required bin-edge dimension coordinate.");
}
