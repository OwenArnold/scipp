// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2019 Scipp contributors (https://github.com/scipp)
/// @file
/// @author Simon Heybrock
#ifndef SCIPPY_BIND_DATA_ACCESS_H
#define SCIPPY_BIND_DATA_ACCESS_H

#include <variant>

#include "scipp/core/dataset.h"
#include "scipp/core/dtype.h"
#include "scipp/core/except.h"
#include "scipp/core/tag_util.h"
#include "scipp/core/variable.h"

#include "numpy.h"
#include "pybind11.h"

namespace py = pybind11;
using namespace scipp;
using namespace scipp::core;

/// Add element size as factor to strides.
template <class T>
std::vector<scipp::index> numpy_strides(const std::vector<scipp::index> &s) {
  std::vector<scipp::index> strides(s.size());
  scipp::index elemSize = sizeof(T);
  for (size_t i = 0; i < strides.size(); ++i) {
    strides[i] = elemSize * s[i];
  }
  return strides;
}

template <class Var>
void expect_shape_compatible(const Var &view, const py::array &data) {
  const auto &dims = view.dims();
  if (dims.sparse()) {
    // Sparse data can be set from an array only for a single item.
    if (dims.shape().size() != 0)
      throw except::DimensionError("Sparse data cannot be set from a single "
                                   "array, unless the sparse dimension is the "
                                   "only dimension.");
    if (data.ndim() != 1)
      throw except::DimensionError("Expected 1-D data.");
  } else {
    if (dims.shape().size() != data.ndim())
      throw except::DimensionError(
          "The shape of the provided data does not match the existing object.");
    for (int i = 0; i < dims.shape().size(); ++i)
      if (dims.shape()[i] != data.shape()[i])
        throw except::DimensionError("The shape of the provided data does not "
                                     "match the existing object.");
  }
}

template <class T> struct MakePyBufferInfoT {
  static py::buffer_info apply(VariableProxy &view) {
    const auto &dims = view.dims();
    return py::buffer_info(
        view.template values<T>().data(), /* Pointer to buffer */
        sizeof(T),                        /* Size of one scalar */
        py::format_descriptor<
            std::conditional_t<std::is_same_v<T, bool>, bool, T>>::
            format(),              /* Python struct-style format descriptor */
        scipp::size(dims.shape()), /* Number of dimensions */
        dims.shape(),              /* Buffer dimensions */
        numpy_strides<T>(view.strides()) /* Strides (in bytes) for each index */
    );
  }
};

inline py::buffer_info make_py_buffer_info(VariableProxy &view) {
  return CallDType<double, float, int64_t, int32_t,
                   bool>::apply<MakePyBufferInfoT>(view.dtype(), view);
}

template <class Getter, class T, class Var>
auto as_py_array_t_impl(py::object &obj, Var &view) {
  std::vector<scipp::index> strides;
  if constexpr (std::is_same_v<Var, DataProxy>)
    strides = VariableProxy(view.data()).strides();
  else
    strides = VariableProxy(view).strides();
  const auto &dims = view.dims();
  using py_T = std::conditional_t<std::is_same_v<T, bool>, bool, T>;
  return py::array_t<py_T>{
      dims.shape(), numpy_strides<T>(strides),
      reinterpret_cast<py_T *>(Getter::template get<T>(view).data()), obj};
}

struct get_values {
  template <class T, class Proxy> static constexpr auto get(Proxy &proxy) {
    return proxy.template values<T>();
  }
};
struct get_variances {
  template <class T, class Proxy> static constexpr auto get(Proxy &proxy) {
    return proxy.template variances<T>();
  }
};

