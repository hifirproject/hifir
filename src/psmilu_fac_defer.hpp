//@HEADER
//----------------------------------------------------------------------------
//                Copyright (C) 2019 The PSMILU AUTHORS
//----------------------------------------------------------------------------
//@HEADER

/// \file psmilu_fac_defer.hpp
/// \brief Implementation of incomplete multilevel deferred factorization
/// \authors Qiao,

#ifndef _PSMILU_FACDEFERRED_HPP
#define _PSMILU_FACDEFERRED_HPP

#include "psmilu_Schur2.hpp"
#include "psmilu_fac.hpp"

#include "psmilu_DeferredCrout.hpp"

namespace psmilu {
namespace internal {

/*!
 * \addtogroup fac
 * @{
 */

/// \brief compress offsets to have a compact L and U
/// \tparam L_AugType augmented storage for \a L, see \ref AugCCS
/// \tparam U_AugType augmented storage for \a U, see \ref AugCRS
/// \tparam PosArray array for storing starting positions, see \ref Array
/// \param[in,out] U uncompressed \a U part
/// \param[in,out] L uncompressed \a L part
/// \param[in] U_start starting positions of the offset of \a U
/// \param[in] L_start starting positions of the offset of \a L
/// \param[in] m leading block size
/// \param[in] dfrs total number of deferrals
template <class L_AugType, class U_AugType, class PosArray>
inline void compress_tails(U_AugType &U, L_AugType &L, const PosArray &U_start,
                           const PosArray &                   L_start,
                           const typename PosArray::size_type m,
                           const typename PosArray::size_type dfrs) {
  using size_type  = typename PosArray::size_type;
  using index_type = typename L_AugType::index_type;

  if (dfrs) {
    const auto comp_index = [=](index_type &j) { j -= dfrs; };
    auto       U_first = U.col_ind().begin(), L_first = L.row_ind().begin();
    for (size_type i(0); i < m; ++i) {
      std::for_each(U_first + U_start[i], U.col_ind_end(i), comp_index);
      std::for_each(L_first + L_start[i], L.row_ind_end(i), comp_index);
    }
  }

  // reshape the secondary axis of the matrices
  L.resize_nrows(L.nrows() / 2);
  U.resize_ncols(U.ncols() / 2);

#ifndef NDEBUG
  L.check_validity();
  U.check_validity();
#endif
}

/// \brief fix the starting positions for symmetric cases while doing deferrals
/// \tparam L_AugCcsType augmented ccs type for \a L, see \ref AugCCS
/// \tparam PosArray position array type, see \ref Array
/// \param[in] L lower part of the decomposition
/// \param[in] back_step the step where we defer to
/// \param[out] L_start starting positions for offset of \a L
///
/// For symmetric factorization, we only are interested in factorizing the
/// offset of \a L, thus we are maintaining the starting positions pointing to
/// the offset locations. However, while we defer a row in \a L, the starting
/// positions no long valid, thus we need to fix it by looping through the nnz
/// entries in the deferred step and decrement the position values.
template <class L_AugCcsType, class PosArray>
inline void search_back_start_symm(const L_AugCcsType &               L,
                                   const typename PosArray::size_type back_step,
                                   const typename PosArray::size_type /* m */,
                                   PosArray &L_start) {
  using index_type  = typename L_AugCcsType::index_type;
  index_type aug_id = L.start_row_id(back_step);
  while (!L.is_nil(aug_id)) {
    const auto col_idx = L.col_idx(aug_id);
    --L_start[col_idx];
    aug_id = L.next_row_id(aug_id);
  }
}

/*!
 * @}
 */ // group fac

}  // namespace internal

/// \brief perform incomplete LU with deferrer factorization for a single level
/// \tparam IsSymm if \a true, then assume a symmetric leading block
/// \tparam CsType input compressed storage, either \ref CRS or \ref CCS
/// \tparam CroutStreamer information streamer for \ref Crout update
/// \tparam PrecsType multilevel preconditioner type, \ref Precs and \ref Prec
/// \tparam IntArray integer array type for nnz storage, see \ref Array
/// \param[in] A input for this level
/// \param[in] m0 initial leading block size
/// \param[in] N reference \b global size for determining Schur sparsity
/// \param[in] opts control parameters
/// \param[in] Crout_info information streamer, API same as \ref psmilu_info
/// \param[in,out] precs list of preconditioner, newly computed components will
///                      be pushed back to the list.
/// \param[in,out] row_sizes nnz of rows in the original input A
/// \param[in,out] col_sizes nnz of columns in the original input A
/// \return Schur complement for next level (if needed), in the same type as
///         that of the input, i.e. \a CsType
/// \ingroup fac
///
/// This is the core algorithm, which has been demonstrated in the paper, i.e.
/// the algorithm 2, \b ilu_dp_factor. There are two modifications: 1) we put
/// the preprocessing inside this routine, and 2) we embed post-processing (
/// computing Schur complement and updating preconditioner components for the
/// current level) as well. The reason(s) is(are), for 1), the preprocessing
/// requires the input type to be \ref CCS, which can only be determined inside
/// this routine (we want to keep this routine as clean as possible, we can, of
/// course, extract the preprocessing out and make this routine takes inputs
/// of both CCS and CRS of \a A.)
template <bool IsSymm, class CsType, class CroutStreamer, class PrecsType,
          class IntArray>
inline CsType iludp_factor_defer(const CsType &                   A,
                                 const typename CsType::size_type m0,
                                 const typename CsType::size_type N,
                                 const Options &                  opts,
                                 const CroutStreamer &            Crout_info,
                                 PrecsType &precs, IntArray &row_sizes,
                                 IntArray &col_sizes) {
  typedef CsType                      input_type;
  typedef typename CsType::other_type other_type;
  using cs_trait = internal::CompressedTypeTrait<input_type, other_type>;
  typedef typename cs_trait::crs_type crs_type;
  typedef typename cs_trait::ccs_type ccs_type;
  typedef AugCRS<crs_type>            aug_crs_type;
  typedef AugCCS<ccs_type>            aug_ccs_type;
  typedef typename CsType::index_type index_type;
  typedef typename CsType::size_type  size_type;
  typedef typename CsType::value_type value_type;
  typedef DenseMatrix<value_type>     dense_type;
  constexpr static bool               ONE_BASED = CsType::ONE_BASED;

  psmilu_error_if(A.nrows() != A.ncols(), "only squared systems are supported");

  psmilu_assert(m0 <= std::min(A.nrows(), A.ncols()),
                "leading size should be smaller than size of A");
  const size_type cur_level = precs.size() + 1;

  if (psmilu_verbose(INFO, opts))
    psmilu_info("\nenter level %zd (%s).\n", cur_level,
                (IsSymm ? "symmetric" : "asymmetric"));

  DefaultTimer timer;

  // build counterpart type
  const other_type A_counterpart(A);

  // now use our trait and its static methods to precisely determine the ccs
  // and crs components.
  const crs_type &A_crs = cs_trait::select_crs(A, A_counterpart);
  const ccs_type &A_ccs = cs_trait::select_ccs(A, A_counterpart);

  // handle row and column sizes
  if (cur_level == 1u) {
    row_sizes.resize(A.nrows());
    col_sizes.resize(A.ncols());
  }
#ifndef PSMILU_USE_CUR_SIZES
  if (cur_level == 1u)
#endif  // PSMILU_USE_CUR_SIZES
  {
    for (size_type i(0); i < A.nrows(); ++i) row_sizes[i] = A_crs.nnz_in_row(i);
    for (size_type i(0); i < A.ncols(); ++i) col_sizes[i] = A_ccs.nnz_in_col(i);
  }

  if (psmilu_verbose(INFO, opts))
    psmilu_info("performing preprocessing with leading block size %zd...", m0);

  // preprocessing
  timer.start();
  Array<value_type>        s, t;
  BiPermMatrix<index_type> p, q;
#ifndef PSMILU_DISABLE_PRE
  size_type m = do_preprocessing<IsSymm>(A_ccs, A_crs, m0, opts, cur_level, s,
                                         t, p, q, opts.saddle);
  // m = defer_dense_tail(A_crs, A_ccs, p, q, m);
#else
  s.resize(m0);
  psmilu_error_if(s.status() == DATA_UNDEF, "memory allocation failed");
  t.resize(m0);
  psmilu_error_if(t.status() == DATA_UNDEF, "memory allocation failed");
  p.resize(m0);
  q.resize(m0);
  std::fill(s.begin(), s.end(), value_type(1));
  std::fill(t.begin(), t.end(), value_type(1));
  p.make_eye();
  q.make_eye();
  size_type m(m0);
#endif             // PSMILU_DISABLE_PRE
  timer.finish();  // prefile pre-processing

  if (psmilu_verbose(INFO, opts)) {
    psmilu_info("preprocessing done with leading block size %zd...", m);
    psmilu_info("time: %gs", timer.time());
  }

#ifdef PSMILU_SAVE_FIRST_LEVEL_PERM_A
  if (cur_level == 1u) {
    std::mt19937                       eng(std::time(0));
    std::uniform_int_distribution<int> d(1000, 1000000);
    const std::string fname  = "Perm_A_" + std::to_string(d(eng)) + ".psmilu";
    auto              A_perm = A_crs.compute_perm(p(), q.inv(), m);
    Array<value_type> s2(m), t2(m);
    for (size_type i = 0; i < m; ++i) {
      s2[i] = s[p[i]];
      t2[i] = t[q[i]];
    }
    A_perm.scale_diag_left(s2);
    A_perm.scale_diag_right(t2);
    psmilu_info("\nsaving first level permutated matrix to file %s\n",
                fname.c_str());
    A_perm.write_native_bin(fname.c_str(), IsSymm ? m : size_type(0));
  }
#endif

  if (psmilu_verbose(INFO, opts)) psmilu_info("preparing data variables...");

  timer.start();

  // extract diagonal
  auto d = internal::extract_perm_diag(s, A_ccs, t, m, p, q);

#ifndef NDEBUG
#  define _GET_MAX_MIN_MINABS(__v, __m)                                 \
    const auto max_##__v =                                              \
        *std::max_element(__v.cbegin(), __v.cbegin() + __m),            \
               min_##__v =                                              \
                   *std::min_element(__v.cbegin(), __v.cbegin() + __m), \
               min_abs_##__v = std::abs(*std::min_element(              \
                   __v.cbegin(), __v.cbegin() + __m,                    \
                   [](const value_type a, const value_type b) {         \
                     return std::abs(a) < std::abs(b);                  \
                   }))
