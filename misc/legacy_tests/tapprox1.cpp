/* Copyright (C) 2012-2020 IBM Corp.
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
#include <NTL/ZZ.h>
#include <algorithm>
#include <complex>

#include <helib/norms.h>
#include <helib/helib.h>
#include <helib/debugging.h>
#include <helib/ArgMap.h>
#include <helib/EncryptedArray.h>
#include <helib/log.h>
#include <helib/fhe_stats.h>

NTL_CLIENT
using namespace helib;

bool verbose = false;

bool reset = false;


void resetPtxtMag(Ctxt& c, const PtxtArray& p)
{
  double maxAbs = NextPow2(Norm(p));
  c.setPtxtMag(NTL::xdouble(maxAbs));
}

/************** Each round consists of the following:
tmp1 = rotate(c0)
tmp1 += const1
c0 += const2
c0 *= tmp1  // c0 = (rotate(c0) + const1)*(c0 + const2) ...squared every round

tmp2 = c1 * const1
c1 = rotate(c1)
c1 += tmp2  // c1 = rotate(c1) + c1*const1 ...doubled every round


tmp3 = c2 * const2
c2 *= c3
c2 += tmp3 // c2 = = c2*c3 + c2*const2 = c2*(c3 + const2)

c3 = c3*const1
**************/

void debugCompare(const SecKey& sk,
                  const PtxtArray& p,
                  const Ctxt& c)
{
  PtxtArray pp(p.getView());
  pp.decrypt(c, sk);

  double err = Distance(pp, p);
  double err_bound = convert<double>(c.errorBound());
  //double rel_err = abs_err / Norm(p);
  std::cout << "   "
            << " err=" << err 
            << " err_bound=" << err_bound
            //<< " rel_err=" << rel_err
            //<< "   "
            //<< " mag=" << Norm(p)
            //<< " mag_bound=" << c.getPtxtMag()
            //<< " scale=" << c.getRatFactor()
            << "\n";
  if (err > err_bound) std::cout << "**** BAD BOUND\n";
}

#define DEBUG_COMPARE(C, P, M)                                                 \
  do {                                                                         \
    if (verbose) {                                                             \
      CheckCtxt(C, M);                                                         \
      debugCompare(secretKey, P, C);                                       \
    }                                                                          \
  } while (0)

void testGeneralOps(const PubKey& publicKey,
                    const SecKey& secretKey,
                    long nRounds)
{
  const Context& context = publicKey.getContext();


  PtxtArray p0(context), p1(context), p2(context), p3(context);
  p0.random();
  p1.random();
  p2.random();
  p3.random();

  Ctxt c0(publicKey), c1(publicKey), c2(publicKey), c3(publicKey);
  p0.encrypt(c0);
  p1.encrypt(c1);
  p2.encrypt(c2);
  p3.encrypt(c3);

  HELIB_NTIMER_START(Circuit);

  for (long i = 0; i < nRounds; i++) {

    if (verbose)
      std::cout << "*** round " << i << "..." << std::endl;

    if (reset) {
      resetPtxtMag(c0, p0);
      resetPtxtMag(c1, p1);
      resetPtxtMag(c2, p2);
      resetPtxtMag(c3, p3);
    }

    DEBUG_COMPARE(c0, p0, "c0");
    DEBUG_COMPARE(c1, p1, "c1");
    DEBUG_COMPARE(c2, p2, "c2");
    DEBUG_COMPARE(c3, p3, "c3");

    long nslots = context.zMStar.getNSlots();

    long rotamt = RandomBnd(2 * nslots - 1) - (nslots - 1);
    // random number in [-(nslots-1)..nslots-1]

    // two random constants
    PtxtArray const1(context), const2(context);
    const1.random();
    const2.random();

    PtxtArray tmp1_p(p0);
    rotate(tmp1_p, rotamt);
    Ctxt tmp1(c0);
    rotate(tmp1, rotamt);
    DEBUG_COMPARE(tmp1, tmp1_p, "tmp1 = rotate(c0)");

    tmp1_p += const1;
    tmp1 += const1;
    DEBUG_COMPARE(tmp1, tmp1_p, "tmp1 += const1");

    p0 += const2;
    c0 += const2;
    DEBUG_COMPARE(c0, p0, "c0 += const2");

    p0 *= tmp1_p;
    c0.multiplyBy(tmp1);
    DEBUG_COMPARE(c0, p0, "c0 *= tmp1");

#if 0
    p0 -= 17.5;
    c0 -= 17.5;
    DEBUG_COMPARE(c0, p0, "c0 -= 17.5");

    p0 *= 1.25;
    c0 *= 1.25;
    DEBUG_COMPARE(c0, p0, "c0 *= 1.25");
#endif

    PtxtArray tmp2_p(p1);
    tmp2_p *= const1;
    Ctxt tmp2(c1);
    tmp2 *= const1;
    DEBUG_COMPARE(tmp2, tmp2_p, "tmp2 = c1 * const1");

    rotate(p1, rotamt);
    rotate(c1, rotamt);
    DEBUG_COMPARE(c1, p1, "c1 = rotate(c1)");

#if 0
    //std::cerr << "*********** shamt=" << rotamt << "\n";
    //std:: cerr << p1 << "\n";
    shift(p1, rotamt);
    //std:: cerr << p1 << "\n";
    shift(c1, rotamt);
    DEBUG_COMPARE(c1, p1, "c1 = shift(c1)");
#endif

    p1 += tmp2_p;
    c1 += tmp2;
    DEBUG_COMPARE(c1, p1, "c1 += tmp2");

    PtxtArray tmp3_p(p2);
    tmp3_p *= const2;
    Ctxt tmp3(c2);
    tmp3 *= const2;
    DEBUG_COMPARE(tmp3, tmp3_p, "tmp3 = c2 * const2");

    p2 *= p3;
    c2.multiplyBy(c3);
    DEBUG_COMPARE(c2, p2, "c2 *= c3");

    p2 += tmp3_p;
    c2 += tmp3;
    DEBUG_COMPARE(c2, p2, "c2 += tmp3");

    p3 *= const1;
    c3 *= const1;
    DEBUG_COMPARE(c3, p3, "c3 *= const1");

    if (verbose) {
      // Check correctness after each round
      PtxtArray pp0(context), pp1(context), pp2(context), pp3(context);

      pp0.decrypt(c0, secretKey);
      pp1.decrypt(c1, secretKey);
      pp2.decrypt(c2, secretKey);
      pp3.decrypt(c3, secretKey);

      if (!(pp0 == Approx(p0) && pp1 == Approx(p1) && pp2 == Approx(p2) &&
          pp3 == Approx(p3))) {
        std::cout << "FAIL AT ROUND " << i << "\n";
        break;
      }
    }
  }

  HELIB_NTIMER_STOP(Circuit);

  PtxtArray pp0(context), pp1(context), pp2(context), pp3(context);

  pp0.decrypt(c0, secretKey);
  pp1.decrypt(c1, secretKey);
  pp2.decrypt(c2, secretKey);
  pp3.decrypt(c3, secretKey);

  if (pp0 == Approx(p0) && pp1 == Approx(p1) && pp2 == Approx(p2) &&
      pp3 == Approx(p3)) 
    std::cout << "SUCCESS\n";
  else
    std::cout << "FAIL\n";


  if (verbose) {
    std::cout << std::endl;
    // printAllTimers();
    std::cout << std::endl;
  }
  resetAllTimers();
}

