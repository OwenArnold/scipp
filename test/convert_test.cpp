/// @file
/// SPDX-License-Identifier: GPL-3.0-or-later
/// @author Simon Heybrock
/// Copyright &copy; 2019 ISIS Rutherford Appleton Laboratory, NScD Oak Ridge
/// National Laboratory, and European Spallation Source ERIC.
#include "test_macros.h"
#include <gtest/gtest.h>

#include "convert.h"
#include "counts.h"
#include "dataset.h"
#include "dimensions.h"

Dataset makeTofDataForUnitConversion() {
  Dataset tof;

  tof.insert(Coord::Tof, {Dim::Tof, 4}, {1000, 2000, 3000, 4000});

  Dataset components;
  // Source and sample
  components.insert(
      Coord::Position, {Dim::Component, 2},
      {Eigen::Vector3d{0.0, 0.0, -10.0}, Eigen::Vector3d{0.0, 0.0, 0.0}});
  tof.insert(Coord::ComponentInfo, {}, {components});
  tof.insert(Coord::Position, {Dim::Spectrum, 2},
             {Eigen::Vector3d{0.0, 0.0, 1.0}, Eigen::Vector3d{0.1, 0.0, 1.0}});

  tof.insert(Data::Value, "counts", {{Dim::Spectrum, 2}, {Dim::Tof, 3}},
             {1, 2, 3, 4, 5, 6});
  tof(Data::Value, "counts").setUnit(units::counts);

  tof.insert(Data::Value, "counts/us", {{Dim::Spectrum, 2}, {Dim::Tof, 3}},
             {1, 2, 3, 4, 5, 6});
  tof(Data::Value, "counts/us").setUnit(units::counts / units::us);

  return tof;
}

TEST(Dataset, convert) {
  Dataset tof = makeTofDataForUnitConversion();

  auto energy = convert(tof, Dim::Tof, Dim::Energy);

  ASSERT_FALSE(energy.dimensions().contains(Dim::Tof));
  ASSERT_TRUE(energy.dimensions().contains(Dim::Energy));
  EXPECT_EQ(energy.dimensions()[Dim::Energy], 3);

  ASSERT_FALSE(energy.contains(Coord::Tof));
  ASSERT_TRUE(energy.contains(Coord::Energy));
  const auto &coord = energy(Coord::Energy);
  // Due to conversion, the coordinate now also depends on Dim::Spectrum.
  ASSERT_EQ(coord.dimensions(),
            Dimensions({{Dim::Spectrum, 2}, {Dim::Energy, 4}}));
  EXPECT_EQ(coord.unit(), units::meV);

  const auto values = coord.get(Coord::Energy);
  // Rule of thumb (https://www.psi.ch/niag/neutron-physics):
  // v [m/s] = 437 * sqrt ( E[meV] )
  Variable tof_in_seconds = tof(Coord::Tof) * 1e-6;
  const auto tofs = tof_in_seconds.get(Coord::Tof);
  // Spectrum 0 is 11 m from source
  EXPECT_NEAR(values[0], pow((11.0 / tofs[0]) / 437.0, 2), values[0] * 0.01);
  EXPECT_NEAR(values[1], pow((11.0 / tofs[1]) / 437.0, 2), values[1] * 0.01);
  EXPECT_NEAR(values[2], pow((11.0 / tofs[2]) / 437.0, 2), values[2] * 0.01);
  EXPECT_NEAR(values[3], pow((11.0 / tofs[3]) / 437.0, 2), values[3] * 0.01);
  // Spectrum 1
  const double L = 10.0 + sqrt(1.0 * 1.0 + 0.1 * 0.1);
  EXPECT_NEAR(values[4], pow((L / tofs[0]) / 437.0, 2), values[4] * 0.01);
  EXPECT_NEAR(values[5], pow((L / tofs[1]) / 437.0, 2), values[5] * 0.01);
  EXPECT_NEAR(values[6], pow((L / tofs[2]) / 437.0, 2), values[6] * 0.01);
  EXPECT_NEAR(values[7], pow((L / tofs[3]) / 437.0, 2), values[7] * 0.01);

  ASSERT_TRUE(energy.contains(Data::Value, "counts"));
  const auto &data = energy(Data::Value, "counts");
  ASSERT_EQ(data.dimensions(),
            Dimensions({{Dim::Spectrum, 2}, {Dim::Energy, 3}}));
  EXPECT_TRUE(equals(data.get(Data::Value), {1, 2, 3, 4, 5, 6}));
  EXPECT_EQ(data.unit(), units::counts);

  ASSERT_TRUE(energy.contains(Data::Value, "counts/us"));
  const auto &density = energy(Data::Value, "counts/us");
  ASSERT_EQ(density.dimensions(),
            Dimensions({{Dim::Spectrum, 2}, {Dim::Energy, 3}}));
  EXPECT_FALSE(equals(density.get(Data::Value), {1, 2, 3, 4, 5, 6}));
  EXPECT_EQ(density.unit(), units::counts / units::meV);

  ASSERT_TRUE(energy.contains(Coord::Position));
  ASSERT_TRUE(energy.contains(Coord::ComponentInfo));
}

