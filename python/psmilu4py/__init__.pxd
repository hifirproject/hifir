# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
#                Copyright (C) 2019 The PSMILU AUTHORS
# ----------------------------------------------------------------------------

# Authors:
#   Qiao,

# This is the core interface for psmilu4py

from libcpp cimport bool
from libcpp.string cimport string as std_string
from libc.stddef cimport size_t


cdef extern from 'psmilu4py.hpp' namespace 'psmilu' nogil:
    # two necessary utilities
    std_string version()
    bool warn_flag(const int)

    # wrap options, we don't care about the attibutes
    ctypedef struct Options:
        pass
    Options get_default_options()
    std_string opt_repr(const Options &opts) except +
    # also, manipulation method
    bool set_option_attr[T](const std_string &attr, const T v, Options &opts)

    cdef cppclass PyBuilder:
        PyBuilder()
        bool empty()
        size_t levels()
        size_t nnz()

        # computing routine
        void compute(const size_t nrows, const size_t ncols,
                     const int *indptr, const int *indices,
                     const double *vals, const size_t m0,
                     const Options &opts, const bool check,
                     const bool is_crs) except +

        # solving routine
        void solve(const size_t n, const double *b, double *x) except +