int main(int argc, char* argv[])
{
  helog.setLogToStderr();

  // Commandline setup

  ArgMap amap;

  long m = 16;
  long r = 8;
  long L = 0;
  double epsilon = 0.01; // Accepted accuracy
  long R = 1;
  long seed = 0;
  bool debug = false;

  amap.arg("m", m, "Cyclotomic index");
  amap.note("e.g., m=1024, m=2047");
  amap.arg("r", r, "Bits of precision");
  amap.arg("R", R, "number of rounds");
  amap.arg("L", L, "Number of bits in modulus", "heuristic");
  amap.arg("ep", epsilon, "Accepted accuracy");
  amap.arg("seed", seed, "PRG seed");
  amap.arg("verbose", verbose, "more printouts");
  amap.arg("debug", debug, "for debugging");
  amap.arg("reset", reset, "forces calls to setPtxtMag each round");

  amap.parse(argc, argv);

  if (seed)
    NTL::SetSeed(ZZ(seed));

  if (R <= 0)
    R = 1;
  if (L == 0) {
    if (R <= 2)
      L = 100 * R;
    else
      L = 220 * (R - 1);
  }

  if (verbose) {
    std::cout << "** m=" << m << ", #rounds=" << R << ", |q|=" << L
              << ", epsilon=" << epsilon << std::endl;
  }

  if (verbose) fhe_stats = true;

  // FHE setup keys, context, SKMs, etc

  Context context(m, /*p=*/-1, r);
  //context.scale = 4; // why is this 4?
  buildModChain(context, L);

  SecKey secretKey(context);
  secretKey.GenSecKey();        // A +-1/0 secret key
  addSome1DMatrices(secretKey); // compute key-switching matrices

  const PubKey& publicKey = secretKey;
  //const PubKey publicKey = secretKey;

  if (verbose) {
    std::cout << "security=" << context.securityLevel() << std::endl;
    context.zMStar.printout();
    std::cout << "r = " << context.alMod.getR() << std::endl;
    std::cout << "ctxtPrimes=" << context.ctxtPrimes
	 << ", specialPrimes=" << context.specialPrimes << std::endl
	 << std::endl;
  }
  if (debug) {
    dbgKey = &secretKey;
    dbgEa = context.ea;
  }
#ifdef HELIB_DEBUG
  dbgKey = &secretKey;
  dbgEa = context.ea;
#endif // HELIB_DEBUG

  // Run the tests.
  testGeneralOps(publicKey, secretKey, R);

  if (verbose) print_stats(cout);

  return 0;
}