TEST(Dataset, convert_to_energy_fails_for_inelastic) {
  Dataset tof = makeTofDataForUnitConversion();

  // Note these conversion fail only because they are not implemented. It should
  // definitely be possible to support this.

  tof.insert(Coord::Ei, {}, {1});
  EXPECT_THROW_MSG(convert(tof, Dim::Tof, Dim::Energy), std::runtime_error,
                   "Dataset contains Coord::Ei or Coord::Ef. However, "
                   "conversion to Dim::Energy is currently only supported for "
                   "elastic scattering.");
  tof.erase(Coord::Ei);

  tof.insert(Coord::Ef, {Dim::Spectrum, 2}, {1.0, 1.5});
  EXPECT_THROW_MSG(convert(tof, Dim::Tof, Dim::Energy), std::runtime_error,
                   "Dataset contains Coord::Ei or Coord::Ef. However, "
                   "conversion to Dim::Energy is currently only supported for "
                   "elastic scattering.");
  tof.erase(Coord::Ef);

  EXPECT_NO_THROW(convert(tof, Dim::Tof, Dim::Energy));
}

TEST(Dataset, convert_direct_inelastic) {
  Dataset tof;

  tof.insert(Coord::Tof, {Dim::Tof, 4}, {30000, 31000, 32000, 33000});

  Dataset components;
  // Source and sample
  components.insert(
      Coord::Position, {Dim::Component, 2},
      {Eigen::Vector3d{0.0, 0.0, -10.0}, Eigen::Vector3d{0.0, 0.0, 0.0}});
  tof.insert(Coord::ComponentInfo, {}, {components});
  tof.insert(Coord::Position, {Dim::Spectrum, 3},
             {Eigen::Vector3d{0.0, 0.0, 1.0}, Eigen::Vector3d{0.0, 0.0, 1.0},
              Eigen::Vector3d{0.1, 0.0, 1.0}});

  tof.insert(Data::Value, "", {{Dim::Spectrum, 3}, {Dim::Tof, 3}},
             {1, 2, 3, 4, 5, 6, 7, 8, 9});
  tof(Data::Value, "").setUnit(units::counts);

  tof.insert(Coord::Ei, {}, {1});

  auto energy = convert(tof, Dim::Tof, Dim::DeltaE);

  ASSERT_FALSE(energy.dimensions().contains(Dim::Tof));
  ASSERT_TRUE(energy.dimensions().contains(Dim::DeltaE));
  EXPECT_EQ(energy.dimensions()[Dim::DeltaE], 3);

  ASSERT_FALSE(energy.contains(Coord::Tof));
  ASSERT_TRUE(energy.contains(Coord::DeltaE));
  const auto &coord = energy(Coord::DeltaE);
  // Due to conversion, the coordinate now also depends on Dim::Spectrum.
  ASSERT_EQ(coord.dimensions(),
            Dimensions({{Dim::Spectrum, 3}, {Dim::DeltaE, 4}}));
  // TODO Check actual values here after conversion is fixed.
  for (gsl::index spec = 0; spec < coord.dimensions()[Dim::Spectrum]; ++spec) {
    const auto edges = coord(Dim::Spectrum, spec).span<double>();
    EXPECT_TRUE(std::is_sorted(edges.begin(), edges.end()));
  }

  EXPECT_FALSE(
      equals(coord.get(Coord::DeltaE), {1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4}));
  // 2 spectra at same position see same deltaE.
  EXPECT_EQ(coord(Dim::Spectrum, 0).get(Coord::DeltaE)[0],
            coord(Dim::Spectrum, 1).get(Coord::DeltaE)[0]);
  EXPECT_EQ(coord.unit(), units::meV);

  ASSERT_TRUE(energy.contains(Data::Value));
  const auto &data = energy(Data::Value);
  ASSERT_EQ(data.dimensions(),
            Dimensions({{Dim::Spectrum, 3}, {Dim::DeltaE, 3}}));
  EXPECT_TRUE(equals(data.get(Data::Value), {1, 2, 3, 4, 5, 6, 7, 8, 9}));
  EXPECT_EQ(data.unit(), units::counts);

  ASSERT_TRUE(energy.contains(Coord::Position));
  ASSERT_TRUE(energy.contains(Coord::ComponentInfo));
  ASSERT_TRUE(energy.contains(Coord::Ei));
}

