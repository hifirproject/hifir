#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <tuple>

#include "HILUCSI.hpp"

using namespace hilucsi;
using std::string;

using prec_t   = DefaultHILUCSI;  // use default C CRS with double and int
using crs_t    = prec_t::crs_type;
using array_t  = prec_t::array_type;
using solver_t = ksp::FGMRES<prec_t>;

const static char *help =
    "\n"
    "usage:\n"
    "\n"
    " ./demo case [options] [flags]\n"
    "\n"
    "where the \"case\" is an HILUCSI benchmark input directory that contains\n"
    "a matrix file and an ASCII rhs file. The matrix file is in HILUCSI\n"
    "binary format and has name of \"A.psmilu\", while the rhs file is a "
    "list\n"
    "of rhs entries that are separated by native C++ separator, i.e. space or\n"
    "new line (the name is \"b.txt\".)\n"
    "\n"
    "options:\n"
    "\n"
    " -t|--tau tau\n"
    "\tdrop tolerance (1e-4)\n"
    " -k|--kappa kappa\n"
    "\tinverse norm threshold (3)\n"
    " -a|--alpha alpha\n"
    "\tlocal space control parameter (10)\n"
    " -v|--verbose level\n"
    "\tverbose information printing level (0)\n"
    " -r|--reorder method\n"
    "\treordering methods:\n"
    "\t\t0: off\n"
    "\t\t1: auto (amd), default\n"
    "\t\t2: amd\n"
    "\t\t3: rcm (requires BGL)\n"
    "\t\t4: king (requires BGL)\n"
    "\t\t5: sloan (requires BGL)\n"
    " -m|--matching method\n"
    "\tmatching methods:\n"
    "\t\t0: try to use MC64 if it is available, default\n"
    "\t\t1: MUMPS\n"
    "\t\t2: MC64 (requires F77 MC64)\n"
    " -I|-pre-scale\n"
    "\ta priori scaling before calling matching/scaling\n"
    "\t\t0: off (default)\n"
    "\t\t1: local extreme scale\n"
    "\t\t2: iterative scaling based on inf-norm (experimental)\n"
    " -S|--symm-pre-lvls\n"
    "\tlevels to apply symmetric preprocessing, nonpositive number indicates\n"
    "\tapply symmetric preprocessing for all levels (1)\n"
    " -T|--rtol\n"
    "\trelative tolerance for FGMRES (1e-6) solver\n"
    " -R|--restart\n"
    "\trestart for FGMRES (30)\n"
    "\n"
    "flags:\n"
    "\n"
    " -h|--help\n"
    "\tshow help information and exit\n"
    " -V|--version\n"
    "\tshow version and exit\n"
    " -n|--no-saddle\n"
    "\tdisable static deferals for saddle point problems\n"
    " -N|--no-par-refine\n"
    "\tdisable parameter refinement from level to level\n"
    " -s|--symm\n"
    "\ttreat as symmetric problems\n"
    " -\n"
    "\tindicator for reading Options from stdin\n"
    "\n"
    "examples:\n"
    "\n"
    "\t./demo my_test\n"
    " use default setting solve the problem in \"my_test\" directory.\n"
    "\t./demo my_test - < ./parameters.cfg\n"
    " solve with user-defined parameters and read in from stdin\n"
    "\t./demo my_test -R 1e-12 -t 1e-4\n"
    " solve with rtol=1e-12 and drop-tol=1e-4\n"
    "\n";

// parse input arguments:
//  return (control parameters, thin or augmented, restart, tolerance, symm)
inline static std::tuple<Options, int, double, bool> parse_args(int   argc,
                                                                char *argv[]);

// get the input data, (A, b, m)
inline static std::tuple<crs_t, array_t, array_t::size_type> get_inputs(
    string dir);

int main(int argc, char *argv[]) {
  Options opts;
  int     restart;
  double  rtol;
  bool    symm;
  // parse arguments
  std::tie(opts, restart, rtol, symm) = parse_args(argc, argv);
  if (opts.verbose == VERBOSE_NONE) warn_flag(0);
  crs_t              A;
  array_t            b;
  array_t::size_type m;
  // read input data
  std::tie(A, b, m) = get_inputs(std::string(argv[1]));
  if (symm && m == 0u) {
    std::cerr << "Warning! Input file doesn\'t contain the leading size\n"
              << "for symmetric system, use the overall size instead\n";
    m = A.nrows();
  } else if (!symm)
    m = 0;
  std::cout << "rtol=" << rtol << ", restart=" << restart
            << "\nNumberOfUnknowns=" << A.nrows() << ", nnz(A)=" << A.nnz()
            << "\n"
            << "symmetric=" << symm << ", leading-block=" << m << "\n\n"
            << opt_repr(opts) << std::endl;

  array_t x(b.size());  // solution
  crs_t   A2(A, true);
  std::cout << "eliminated " << A2.eliminate(1e-15) << " small entries\n";

  DefaultTimer timer;

  // build preconditioner
  timer.start();
  std::shared_ptr<prec_t> _M(new prec_t());
  auto &                  M = *_M;
  M.factorize(A2, m, opts, true);
  timer.finish();
  hilucsi_info(
      "\nMLILU done!\n"
      "\tfill-in: %.2f%%\n"
      "\tfill-in (E and F): %.2f%%\n"
      "\tnnz(E+F)/nnz(M)=%.2f%%\n"
      "\tlevels: %zd\n"
      "\tspace-dropping ratio=%.2f%%\n"
      "\ttime: %.4gs\n",
      100.0 * M.nnz() / A.nnz(), 100.0 * M.nnz_EF() / A.nnz(),
      100.0 * M.nnz_EF() / M.nnz(), M.levels(), 100.0 * M.stats(5) / M.stats(4),
      timer.time());

  // solve
  timer.start();
  solver_t solver(_M);
  solver.rtol    = rtol;
  solver.restart = restart;
  int                 flag;
  solver_t::size_type iters;
  std::tie(flag, iters) =
      solver.solve_precond(A, b, x, false, false, opts.verbose);
  timer.finish();
  const double rs = iters ? solver.resids().back() : -1.0;
  hilucsi_info(
      "\nFGMRES(%d,%.1e) done!\n"
      "\tflag: %s\n"
      "\titers: %zd\n"
      "\tres: %.4g\n"
      "\ttime: %.4gs\n",
      restart, rtol, ksp::flag_repr(solver_t::repr(), flag).c_str(), iters, rs,
      timer.time());
  return flag;
}

