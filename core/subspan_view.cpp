// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2019 Scipp contributors (https://github.com/scipp)
/// @file
/// @author Simon Heybrock
#include "scipp/core/subspan_view.h"

namespace scipp::core {

/// Helper returning vector of subspans with given length.
template <class T>
auto make_subspans(const span<T> &data, const scipp::index span_len) {
  const auto len = data.size() / span_len;
  std::vector<span<T>> spans;
  spans.reserve(len);
  for (scipp::index i = 0; i < len; ++i)
    spans.emplace_back(data.subspan(span_len * i, span_len));
  return spans;
}

template <class T>
auto make_subspans(const VariableView<T> &data, const scipp::index span_len) {
  return make_subspans(scipp::span(data.data(), data.data() + data.size()),
                       span_len);
}

/// Return Variable containing spans over given dimension as elements.
template <class T, class Var> Variable subspan_view(Var &var, const Dim dim) {
  expect::notSparse(var);
  if (dim != var.dims().inner())
    throw except::DimensionError(
        "View over subspan can only be created for inner dimension.");
  if (!var.data().isContiguous())
    throw except::DimensionError(
        "View over subspan can only be created for contiguous range of data.");

  using E = std::remove_const_t<T>;
  constexpr static auto values_view = [](auto &v) {
    return make_subspans(v.template values<E>(), v.dims()[v.dims().inner()]);
  };
  constexpr static auto variances_view = [](auto &v) {
    return make_subspans(v.template variances<E>(), v.dims()[v.dims().inner()]);
  };

  auto dims = var.dims();
  dims.erase(dim);
  std::conditional_t<std::is_const_v<T>, const Var, Var> &var_ref = var;
  using MaybeConstT =
      std::conditional_t<std::is_const_v<Var>, std::add_const_t<T>, T>;
  return makeVariable<span<MaybeConstT>>(
      dims, var.unit(), values_view(var_ref),
      var.hasVariances() ? variances_view(var_ref)
                         : std::vector<span<MaybeConstT>>{});
}

template <class... Ts, class... Args>
auto invoke(const DType dtype, Args &&... args) {
  using Ret = decltype(subspan_view<double>(std::forward<Args>(args)...));
  Ret ret;
  if (!((scipp::core::dtype<Ts> == dtype
             ? (ret = subspan_view<Ts>(std::forward<Args>(args)...), true)
             : false) ||
        ...))
    throw std::runtime_error("Unsupported dtype.");
  return ret;
}

template <class Var> Variable subspan_view_impl(Var &var, const Dim dim) {
  return invoke<double, float>(var.dtype(), var, dim);
}

/// Return Variable containing mutable spans over given dimension as elements.
Variable subspan_view(Variable &var, const Dim dim) {
  return subspan_view_impl(var, dim);
}
/// Return Variable containing mutable spans over given dimension as elements.
Variable subspan_view(const VariableProxy &var, const Dim dim) {
  return subspan_view_impl(var, dim);
}
/// Return Variable containing const spans over given dimension as elements.
Variable subspan_view(const VariableConstProxy &var, const Dim dim) {
  return subspan_view_impl(var, dim);
}


} // namespace scipp::core