#  define _SHOW_MAX_MIN_MINABS(__v)                                         \
    psmilu_info("\t" #__v " max=%g, min=%g, min_abs=%g", (double)max_##__v, \
                (double)min_##__v, (double)min_abs_##__v)
  if (psmilu_verbose(INFO, opts)) {
    _GET_MAX_MIN_MINABS(d, m);
    _SHOW_MAX_MIN_MINABS(d);
    _GET_MAX_MIN_MINABS(s, m);
    _SHOW_MAX_MIN_MINABS(s);
    _GET_MAX_MIN_MINABS(t, m);
    _SHOW_MAX_MIN_MINABS(t);
  }
#endif

  // create U storage with deferred
  aug_crs_type U(m, A.ncols() * 2);
  psmilu_error_if(U.row_start().status() == DATA_UNDEF,
                  "memory allocation failed for U:row_start at level %zd.",
                  cur_level);
  do {
    const size_type rsv_fac =
        PSMILU_RESERVE_FAC <= 0 ? opts.alpha_U : PSMILU_RESERVE_FAC;
    U.reserve(A.nnz() * rsv_fac);
    psmilu_error_if(
        U.col_ind().status() == DATA_UNDEF || U.vals().status() == DATA_UNDEF,
        "memory allocation failed for U-nnz arrays at level %zd.", cur_level);
  } while (false);

  // create L storage with deferred
  aug_ccs_type L(A.nrows() * 2, m);
  psmilu_error_if(L.col_start().status() == DATA_UNDEF,
                  "memory allocation failed for L:col_start at level %zd.",
                  cur_level);
  do {
    const size_type rsv_fac =
        PSMILU_RESERVE_FAC <= 0 ? opts.alpha_L : PSMILU_RESERVE_FAC;
    L.reserve(A.nnz() * rsv_fac);
    psmilu_error_if(
        L.row_ind().status() == DATA_UNDEF || L.vals().status() == DATA_UNDEF,
        "memory allocation failed for L-nnz arrays at level %zd.", cur_level);
  } while (false);

  // create l and ut buffer
  SparseVector<value_type, index_type, ONE_BASED> l(A.nrows() * 2),
      ut(A.ncols() * 2);

  // create buffer for L and U start
  Array<index_type> L_start(m), U_start(m);
  psmilu_error_if(
      L_start.status() == DATA_UNDEF || U_start.status() == DATA_UNDEF,
      "memory allocation failed for L_start and/or U_start at level %zd.",
      cur_level);

  // create storage for kappa's
  Array<value_type> kappa_l(m), kappa_ut(m);
  psmilu_error_if(
      kappa_l.status() == DATA_UNDEF || kappa_ut.status() == DATA_UNDEF,
      "memory allocation failed for kappa_l and/or kappa_ut at level %zd.",
      cur_level);

#ifdef PSMILU_ENABLE_NORM_STAT
  Array<value_type> ut_norm_ratios(m), l_norm_ratios(m);
#endif  // PSMILU_ENABLE_NORM_STAT

  U.begin_assemble_rows();
  L.begin_assemble_cols();

  // localize parameters
  double tau_d, tau_kappa, tau_L, tau_U;
  int    alpha_L, alpha_U;
  std::tie(tau_d, tau_kappa, tau_L, tau_U, alpha_L, alpha_U) =
      determine_fac_pars(opts, cur_level);
  const auto kappa_sq = tau_kappa * tau_kappa;

  // Removing bounding the large diagonal values
  const auto is_bad_diag = [=](const value_type a) -> bool {
    return std::abs(1. / a) > tau_d;  // || std::abs(a) > tau_d;
  };

  const size_type m2(m), n(A.nrows());

  // deferred permutations
  Array<index_type> P(n * 2), Q(n * 2);
  psmilu_error_if(P.status() == DATA_UNDEF || Q.status() == DATA_UNDEF,
                  "memory allocation failed for P and/or Q at level %zd",
                  cur_level);
  std::copy_n(p().cbegin(), n, P.begin());
  std::copy_n(q().cbegin(), n, Q.begin());
  auto &P_inv = p.inv(), &Q_inv = q.inv();

  // 0 for defer due to diagonal, 1 for defer due to bad inverse conditioning
  index_type info_counter[] = {0, 0, 0, 0, 0, 0, 0};

  if (psmilu_verbose(INFO, opts)) psmilu_info("start Crout update...");
  DeferredCrout step;
  for (; step < m; ++step) {
    // first check diagonal
    bool            pvt         = is_bad_diag(d[step.deferred_step()]);
    const size_type m_prev      = m;
    const size_type defers_prev = step.defers();
    info_counter[0] += pvt;

    Crout_info(" Crout step %zd, leading block size %zd", step, m);

    // compute kappa for u wrp deferred index
    update_kappa_ut(step, U, kappa_ut, step.deferred_step());
    // then compute kappa for l
    update_kappa_l<IsSymm>(step, L, kappa_ut, kappa_l, step.deferred_step());

#ifdef PSMILU_DEFERREDFAC_VERBOSE_STAT
    info_counter[2] += (std::abs(kappa_ut[step]) > tau_kappa &&
                        std::abs(kappa_l[step]) > tau_kappa);
#endif  // PSMILU_DEFERREDFAC_VERBOSE_STAT

    // check condition number if diagonal doesn't satisfy
    if (!pvt) {
      pvt = std::abs(kappa_ut[step]) > tau_kappa ||
            std::abs(kappa_l[step]) > tau_kappa;
      info_counter[1] += pvt;
    }

    if (pvt) {
      while (m > step) {
        --m;
        const auto tail_pos = n + step.defers();
        U.defer_col(step.deferred_step(), tail_pos);
        L.defer_row(step.deferred_step(), tail_pos);
        if (IsSymm) internal::search_back_start_symm(L, tail_pos, m2, L_start);
        P[tail_pos]        = p[step.deferred_step()];
        Q[tail_pos]        = q[step.deferred_step()];
        P_inv[P[tail_pos]] = tail_pos;
        Q_inv[Q[tail_pos]] = tail_pos;
        // mark as empty entries
        P[step.deferred_step()] = Q[step.deferred_step()] = -1;

        step.increment_defer_counter();  // increment defers here
        // handle the last step
        if (step.deferred_step() >= m2) {
          m = step;
          break;
        }
        pvt = is_bad_diag(d[step.deferred_step()]);
        if (pvt) {
          ++info_counter[0];
#ifdef PSMILU_DEFERREDFAC_VERBOSE_STAT
          // compute kappa for u wrp deferred index
          update_kappa_ut(step, U, kappa_ut, step.deferred_step());
          // then compute kappa for l
          update_kappa_l<IsSymm>(step, L, kappa_ut, kappa_l,
                                 step.deferred_step());
          info_counter[2] += (std::abs(kappa_ut[step]) > tau_kappa &&
                              std::abs(kappa_l[step]) > tau_kappa);
#endif  // PSMILU_DEFERREDFAC_VERBOSE_STAT
          continue;
        }

        // compute kappa for u wrp deferred index
        update_kappa_ut(step, U, kappa_ut, step.deferred_step());
        // then compute kappa for l
        update_kappa_l<IsSymm>(step, L, kappa_ut, kappa_l,
                               step.deferred_step());
        pvt = std::abs(kappa_ut[step]) > tau_kappa ||
              std::abs(kappa_l[step]) > tau_kappa;
        if (pvt) {
          ++info_counter[1];
#ifdef PSMILU_DEFERREDFAC_VERBOSE_STAT
          info_counter[2] += (std::abs(kappa_ut[step]) > tau_kappa &&
                              std::abs(kappa_l[step]) > tau_kappa);
#endif  // PSMILU_DEFERREDFAC_VERBOSE_STAT
          continue;
        }
        break;
      }                      // while
      if (m == step) break;  // break for
    }

    //----------------
    // inverse thres
    //----------------

    const auto k_ut = kappa_ut[step], k_l = kappa_l[step];

    // check pivoting
    psmilu_assert(!(std::abs(k_ut) > tau_kappa || std::abs(k_l) > tau_kappa),
                  "should not happen!");

    Crout_info("  kappa_ut=%g, kappa_l=%g", (double)std::abs(k_ut),
               (double)std::abs(k_l));

    Crout_info(
        "  previous/current leading block sizes %zd/%zd, local/total "
        "defers=%zd/%zd",
        m_prev, m, step.defers() - defers_prev, step.defers());

#ifndef PSMILU_DISABLE_DYN_PVT_THRES
    const auto kappa_sq = std::abs(k_ut * k_l);
#endif  // PSMILU_DISABLE_DYN_PVT_THRES

    //------------------------
    // update start positions
    //------------------------

    Crout_info("  updating L_start/U_start and performing Crout update");

    // compress diagonal
    step.compress_array(d);

    // compress permutation vectors
    step.compress_array(p);
    step.compress_array(q);

    // update U
    step.update_U_start_and_compress_U(U, U_start);
    // then update L
    step.update_L_start_and_compress_L<IsSymm>(L, m2, L_start);

    //----------------------
    // compute Crout updates
    //----------------------

    // compute Uk'
    step.compute_ut(s, A_crs, t, p[step], Q_inv, L, d, U, U_start, ut);
    // compute Lk
    step.compute_l<IsSymm>(s, A_ccs, t, P_inv, q[step], m2, L, L_start, d, U,
                           l);

    // update diagonal entries for u first
#ifndef NDEBUG
    const bool u_is_nonsingular =
#else
    (void)
#endif
        step.scale_inv_diag(d, ut);
    psmilu_assert(!u_is_nonsingular, "u is singular at level %zd step %zd",
                  cur_level, step);

    // update diagonals b4 dropping
    step.update_B_diag<IsSymm>(l, ut, m2, d);

#ifndef NDEBUG
    const bool l_is_nonsingular =
#else
    (void)
#endif
        step.scale_inv_diag(d, l);
    psmilu_assert(!l_is_nonsingular, "l is singular at level %zd step %zd",
                  cur_level, step);

    //---------------
    // drop and sort
    //---------------

    const size_type ori_ut_size = ut.size(), ori_l_size = l.size();

    // apply drop for U
    apply_num_dropping(tau_U, kappa_sq, ut);
#ifdef PSMILU_ENABLE_NORM_STAT
    ut_norm_ratios[step] = ut.norm1();
#endif  // PSMILU_ENABLE_NORM_STAT
#ifndef PSMILU_DISABLE_SPACE_DROP
    const size_type n_ut = ut.size();
    apply_space_dropping(row_sizes[p[step]], alpha_U, ut);
    info_counter[3] += n_ut - ut.size();
#endif  // PSMILU_DISABLE_SPACE_DROP
    info_counter[5] += ori_ut_size - ut.size();
    ut.sort_indices();
#ifdef PSMILU_ENABLE_NORM_STAT
    ut_norm_ratios[step] = ut.norm1() / ut_norm_ratios[step];
#endif  // PSMILU_ENABLE_NORM_STAT

    // push back rows to U
    U.push_back_row(step, ut.inds().cbegin(), ut.inds().cbegin() + ut.size(),
                    ut.vals());

    Crout_info("  ut sizes before/after dropping %zd/%zd, drops=%zd",
               ori_ut_size, ut.size(), ori_ut_size - ut.size());

    // apply numerical dropping on L
    apply_num_dropping(tau_L, kappa_sq, l);

#ifdef PSMILU_ENABLE_NORM_STAT
    l_norm_ratios[step] = l.norm1();
#endif  // PSMILU_ENABLE_NORM_STAT

    if (IsSymm) {
#ifndef PSMILU_DISABLE_SPACE_DROP
      // for symmetric cases, we need first find the leading block size
      auto info = find_sorted(ut.inds().cbegin(),
                              ut.inds().cbegin() + ut.size(), m2 + ONE_BASED);
      apply_space_dropping(col_sizes[q[step]], alpha_L, l,
                           info.second - ut.inds().cbegin());

#  ifndef NDEBUG
      if (info.second != ut.inds().cbegin() &&
          info.second != ut.inds().cbegin() + ut.size() && l.size())
        psmilu_error_if(*(info.second - 1) >= *l.inds().cbegin() ||
                            *(info.second - 1) - ONE_BASED >= m2,
                        "l contains symm part (%zd,%zd,%zd)",
                        (size_type)(*(info.second - 1)),
                        (size_type)*l.inds().cbegin(), m2);
#  endif
      auto u_last = info.second;
#else   // !PSMILU_DISABLE_SPACE_DROP
      auto u_last = ut.inds().cbegin() + ut.size();
#endif  // PSMILU_DISABLE_SPACE_DROP

      l.sort_indices();
      Crout_info(
          "  l sizes (asymm parts) before/after dropping %zd/%zd, drops=%zd",
          ori_l_size, l.size(), ori_l_size - l.size());

      // push back symmetric entries and offsets
      L.push_back_col(step, ut.inds().cbegin(), u_last, ut.vals(),
                      l.inds().cbegin(), l.inds().cbegin() + l.size(),
                      l.vals());
    } else {
#ifndef PSMILU_DISABLE_SPACE_DROP
      const size_type n_l = l.size();
      apply_space_dropping(col_sizes[q[step]], alpha_L, l);
      info_counter[4] += n_l - l.size();
#endif  // PSMILU_DISABLE_SPACE_DROP
      info_counter[6] += ori_l_size - l.size();
      l.sort_indices();
      Crout_info("  l sizes before/after dropping %zd/%zd, drops=%zd",
                 ori_l_size, l.size(), ori_l_size - l.size());
      L.push_back_col(step, l.inds().cbegin(), l.inds().cbegin() + l.size(),
                      l.vals());
    }
#ifdef PSMILU_ENABLE_NORM_STAT
    l_norm_ratios[step] = l.norm1() / l_norm_ratios[step];
#endif  // PSMILU_ENABLE_NORM_STAT
    Crout_info(" Crout step %zd done!", step);
  }

  // compress permutation vectors
  for (; step < n; ++step) {
    step.assign_gap_array(P, p);
    step.assign_gap_array(Q, q);
    // NOTE important to compress the start/end in augmented DS
    // the tails were updated while deferring and the leading
    // block is compressed while updating L and U starts
    // thus, we only need to compress the offsets
    step.compress_array(U.col_start());
    step.compress_array(U.col_end());
    step.compress_array(L.row_start());
    step.compress_array(L.row_end());
  }
  // rebuild the inverse mappings
  p.build_inv();
  q.build_inv();

  U.end_assemble_rows();
  L.end_assemble_cols();

  // finalize start positions
  if (m) {
    U_start[m - 1] = U.row_start()[m - 1];
    L_start[m - 1] = L.col_start()[m - 1];
  }
  // compress tails
  internal::compress_tails(U, L, U_start, L_start, m, step.defers());

  timer.finish();  // profile Crout update

  // now we are done
  if (psmilu_verbose(INFO, opts)) {
    psmilu_info(
        "finish Crout update...\n"
        "\ttotal deferrals=%zd\n"
        "\tleading block size in=%zd\n"
        "\tleading block size out=%zd\n"
        "\tdiff=%zd\n"
        "\tdiag deferrals=%zd\n"
        "\tinv-norm deferrals=%zd\n"
        "\tdrop ut=%zd\n"
        "\tspace drop ut=%zd\n"
        "\tdrop l=%zd\n"
        "\tspace drop l=%zd\n"
        "\tmin |kappa_u|=%g\n"
        "\tmax |kappa_u|=%g\n"
        "\tmin |kappa_l|=%g\n"
        "\tmax |kappa_l|=%g"
#ifdef PSMILU_DEFERREDFAC_VERBOSE_STAT
        "\n\tboth-inv-bad deferrals=%zd"
#endif  // PSMILU_DEFERREDFAC_VERBOSE_STAT
        ,
        step.defers(), m2, m, m2 - m, (size_type)info_counter[0],
        (size_type)info_counter[1], (size_type)info_counter[5],
        (size_type)info_counter[3], (size_type)info_counter[6],
        (size_type)info_counter[4],
        (double)std::abs(
            *std::min_element(kappa_ut.cbegin(), kappa_ut.cbegin() + m,
                              [](const value_type l, const value_type r) {
                                return std::abs(l) < std::abs(r);
                              })),
        (double)std::abs(
            *std::max_element(kappa_ut.cbegin(), kappa_ut.cbegin() + m,
                              [](const value_type l, const value_type r) {
                                return std::abs(l) < std::abs(r);
                              })),
        (double)std::abs(
            *std::min_element(kappa_l.cbegin(), kappa_l.cbegin() + m,
                              [](const value_type l, const value_type r) {
                                return std::abs(l) < std::abs(r);
                              })),
        (double)std::abs(
            *std::max_element(kappa_l.cbegin(), kappa_l.cbegin() + m,
                              [](const value_type l, const value_type r) {
                                return std::abs(l) < std::abs(r);
                              }))
#ifdef PSMILU_DEFERREDFAC_VERBOSE_STAT
            ,
        (size_type)info_counter[2]
#endif  // PSMILU_DEFERREDFAC_VERBOSE_STAT
    );
#ifndef NDEBUG
    _GET_MAX_MIN_MINABS(d, m);
    _SHOW_MAX_MIN_MINABS(d);
#endif
#ifdef PSMILU_ENABLE_NORM_STAT
    psmilu_info(
        "analyzing the behaviors of l and ut after/before space dropping...");
    std::sort(ut_norm_ratios.begin(), ut_norm_ratios.begin() + m);
    std::sort(l_norm_ratios.begin(), l_norm_ratios.begin() + m);
    const size_type i25 = 25.0 * m / 100, i50 = 50.0 * m / 100,
                    i75 = 75.0 * m / 100;
    psmilu_info(
        "\tut 1-norm ratio:\n"
        "\t\tmin=%g\n"
        "\t\t25%%=%g\n"
        "\t\tmedian=%g\n"
        "\t\t75%%=%g\n"
        "\t\tmax=%g\n"
        "\t\t<=99%%=%zd\n"
        "\tl 1-norm ratio:\n"
        "\t\tmin=%g\n"
        "\t\t25%%=%g\n"
        "\t\tmedian=%g\n"
        "\t\t75%%=%g\n"
        "\t\tmax=%g\n"
        "\t\t<=99%%=%zd",
        (double)ut_norm_ratios.front(), (double)ut_norm_ratios[i25],
        (double)ut_norm_ratios[i50], (double)ut_norm_ratios[i75],
        (double)ut_norm_ratios[m - 1],
        size_type(std::upper_bound(ut_norm_ratios.cbegin(),
                                   ut_norm_ratios.cend(), 0.99) -
                  ut_norm_ratios.cbegin()),
        (double)l_norm_ratios.front(), (double)l_norm_ratios[i25],
        (double)l_norm_ratios[i50], (double)l_norm_ratios[i75],
        (double)l_norm_ratios[m - 1],
        size_type(std::upper_bound(l_norm_ratios.cbegin(), l_norm_ratios.cend(),
                                   0.99) -
                  l_norm_ratios.cbegin()));
#endif  // PSMILU_ENABLE_NORM_STAT
    psmilu_info("time: %gs", timer.time());
  }

  if (psmilu_verbose(INFO, opts))
    psmilu_info("computing Schur complement and assembling Prec...");

  timer.start();

  // drop
  auto     E   = crs_type(internal::extract_E(s, A_crs, t, m, p, q));
  auto     F   = internal::extract_F(s, A_ccs, t, m, p, q, ut.vals());
  auto     L_E = L.template split_crs<true>(m, L_start);
  crs_type U_F;
  do {
    auto            U_F2 = U.template split_ccs<true>(m, U_start);
    const size_type nnz1 = L_E.nnz(), nnz2 = U_F2.nnz();
#ifndef PSMILU_NO_DROP_LE_UF
    const double a_L = opts.alpha_L, a_U = opts.alpha_U;
    if (psmilu_verbose(INFO, opts))
      psmilu_info("applying dropping on L_E and U_F with alpha_{L,U}=%g,%g...",
                  a_L, a_U);
    if (m < n) {
#  ifdef PSMILU_USE_CUR_SIZES
      drop_L_E(E.row_start(), a_L, L_E, l.vals(), l.inds());
      drop_U_F(F.col_start(), a_U, U_F2, ut.vals(), ut.inds());
#  else
      if (cur_level == 1u) {
        drop_L_E(E.row_start(), a_L, L_E, l.vals(), l.inds());
        drop_U_F(F.col_start(), a_U, U_F2, ut.vals(), ut.inds());
      } else {
        // use P and Q as buffers
        P[0] = Q[0] = 0;
        for (size_type i(m); i < n; ++i) {
          P[i - m + 1] = P[i - m] + row_sizes[p[i]];
          Q[i - m + 1] = Q[i - m] + col_sizes[q[i]];
        }
        drop_L_E(P, a_L, L_E, l.vals(), l.inds());
        drop_U_F(Q, a_U, U_F2, ut.vals(), ut.inds());
      }
#  endif
    }
#endif  // PSMILU_NO_DROP_LE_UF
    U_F = crs_type(U_F2);
    if (psmilu_verbose(INFO, opts))
      psmilu_info("nnz(L_E)=%zd/%zd, nnz(U_F)=%zd/%zd...", nnz1, L_E.nnz(),
                  nnz2, U_F.nnz());
  } while (false);

  // compute the nnz(A)-nnz(B) for first level
  size_type AmB_nnz(0);
  for (size_type i(m); i < n; ++i) AmB_nnz += row_sizes[p[i]] + col_sizes[q[i]];

  // compute S version of Schur complement
  bool             use_h_ver = false;
  const input_type S =
      input_type(compute_Schur_simple(s, A_crs, t, p, q, m, L_E, d, U_F, l));

  // compute L_B and U_B
  auto L_B = L.template split<false>(m, L_start);
  auto U_B = U.template split_ccs<false>(m, U_start);

  const size_type dense_thres1 = static_cast<size_type>(
                      std::max(opts.alpha_L, opts.alpha_U) * AmB_nnz),
                  dense_thres2 = std::max(static_cast<size_type>(std::ceil(
                                              opts.c_d * std::cbrt(N))),
                                          size_type(1000));

  if (psmilu_verbose(INFO, opts))
    psmilu_info(
        "nnz(S_C)=%zd, nnz(L/L_B)=%zd/%zd, nnz(U/U_B)=%zd/%zd\n"
        "dense_thres{1,2}=%zd/%zd...",
        S.nnz(), L.nnz(), L_B.nnz(), U.nnz(), U_B.nnz(), dense_thres1,
        dense_thres2);

  // test H version
  const size_type nm = n - m;
  dense_type      S_D;
  psmilu_assert(S_D.empty(), "fatal!");

  if ((size_type)std::ceil(nm * nm * opts.rho) <= dense_thres1 ||
      nm <= dense_thres2 || !m) {
    S_D = dense_type::from_sparse(S);
    if (psmilu_verbose(INFO, opts))
      psmilu_info("converted Schur complement (%s) to dense for last level...",
                  (use_h_ver ? "H" : "S"));
  }

#ifndef PSMILU_USE_CUR_SIZES
  if (S_D.empty()) {
    // update the row and column sizes
    // Important! Update this information before destroying p and q in the
    // following emplace_back call
    // NOTE use P and Q as buffers
    for (size_type i(m); i < n; ++i) {
      P[i] = row_sizes[p[i]];
      Q[i] = col_sizes[q[i]];
    }
    for (size_type i(m); i < n; ++i) {
      row_sizes[i - m] = P[i];
      col_sizes[i - m] = Q[i];
    }
  }
#endif  // PSMILU_USE_CUR_SIZES

  precs.emplace_back(m, n, std::move(L_B), std::move(d), std::move(U_B),
                     std::move(E), std::move(F), std::move(s), std::move(t),
                     std::move(p()), std::move(q.inv()));

  // if dense is not empty, then push it back
  if (!S_D.empty()) {
    auto &last_level = precs.back().dense_solver;
    last_level.set_matrix(std::move(S_D));
    last_level.factorize();
    if (psmilu_verbose(INFO, opts))
      psmilu_info("successfully factorized the dense component...");
  }

  timer.finish();  // profile post-processing

  if (psmilu_verbose(INFO, opts)) psmilu_info("time: %gs", timer.time());

  if (psmilu_verbose(INFO, opts)) psmilu_info("\nfinish level %zd.", cur_level);

  return S;
}

}  // namespace psmilu

#endif  // _PSMILU_FACDEFERRED_HPP
