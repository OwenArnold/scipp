# @file
# SPDX-License-Identifier: GPL-3.0-or-later
# @author Neil Vaytet
# Copyright &copy; 2018 ISIS Rutherford Appleton Laboratory, NScD Oak Ridge
# National Laboratory, and European Spallation Source ERIC.
import unittest

from scippy import Dataset, Coord, Dim, Data, plot, units
import numpy as np
import io
from contextlib import redirect_stdout

# TODO: For now we are just checking that the plot does not throw any errors.
# In the future it would be nice to check the output by either comparing
# checksums or by using tools like squish.


def do_plot(d, axes=None, waterfall=None):
    with io.StringIO() as buf, redirect_stdout(buf):
        plot(d, axes=axes, waterfall=waterfall)
    return


class TestPlotting(unittest.TestCase):

    def test_plot_1d(self):
        d1 = Dataset()
        N = 100
        d1[Coord.Tof] = ([Dim.Tof], np.arange(N).astype(np.float64))
        d1[Data.Value, "Counts"] = (
            [Dim.Tof], np.arange(N).astype(np.float64)**2)
        do_plot(d1)
        d1[Data.Variance, "Counts"] = (
            [Dim.Tof], 0.1 * np.arange(N).astype(np.float64))
        do_plot(d1)

    def test_plot_1d_bin_edges(self):
        d1 = Dataset()
        N = 100
        d1[Coord.Tof] = ([Dim.Tof], np.arange(N + 1).astype(np.float64))
        d1[Data.Value, "Counts"] = (
            [Dim.Tof], np.arange(N).astype(np.float64)**2)
        do_plot(d1)

    def test_plot_1d_two_values(self):
        d1 = Dataset()
        N = 100
        d1[Coord.Tof] = ([Dim.Tof], np.arange(N).astype(np.float64))
        d1[Data.Value, "Counts"] = (
            [Dim.Tof], np.arange(N).astype(np.float64)**2)
        d1[Data.Variance, "Counts"] = (
            [Dim.Tof], 0.1 * np.arange(N).astype(np.float64))
        d1[Data.Value, "Sample"] = (
            [Dim.Tof], np.sin(np.arange(N).astype(np.float64)))
        d1[Data.Variance, "Sample"] = (
            [Dim.Tof], 0.1 * np.arange(N).astype(np.float64))
        do_plot(d1)

    def test_plot_1d_list_of_datasets(self):
        d1 = Dataset()
        N = 100
        d1[Coord.Tof] = ([Dim.Tof], np.arange(N).astype(np.float64))
        d1[Data.Value, "Counts"] = (
            [Dim.Tof], np.arange(N).astype(np.float64)**2)
        d1[Data.Variance, "Counts"] = (
            [Dim.Tof], 0.1 * np.arange(N).astype(np.float64))
        d2 = Dataset()
        d2[Coord.Tof] = ([Dim.Tof], np.arange(N).astype(np.float64))
        d2[Data.Value, "Sample"] = (
            [Dim.Tof], np.cos(np.arange(N).astype(np.float64)))
        d2[Data.Variance, "Sample"] = (
            [Dim.Tof], 0.1 * np.arange(N).astype(np.float64))
        do_plot([d1, d2])

    def test_plot_2d_image(self):
        d3 = Dataset()
        n1 = 100
        n2 = 200
        d3[Coord.Tof] = ([Dim.Tof], np.arange(n1).astype(np.float64))
        d3[Coord.Position] = (
            [Dim.Position], np.arange(n2).astype(np.float64))
        d3[Data.Value, "sample"] = \
            ([Dim.Position, Dim.Tof],
             np.arange(n1 * n2).reshape(n2, n1).astype(np.float64))
        d3[Data.Value, "sample"].unit = units.counts
        do_plot(d3)
        d3[Coord.SpectrumNumber] = (
            [Dim.Position], np.arange(n2).astype(np.float64))
        do_plot(d3, axes=[Coord.SpectrumNumber, Coord.Tof])

    def test_plot_waterfall(self):
        d4 = Dataset()
        n1 = 100
        n2 = 30
        d4[Coord.Tof] = ([Dim.Tof], np.arange(n1).astype(np.float64))
        d4[Coord.Position] = (
            [Dim.Position], np.arange(n2).astype(np.float64))
        d4[Data.Value, "counts"] = \
            ([Dim.Position, Dim.Tof], np.arange(
             n1 * n2).reshape(n2, n1).astype(np.float64))
        do_plot(d4, waterfall=Dim.Position)

    def test_plot_sliceviewer(self):
        d5 = Dataset()
        n1 = 10
        n2 = 20
        n3 = 30
        d5[Coord.X] = ([Dim.X], np.arange(n1).astype(np.float64))
        d5[Coord.Y] = ([Dim.Y], np.arange(n2).astype(np.float64))
        d5[Coord.Z] = ([Dim.Z], np.arange(n3).astype(np.float64))
        d5[Data.Value, "background"] = \
            ([Dim.Z, Dim.Y, Dim.X], np.arange(
             n1 * n2 * n3).reshape(n3, n2, n1).astype(np.float64))
        do_plot(d5)

    def test_plot_sliceviewer_with_two_sliders(self):
        d5 = Dataset()
        n1 = 10
        n2 = 11
        n3 = 12
        n4 = 13
        d5[Coord.X] = ([Dim.X], np.arange(n1).astype(np.float64))
        d5[Coord.Y] = ([Dim.Y], np.arange(n2).astype(np.float64))
        d5[Coord.Z] = ([Dim.Z], np.arange(n3).astype(np.float64))
        d5[Coord.Energy] = (
            [Dim.Energy], np.arange(n4).astype(np.float64))
        d5[Data.Value, "sample"] = \
            ([Dim.Energy, Dim.Z, Dim.Y, Dim.X], np.arange(
             n1 * n2 * n3 * n4).reshape(n4, n3, n2, n1).astype(np.float64))
        do_plot(d5)

    def test_plot_sliceviewer_with_axes(self):
        d5 = Dataset()
        n1 = 10
        n2 = 20
        n3 = 30
        d5[Coord.X] = ([Dim.X], np.arange(n1).astype(np.float64))
        d5[Coord.Y] = ([Dim.Y], np.arange(n2).astype(np.float64))
        d5[Coord.Z] = ([Dim.Z], np.arange(n3).astype(np.float64))
        d5[Data.Value, "background"] = \
            ([Dim.Z, Dim.Y, Dim.X], np.arange(
             n1 * n2 * n3).reshape(n3, n2, n1).astype(np.float64))
        do_plot(d5, axes=[Coord.X, Coord.Z, Coord.Y])


if __name__ == '__main__':
    unittest.main()