inline static std::tuple<Options, int, double, bool> parse_args(int   argc,
                                                                char *argv[]) {
  Options opts    = get_default_options();
  int     restart = 30;
  double  tol     = 1e-6;
  bool    symm    = false;
  opts.verbose    = VERBOSE_NONE;
  if (argc < 2) {
    std::cerr << "Missing input directory!\n" << help;
    std::exit(1);
  }
  std::string arg;
  arg = argv[1];
  if (arg == string("-h") || arg == string("--help")) {
    std::cout << help;
    std::exit(0);
  }
  if (arg == string("-V") || arg == string("--version")) {
    std::cout << "PSMILU version: " << version() << std::endl;
    std::exit(0);
  }
  if (arg == string("") || arg[0] == '-') {
    std::cerr << "Invalid input directory " << arg << std::endl;
    std::exit(1);
  }
  auto fatal_exit = [&](const char *reason) {
    std::cerr << reason << std::endl;
    std::exit(1);
  };
  int i(2);
  for (;;) {
    if (i >= argc) break;
    arg = argv[i];
    if (arg == string("-h") || arg == string("--help")) {
      std::cout << help;
      std::exit(0);
    }
    if (arg == string("-V") || arg == string("--version")) {
      std::cout << "PSMILU version: " << version() << std::endl;
      std::exit(0);
    }
    if (arg == string("-t") || arg == string("--tau")) {
      ++i;
      if (i >= argc) fatal_exit("missing drop tolerance (tau) value!");
      opts.tau_L = opts.tau_U = std::atof(argv[i]);
    } else if (arg == string("-k") || arg == string("--kappa")) {
      ++i;
      if (i >= argc) fatal_exit("missing inverse norm thres (kappa) value!");
      opts.tau_d = opts.tau_kappa = std::atof(argv[i]);
    } else if (arg == string("-a") || arg == string("--alpha")) {
      ++i;
      if (i >= argc) fatal_exit("missing space control (alpha) value!");
      opts.alpha_L = opts.alpha_U = std::atoi(argv[i]);
    } else if (arg == string("-v") || arg == string("--verbose")) {
      ++i;
      if (i >= argc) fatal_exit("missing verbose level!");
      opts.verbose = std::atoi(argv[i]);
    } else if (arg == string("-r") || arg == string("--reorder")) {
      ++i;
      if (i >= argc) fatal_exit("missing reorder method tag!");
      opts.reorder = std::atoi(argv[i]);
    } else if (arg == string("-m") || arg == string("--matching")) {
      ++i;
      if (i >= argc) fatal_exit("missing matching method tag!");
      opts.matching = std::atoi(argv[i]);
    } else if (arg == string("-I") || arg == string("-pre-scale")) {
      ++i;
      if (i >= argc) fatal_exit("missing pre-scale type!");
      opts.pre_scale = std::atoi(argv[i]);
    } else if (arg == string("-S") || arg == string("--symm-pre-lvls")) {
      ++i;
      if (i >= argc) fatal_exit("missing number of symmetric pre levels!");
      opts.symm_pre_lvls = std::atoi(argv[i]);
    } else if (arg == string("-n") || arg == string("--no-saddle")) {
      opts.saddle = 0;
    } else if (arg == string("-N") || arg == string("--no-par-refine")) {
      opts.rf_par = 0;
    } else if (arg == string("-")) {
      // read options from stdin
      std::cout << "read options from stdin" << std::endl;
      std::cin >> opts;
    } else if (arg == string("-s") || arg == string("--symm")) {
      symm = true;
    } else if (arg == string("-T") || arg == string("--rtol")) {
      ++i;
      if (i >= argc) fatal_exit("missing GMRES rtol value!");
      tol = std::atof(argv[i]);
    } else if (arg == string("-R") || arg == string("--restart")) {
      ++i;
      if (i >= argc) fatal_exit("missing GMRES restart value!");
      restart = std::atoi(argv[i]);
    }
    ++i;
  }
  return std::make_tuple(opts, restart, tol, symm);
}

inline static std::tuple<crs_t, array_t, array_t::size_type> get_inputs(
    string dir) {
  if (dir.back() != '/') dir += "/";
  const std::string  A_file = dir + "A.psmilu", b_file = dir + "b.txt";
  array_t::size_type m(0);
  crs_t              A = crs_t::from_bin(A_file.c_str(), &m);
  array_t            b(A.nrows());
  std::ifstream      f(b_file.c_str());
  if (!f.is_open()) {
    std::cerr << "cannot open file " << b_file << std::endl;
    std::exit(1);
  }
  for (auto &v : b) f >> v;
  f.close();
  return std::make_tuple(A, b, m);
}