Dataset makeMultiEiTofData() {
  Dataset tof;
  tof.insert(Coord::Tof, {Dim::Tof, 4}, {30000, 31000, 32000, 33000});

  Dataset components;
  // Source and sample
  components.insert(
      Coord::Position, {Dim::Component, 2},
      {Eigen::Vector3d{0.0, 0.0, -10.0}, Eigen::Vector3d{0.0, 0.0, 0.0}});
  tof.insert(Coord::ComponentInfo, {}, {components});
  tof.insert(Coord::Position, {Dim::Position, 3},
             {Eigen::Vector3d{0.0, 0.0, 1.0}, Eigen::Vector3d{0.0, 0.0, 1.0},
              Eigen::Vector3d{0.1, 0.0, 1.0}});

  tof.insert(Data::Value, "", {{Dim::Position, 3}, {Dim::Tof, 3}},
             {1, 2, 3, 4, 5, 6, 7, 8, 9});
  tof(Data::Value, "").setUnit(units::counts);

  // In practice not every spectrum would have a different Ei, more likely we
  // would have an extra dimension, Dim::Ei in addition to Dim::Position.
  tof.insert(Coord::Ei, {Dim::Position, 3}, {10.0, 10.5, 11.0});
  return tof;
}

TEST(Dataset, convert_direct_inelastic_multi_Ei) {
  const auto tof = makeMultiEiTofData();

  auto energy = convert(tof, Dim::Tof, Dim::DeltaE);

  ASSERT_FALSE(energy.dimensions().contains(Dim::Tof));
  ASSERT_TRUE(energy.dimensions().contains(Dim::DeltaE));
  EXPECT_EQ(energy.dimensions()[Dim::DeltaE], 3);

  ASSERT_FALSE(energy.contains(Coord::Tof));
  ASSERT_TRUE(energy.contains(Coord::DeltaE));
  const auto &coord = energy(Coord::DeltaE);
  // Due to conversion, the coordinate now also depends on Dim::Position.
  ASSERT_EQ(coord.dimensions(),
            Dimensions({{Dim::Position, 3}, {Dim::DeltaE, 4}}));
  // TODO Check actual values here after conversion is fixed.
  EXPECT_FALSE(
      equals(coord.get(Coord::DeltaE), {1000, 2000, 3000, 4000, 1000, 2000,
                                        3000, 4000, 1000, 2000, 3000, 4000}));
  // 2 spectra at same position, but now their Ei differs, so deltaE is also
  // different (compare to test for single Ei above).
  EXPECT_NE(coord(Dim::Position, 0).get(Coord::DeltaE)[0],
            coord(Dim::Position, 1).get(Coord::DeltaE)[0]);
  EXPECT_EQ(coord.unit(), units::meV);

  ASSERT_TRUE(energy.contains(Data::Value));
  const auto &data = energy(Data::Value);
  ASSERT_EQ(data.dimensions(),
            Dimensions({{Dim::Position, 3}, {Dim::DeltaE, 3}}));
  EXPECT_TRUE(equals(data.get(Data::Value), {1, 2, 3, 4, 5, 6, 7, 8, 9}));
  EXPECT_EQ(data.unit(), units::counts);

  ASSERT_TRUE(energy.contains(Coord::Position));
  ASSERT_TRUE(energy.contains(Coord::ComponentInfo));
  ASSERT_TRUE(energy.contains(Coord::Ei));
}

TEST(Dataset, convert_direct_inelastic_multi_Ei_to_QxQyQz) {
  const auto tof = makeMultiEiTofData();
  auto energy = convert(tof, Dim::Tof, Dim::DeltaE);

  Dataset qCoords;
  qCoords.insert(Coord::Qx, {Dim::Qx, 4}, {0.0, 1.0, 2.0, 3.0});
  qCoords.insert(Coord::Qy, {Dim::Qy, 2}, {0, 1});
  qCoords.insert(Coord::Qz, {Dim::Qz, 4}, {8, 9, 10, 11});
  qCoords.insert(Coord::DeltaE, {Dim::DeltaE, 3}, {9, 10, 11});

  EXPECT_NO_THROW(convert(energy, {Dim::DeltaE, Dim::Position}, qCoords));
}
