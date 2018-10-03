/// @file
/// SPDX-License-Identifier: GPL-3.0-or-later
/// @author Simon Heybrock
/// Copyright &copy; 2018 ISIS Rutherford Appleton Laboratory, NScD Oak Ridge
/// National Laboratory, and European Spallation Source ERIC.
#ifndef TAGS_H
#define TAGS_H

#include <tuple>
#include <vector>

#include <gsl/gsl_util>

#include "unit.h"

namespace detail {
template <class T, class Tuple> struct index;
template <class T, class... Types> struct index<T, std::tuple<T, Types...>> {
  static const std::size_t value = 0;
};
template <class T, class U, class... Types>
struct index<T, std::tuple<U, Types...>> {
  static const std::size_t value = 1 + index<T, std::tuple<Types...>>::value;
};
struct ReturnByValuePolicy {};
struct FlexibleTypePolicy {};
} // namespace detail

struct Coord {
  struct X {
    using type = double;
    static constexpr auto unit = Unit::Id::Length;
  };
  struct Y {
    using type = double;
    static constexpr auto unit = Unit::Id::Length;
  };
  struct Z {
    using type = double;
    static constexpr auto unit = Unit::Id::Length;
  };
  struct Tof {
    using type = double;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct MonitorTof {
    using type = double;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct DetectorId {
    using type = int32_t;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct SpectrumNumber {
    using type = int32_t;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct DetectorPosition {
    // Dummy for now, should be something like Eigen::Vector3d.
    using type = double;
    static constexpr auto unit = Unit::Id::Length;
  };
  struct DetectorGrouping {
    // Dummy for now, or sufficient like this?
    using type = std::vector<gsl::index>;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct SpectrumPosition : public detail::ReturnByValuePolicy {
    using type = double;
  };
  struct RowLabel {
    using type = std::string;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct Polarization {
    // Dummy for now
    using type = std::string;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct Temperature {
    using type = double;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct TimeInterval {
    using type = std::pair<int64_t, int64_t>;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct Mask {
    using type = char;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };

  using tags =
      std::tuple<X, Y, Z, Tof, MonitorTof, DetectorId, SpectrumNumber,
                 DetectorPosition, DetectorGrouping, SpectrumPosition, RowLabel,
                 Polarization, Temperature, TimeInterval, Mask>;
};

class Dataset;
struct Data {
  struct Tof {
    using type = double;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct PulseTime {
    using type = double;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct Any : public detail::FlexibleTypePolicy {
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct Value {
    using type = double;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct Variance {
    using type = double;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct StdDev : public detail::ReturnByValuePolicy {
    using type = double;
  };
  struct Int {
    using type = int64_t;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct DimensionSize {
    using type = gsl::index;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct String {
    using type = std::string;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct History {
    using type = std::vector<std::string>;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };
  struct Events {
    using type = Dataset;
    static constexpr auto unit = Unit::Id::Dimensionless;
  };

  using tags = std::tuple<Tof, PulseTime, Any, Value, Variance, StdDev, Int,
                          DimensionSize, String, History, Events>;
};

using Tags = decltype(
    std::tuple_cat(std::declval<Coord::tags>(), std::declval<Data::tags>()));
template <class T>
static constexpr uint16_t tag_id =
    detail::index<std::remove_const_t<T>, Tags>::value;
template <class T>
static constexpr bool is_coord =
    tag_id<T> < std::tuple_size<Coord::tags>::value;

template <class Tag, class Type = void, class Enable = void> struct tag_type;
template <class Tag> struct tag_type<Tag> { using type = typename Tag::type; };
template <class Tag, class Type>
struct tag_type<
    Tag, Type,
    std::enable_if_t<std::is_base_of<detail::FlexibleTypePolicy, Tag>::value>> {
  using type = Type;
};
template <class... Args> using tag_type_t = typename tag_type<Args...>::type;

template <class Tag, class Enable = void>
struct tag_has_type : public std::true_type {};
template <class Tag>
struct tag_has_type<
    Tag,
    std::enable_if_t<std::is_base_of<detail::FlexibleTypePolicy, Tag>::value>>
    : public std::false_type {};

class DataBin {
public:
  DataBin(const double left, const double right)
      : m_left(left), m_right(right) {}

  double center() const { return 0.5 * (m_left + m_right); }
  double width() const { return m_right - m_left; }
  double left() const { return m_left; }
  double right() const { return m_right; }

private:
  double m_left;
  double m_right;
};

template <class T> struct Bin { using type = DataBin; };

template <class Tag> struct element_return_type {
  using type = std::conditional_t<
      std::is_base_of<detail::ReturnByValuePolicy, Tag>::value,
      typename Tag::type,
      std::conditional_t<std::is_const<Tag>::value, const typename Tag::type &,
                         typename Tag::type &>>;
};

template <class Tags> struct element_return_type<Bin<Tags>> {
  using type = DataBin;
};

template <class... Ts> class DatasetViewImpl;
template <class... Tags> struct element_return_type<DatasetViewImpl<Tags...>> {
  using type = DatasetViewImpl<Tags...>;
};

template <class Tag>
using element_return_type_t = typename element_return_type<Tag>::type;

#endif // TAGS_H
