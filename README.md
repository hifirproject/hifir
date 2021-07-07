# Welcome to *HIFIR* Project #

## Introduction ##

Welcome to the `HIFIR` package! `HIFIR` stands for `H`ybrid `I`ncomplete `F`actorization with `I`terative `R`efinement, which is a *multilevel* preconditioner for ill-conditioned and (nearly) singular systems. `HIFIR` has (near) linear time complexity in its factorization and solve and is robust due to its multilevel structure, scalability-oriented dropping, and careful control of the condition numbers of the triangular factors.

## Installation ##

The C++ interface of `HIFIR` is header-only. You can simply do the following on a UNIX system

```console
sudo cp -r /path/to/hifir/src/* /usr/local/include
```

or copy `/path/to/hifir/src/*` to any user-level directory, for example,

```console
cp -r /path/to/hifir/src/* $HOME/.local/include
```
and then add `-I$HOME/.local/include` to the command line of your `c++` compiler.

## Copyright and Licenses ##

The `HIFIR` software suite (including `hifir4m` and `hifir4py`) are developed by the NumGeom Research Group at Stony Brook University.

The `HIFIR` software package is released under a dual-license model. For academic users, individual users, or open-source software developers, you can use HIFIR under the GNU Affero General Public License version 3 (AGPLv3+, see [`LICENSE`](./LICENSE)) free of charge for research and evaluation purpose. For commerical users, separate commerical licenses are available through the Stony Brook University. For inquiries regarding commercial licenses, please contact Prof. Xiangmin Jiao at xiangmin.jiao@stonybrook.edu.

## How to Cite `HIFIR` ##

If you use `HIFIR` in your research for nonsingular systems, please cite the `HILUCSI` paper:

```bibtex
@Article{chen2021hilucsi,
  author  = {Chen, Qiao and Ghai, Aditi and Jiao, Xiangmin},
  title   = {{HILUCSI}: Simple, robust, and fast multilevel {ILU} for
             large-scale saddle-point problems from {PDE}s},
  journal = {Numer. Linear Algebra Appl.},
  year    = {2021},
  doi     = {10.1002/nla.2400}
}
```

If you plan to use HIFIR in solving singular and ill-conditioned systems, please cite the following papers.

```bibtex
@article{jiao2020approximate,
  author  = {Xiangmin Jiao and Qiao Chen},
  journal = {arXiv},
  title   = {Approximate generalized inverses with iterative refinement for
             $\epsilon$-accurate preconditioning of singular systems},
  year    = {2020},
  note    = {arXiv:2009.01673}
}
```

```bibtex
@article{chen2021hifir,
  author  = {Chen, Qiao and Jiao, Xiangmin},
  title   = {{HIFIR}: Hybrid incomplete factorization with iterative refinement
             for preconditioning ill-conditioned and singular systems},
  journal = {arXiv},
  year    = {2021},
  note    = {arXiv:2106.09877}
}
```

## Contacts ##

- Qiao Chen, <qiao.chen@stonybrook.edu>, <benechiao@gmail.com>
- Xiangmin Jiao, <xiangmin.jiao@stonybrook.edu>, <xmjiao@gmail.com>
