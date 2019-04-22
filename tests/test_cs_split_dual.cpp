//@HEADER
//----------------------------------------------------------------------------
//                Copyright (C) 2019 The PSMILU AUTHORS
//----------------------------------------------------------------------------
//@HEADER

#include "common.hpp"
// line break to avoid sorting
#include "psmilu_Array.hpp"
#include "psmilu_CompressedStorage.hpp"

#include <gtest/gtest.h>

using namespace psmilu;

static const RandIntGen i_rand(1, 100);

TEST(crs, c) {
  using mat_t          = CRS<double, int>;
  const auto        A  = gen_rand_sparse<mat_t>(100, 100);
  const auto        AD = convert2dense(A);
  mat_t::other_type F, S;
  const int         m = i_rand();
  std::tie(F, S)      = A.split_ccs(m);
  const auto FD       = convert2dense(F);
  const auto SD       = convert2dense(S);
  for (int i = 0; i < 100; ++i) {
    int j = 0;
    for (; j < m; ++j) EXPECT_EQ(AD[i][j], FD[i][j]);
    for (; j < 100; ++j) EXPECT_EQ(AD[i][j], SD[i][j - m]);
  }
}

TEST(crs, fortran) {
  using mat_t          = CRS<double, int, true>;
  const auto        A  = gen_rand_sparse<mat_t>(100, 100);
  const auto        AD = convert2dense(A);
  mat_t::other_type F, S;
  const int         m = i_rand();
  std::tie(F, S)      = A.split_ccs(m);
  const auto FD       = convert2dense(F);
  const auto SD       = convert2dense(S);
  for (int i = 0; i < 100; ++i) {
    int j = 0;
    for (; j < m; ++j) EXPECT_EQ(AD[i][j], FD[i][j]);
    for (; j < 100; ++j) EXPECT_EQ(AD[i][j], SD[i][j - m]);
  }
}

TEST(ccs, c) {
  using mat_t          = CCS<double, int>;
  const auto        A  = gen_rand_sparse<mat_t>(100, 100);
  const auto        AD = convert2dense(A);
  mat_t::other_type F, S;
  const int         m = i_rand();
  std::tie(F, S)      = A.split_crs(m);
  const auto FD       = convert2dense(F);
  const auto SD       = convert2dense(S);
  for (int j = 0; j < 100; ++j) {
    int i = 0;
    for (; i < m; ++i) EXPECT_EQ(AD[i][j], FD[i][j]);
    for (; i < 100; ++i) EXPECT_EQ(AD[i][j], SD[i - m][j]);
  }
}

TEST(ccs, fortran) {
  using mat_t          = CCS<double, int, true>;
  const auto        A  = gen_rand_sparse<mat_t>(100, 100);
  const auto        AD = convert2dense(A);
  mat_t::other_type F, S;
  const int         m = i_rand();
  std::tie(F, S)      = A.split_crs(m);
  const auto FD       = convert2dense(F);
  const auto SD       = convert2dense(S);
  for (int j = 0; j < 100; ++j) {
    int i = 0;
    for (; i < m; ++i) EXPECT_EQ(AD[i][j], FD[i][j]);
    for (; i < 100; ++i) EXPECT_EQ(AD[i][j], SD[i - m][j]);
  }
}