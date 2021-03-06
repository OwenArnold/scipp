# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2020 Scipp contributors (https://github.com/scipp)
# @file
# @author Simon Heybrock
import scipp as sc
from scipp import Dim


def test_setitem_required_for_inplace_ops():
    # Test that all required __setitem__ overloads for in-place operations
    # are available.

    var = sc.Variable(dims=[Dim.X, Dim.Y], shape=[2, 3])
    var *= 1.5  # not setitem, just assigns python variable
    var[Dim.X, 1:] *= 1.5  # Variable.__setitem__
    var[Dim.X, 1:][Dim.Y, 1:] *= 1.5  # VariableProxy.__setitem__

    a = sc.DataArray(data=var)
    a *= 1.5  # not setitem, just assigns python variable
    a[Dim.X, 1:] *= 1.5  # DataArray.__setitem__
    a[Dim.X, 1:][Dim.Y, 1:] *= 1.5  # DataProxy.__setitem__

    d = sc.Dataset(data={'a': var})
    d *= 1.5  # not setitem, just assigns python variable
    d['a'] *= 1.5  # Dataset.__setitem__(string)
    d[Dim.X, 1:] *= 1.5  # Dataset.__setitem__(slice)
    d[Dim.X, 1:]['a'] *= 1.5  # DatasetProxy.__setitem__(string)
    d['a'][Dim.X, 1:] *= 1.5  # DatasetProxy.__setitem__(slice)
    d[Dim.X, 1:][Dim.Y, 1:] *= 1.5  # DatasetProxy.__setitem__(slice)
