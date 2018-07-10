/* Copyright (C) 2012-2017 IBM Corp.
 * This program is Licensed under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *   http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. See accompanying LICENSE file.
 */
/**
 * @file fft.cpp - computing the canonical embedding and related norms
 **/
#include <complex>
#include <cmath>
#include <numeric> // std::accumulate
#include <NTL/BasicThreadPool.h>
#include "NumbTh.h"
#include "timing.h"
#include "norms.h"
#include "PAlgebra.h"
NTL_CLIENT

const double pi = 4 * std::atan(1);

#ifdef FFT_ARMA
#warning "canonicalEmbedding implemented via Armadillo"
#include <armadillo>
void convert(zzX& to, const arma::vec& from)
{
  to.SetLength(from.size());
  NTL_EXEC_RANGE(to.length(), first, last)
  for (long i=first; i<last; i++)
    to[i] = std::round(from[i]);
  NTL_EXEC_RANGE_END
}
void convert(arma::vec& to, const zzX& from)
{
  to.resize(from.length());
  NTL_EXEC_RANGE(from.length(), first, last)
  for (long i=first; i<last; i++)
    to[i] = from[i];
  NTL_EXEC_RANGE_END
}

// Computing the canonical embedding. This function returns in v only
// the first half of the entries, the others are v[phi(m)-i]=conj(v[i])
void canonicalEmbedding(std::vector<cx_double>& v,
                        const zzX& f, const PAlgebra& palg)
{
  FHE_TIMER_START;
  long m = palg.getM();
  long phimBy2 = divc(palg.getPhiM(),2);
  arma::vec av; // convert to vector of doubles
  convert(av, f);
  arma::cx_vec avv = arma::fft(av,m); // compute the full FFT

  v.resize(phimBy2); // the first half of Zm*
  for (long i=1, idx=0; i<=m/2; i++)
    if (palg.inZmStar(i)) v[idx++] = avv[i];
  FHE_TIMER_STOP;
}

// Roughly the inverse of canonicalEmbedding, except for scaling and
// rounding issues. Calling embedInSlots(f,v,palg,1.0,strictInverse=true)
// after setting canonicalEmbedding(v, f, palg), is sure to recover the
// same f, but embedInSlots(f,v,palg,1.0,strictInverse=false) may return
// a different "nearby" f.
void embedInSlots(zzX& f, const std::vector<cx_double>& v,
                  const PAlgebra& palg, double scaling, bool strictInverse)
{
  FHE_TIMER_START;
  long m = palg.getM();
  arma::cx_vec avv(m);
  for (long i=1, idx=0; i<=m/2; i++) {
    if (palg.inZmStar(i)) {
      avv[i] = scaling*v[idx++];
      avv[m-i] = std::conj(avv[i]);
    }
    else // not in Zm*
      avv[m-i] = avv[i] = std::complex<double>(0.0,0.0);
  }
  arma::vec av = arma::real(arma::ifft(avv,m));

  // If v was obtained by canonicalEmbedding(v,f,palg,1.0) then we have
  // the guarantee that m*av is an integral polynomial, and moreover
  // m*av mod Phi_m(x) is in m*Z[X].
  if (strictInverse) av *= m; // scale up by m
  convert(f, av);    // round to an integer polynomial
  reduceModPhimX(f, palg);
  if (strictInverse) f /= m;  // scale down by m
  normalize(f);
}
#else
#ifdef FFT_NATIVE
#warning "canonicalEmbedding implemented via slow DFT, expect very slow key-generation"
// An extremely lame implementation of the canonical embedding

// evaluate poly(x) using Horner's rule
cx_double complexEvalPoly(const zzX& poly, const cx_double& x)
{
  if (lsize(poly)<=0) return cx_double(0.0,0.0);
  cx_double res(double(poly[0]), 0.0);
  for (long i=1; i<lsize(poly); i++) {
    res *= x;
    res += cx_double(double(poly[i]));
  }
  return res;
}

void canonicalEmbedding(std::vector<cx_double>& v, const zzX& f, const PAlgebra& palg)
{
  FHE_TIMER_START;
  long m = palg.getM();
  long phimBy2 = divc(palg.getPhiM(),2);
  vector<long> zmstar(phimBy2); // the first half of Zm*
  for (long i=1, idx=0; i<=m/2; i++)
    if (palg.inZmStar(i)) zmstar[idx++] = i;

  v.resize(phimBy2);
  NTL_EXEC_RANGE(phimBy2, first, last)
  for (long i=first; i < last; ++i) {
    auto rou = std::polar<double>(1.0, -(2*pi*zmstar[i])/m); // root of unity
    v[i] = complexEvalPoly(f,rou);
  }
  NTL_EXEC_RANGE_END
  FHE_TIMER_STOP;
}
void embedInSlots(zzX& f, const std::vector<cx_double>& v,
                  const PAlgebra& palg, bool strictInverse)
{
  NTL::Error("embedInSlots not implemented\n");
}
#endif // ifdef FFT_NATIVE
#endif // ifdef FFT_ARMA