template <class... Ts> struct as_VariableViewImpl {
  template <class Getter, class Var>
  static std::variant<std::conditional_t<
      std::is_same_v<Var, Variable>, scipp::span<underlying_type_t<Ts>>,
      VariableView<underlying_type_t<Ts>>>...>
  get(Var &view) {
    switch (view.data().dtype()) {
    case dtype<double>:
      return {Getter::template get<double>(view)};
    case dtype<float>:
      return {Getter::template get<float>(view)};
    case dtype<int64_t>:
      return {Getter::template get<int64_t>(view)};
    case dtype<int32_t>:
      return {Getter::template get<int32_t>(view)};
    case dtype<bool>:
      return {Getter::template get<bool>(view)};
    case dtype<std::string>:
      return {Getter::template get<std::string>(view)};
    case dtype<sparse_container<double>>:
      return {Getter::template get<sparse_container<double>>(view)};
    case dtype<sparse_container<float>>:
      return {Getter::template get<sparse_container<float>>(view)};
    case dtype<sparse_container<int64_t>>:
      return {Getter::template get<sparse_container<int64_t>>(view)};
    case dtype<Dataset>:
      return {Getter::template get<Dataset>(view)};
    case dtype<Eigen::Vector3d>:
      return {Getter::template get<Eigen::Vector3d>(view)};
    default:
      throw std::runtime_error("not implemented for this type.");
    }
  }

  template <class Getter, class Var>
  static py::object get_py_array_t(py::object &obj) {
    auto &view = obj.cast<Var &>();
    switch (view.data().dtype()) {
    case dtype<double>:
      return as_py_array_t_impl<Getter, double>(obj, view);
    case dtype<float>:
      return as_py_array_t_impl<Getter, float>(obj, view);
    case dtype<int64_t>:
      return as_py_array_t_impl<Getter, int64_t>(obj, view);
    case dtype<int32_t>:
      return as_py_array_t_impl<Getter, int32_t>(obj, view);
    case dtype<bool>:
      return as_py_array_t_impl<Getter, bool>(obj, view);
    default:
      return std::visit(
          [&view](const auto &data) {
            const auto &dims = view.dims();
            // We return an individual item in two cases:
            // 1. For 0-D data (consistent with numpy behavior, e.g., when
            //    slicing a 1-D array).
            // 2. For 1-D sparse data, where the individual item is then a
            //    vector-like object.
            if (dims.shape().size() == 0)
              return py::cast(data[0]);
            else
              return py::cast(data);
          },
          get<Getter>(view));
    }
  }

  template <class Var> static py::object values(py::object &object) {
    return get_py_array_t<get_values, Var>(object);
  }

  template <class Var> static py::object variances(py::object &object) {
    return get_py_array_t<get_variances, Var>(object);
  }

  template <class Proxy>
  static void set(const Proxy &proxy, const py::array &data) {
    std::visit(
        [&data](const auto &proxy_) {
          using T =
              typename std::remove_reference_t<decltype(proxy_)>::value_type;
          if constexpr (std::is_trivial_v<T>) {
            copy_flattened<T>(data, proxy_);
          } else if constexpr (is_sparse_v<T>) {
            py::array_t<typename T::value_type> data_t(data);
            auto r = data_t.unchecked();
            proxy_[0].clear();
            for (ssize_t i = 0; i < r.shape(0); ++i)
              proxy_[0].emplace_back(r(i));
          } else {
            throw std::runtime_error("Only POD types can be set from numpy.");
          }
        },
        proxy);
  }
  template <class Var>
  static void set_values(Var &view, const py::array &data) {
    expect_shape_compatible(view, data);
    set(get<get_values>(view), data);
  }
  template <class Var>
  static void set_variances(Var &view, const py::array &data) {
    expect_shape_compatible(view, data);
    set(get<get_variances>(view), data);
  }

  // Return a scalar value from a variable, implicitly requiring that the
  // variable is 0-dimensional and thus has only a single item.
  template <class Var> static py::object value(Var &view) {
    expect::equals(Dimensions(), view.dims());
    return std::visit(
        [](const auto &data) {
          return py::cast(data[0], py::return_value_policy::reference);
        },
        get<get_values>(view));
  }
  // Return a scalar variance from a variable, implicitly requiring that the
  // variable is 0-dimensional and thus has only a single item.
  template <class Var> static py::object variance(Var &view) {
    expect::equals(Dimensions(), view.dims());
    return std::visit(
        [](const auto &data) {
          return py::cast(data[0], py::return_value_policy::reference);
        },
        get<get_variances>(view));
  }
  // Set a scalar value in a variable, implicitly requiring that the
  // variable is 0-dimensional and thus has only a single item.
  template <class Var> static void set_value(Var &view, const py::object &o) {
    expect::equals(Dimensions(), view.dims());
    std::visit(
        [&o](const auto &data) {
          data[0] = o.cast<typename std::decay_t<decltype(data)>::value_type>();
        },
        get<get_values>(view));
  }
  // Set a scalar variance in a variable, implicitly requiring that the
  // variable is 0-dimensional and thus has only a single item.
  template <class Var>
  static void set_variance(Var &view, const py::object &o) {
    expect::equals(Dimensions(), view.dims());
    std::visit(
        [&o](const auto &data) {
          data[0] = o.cast<typename std::decay_t<decltype(data)>::value_type>();
        },
        get<get_variances>(view));
  }
};

using as_VariableView =
    as_VariableViewImpl<double, float, int64_t, int32_t, bool, std::string,
                        sparse_container<double>, sparse_container<float>,
                        sparse_container<int64_t>, Dataset, Eigen::Vector3d>;

template <class T, class... Ignored>
void bind_data_properties(pybind11::class_<T, Ignored...> &c) {
  c.def_property_readonly("dims", [](const T &self) { return self.dims(); },
                          py::return_value_policy::copy,
                          "The dimensions of the data (read-only).");
  c.def_property("unit", &T::unit, &T::setUnit,
                 "The physical unit of the data (writable).");

  c.def_property("values", &as_VariableView::values<T>,
                 &as_VariableView::set_values<T>,
                 "The array of values of the data (writable).");
  c.def_property("variances", &as_VariableView::variances<T>,
                 &as_VariableView::set_variances<T>,
                 "The array of variances of the data (writable).");
  c.def_property("value", &as_VariableView::value<T>,
                 &as_VariableView::set_value<T>,
                 "The only value for 0-dimensional data. Raises an exception "
                 "if the data is not 0-dimensional.");
  c.def_property("variance", &as_VariableView::variance<T>,
                 &as_VariableView::set_variance<T>,
                 "The only variance for 0-dimensional data. Raises an "
                 "exception if the data is not 0-dimensional.");
  c.def_property_readonly(
      "has_variances",
      [](T &v) {
        return v.hasVariances();
      }); // Cannot bind to member directly, see
          // https://github.com/pybind/pybind11/issues/580#issuecomment-269836784
}

#endif // SCIPPY_BIND_DATA_ACCESS_H