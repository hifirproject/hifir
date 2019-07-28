//@HEADER
//----------------------------------------------------------------------------
//                Copyright (C) 2019 The HILUCSI AUTHORS
//----------------------------------------------------------------------------
//@HEADER

#include "common.hpp"
// line break to avoid sorting
#include "hilucsi/ds/Array.hpp"
#include "hilucsi/ds/CompressedStorage.hpp"
#include "hilucsi/pre/matching_scaling.hpp"

#include <gtest/gtest.h>

using namespace hilucsi;

constexpr static int N = 100;

const static RandIntGen i_rand(1, N);

TEST(NZ_PT_APAT, core) {
  using crs_t   = CRS<double, int>;
  using ccs_t   = crs_t::other_type;

  const int   m(i_rand());
  const crs_t A1 = gen_rand_sparse<crs_t>(m, m);
  const ccs_t A2(A1);

  struct {
    inline constexpr int operator[](const int i) const { return i; }
    inline constexpr int inv(const int i) const { return i; }
  } dummy_p;

  const auto B =
      internal::compute_perm_leading_block(A2, A1, m, dummy_p, dummy_p, true);

  for (int i = 0; i < m; ++i) {
    std::vector<bool> mask(m, false);
    for (auto itr = A1.col_ind_cbegin(i); itr != A1.col_ind_cend(i); ++itr)
      mask[*itr] = true;
    for (auto itr = A2.row_ind_cbegin(i); itr != A2.row_ind_cend(i); ++itr)
      mask[*itr] = true;
    for (auto itr = B.row_ind_cbegin(i); itr != B.row_ind_cend(i); ++itr) {
      EXPECT_TRUE(mask[*itr]);
      mask[*itr] = false;
    }
    EXPECT_TRUE(std::all_of(mask.cbegin(), mask.cend(),
                            [](const bool i) { return !i; }));
  }
}
