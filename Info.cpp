/*
 * Copyright (c) Martin Kinkelin
 *
 * See the "License.txt" file in the root directory for infos
 * about permitted and prohibited uses of this code.
 */

#include <cmath>
#include <algorithm> // for min/max
#include <exception>
#include "Info.h"
#include "WinRing0.h"

#include <assert.h>

#include <stdio.h>

//top voltage, fixed value used for calculating VIDs and stuff, do not change!!!
#define V155 1.55
#define V1325 1.325 //my original turbo state voltage!

using std::min;
using std::max;

// special divisors for family 0x12 (aka 18 in decimal)
static const double DIVISORS_12[] = { 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 0.0 };


void FindFraction(double value, const double* divisors,
    int& numerator, int& divisorIndex,
    const int minNumerator, const int maxNumerator) {
  // limitations: non-negative value and divisors

  // count the null-terminated and ascendingly ordered divisors
  int numDivisors = 0;
  for (; divisors[numDivisors] > 0; numDivisors++) { }

  // make sure the value is in a valid range
  value = max(minNumerator / divisors[numDivisors-1], min(maxNumerator / divisors[0], value));

  // search the best-matching combo
  double bestValue = -1.0; // numerator / divisors[divisorIndex]
  for (int i = 0; i < numDivisors; i++) {
    const double d = divisors[i];
    const int n = max(minNumerator, min(maxNumerator, (int)(value * d)));
    const double myValue = n / d;

    if (myValue <= value && myValue > bestValue) {
      numerator = n;
      divisorIndex = i;
      bestValue = myValue;

      if (bestValue == value)
        break;
    }
  }
}

PStateInfo Info::ReadPState(int index) const {
  const uint64_t msr = Rdmsr(0xc0010064 + index);

  PStateInfo result;
  result.Index = index;

  int fid, did;
  fid = GetBits(msr, 4, 5);
  did = GetBits(msr, 0, 4);

  result.Multi = DecodeMulti(fid, did);

  result.VID = GetBits(msr, 9, 7);

  fprintf(stdout,"!! ReadPState index:%d fid:%d did:%d multi:%02.2f vid:%d\n", 
      result.Index, fid, did, result.Multi, result.VID);
  return result;
}

bool Info::WritePState(const PStateInfo& info) const {
  const uint32_t regIndex = 0xc0010064 + info.Index;
  uint64_t msr = Rdmsr(regIndex);

  const int fidbefore = GetBits(msr, 4, 5);
  const int didbefore = GetBits(msr, 0, 4);
  const double Multi = DecodeMulti(fidbefore, didbefore);
  const int VID = GetBits(msr, 9, 7);
  fprintf(stdout,"!! Write PState(1of3) read : fid:%d did:%d vid:%d Multi:%f\n", fidbefore, didbefore, VID, Multi);

  assert(info.Multi >= CPUMINMULTIunderclocked);
  assert(info.Multi <= CPUMAXMULTIunderclocked);

  int fid, did;
  EncodeMulti(info.Multi, fid, did);
  if ((fid != fidbefore) || (did != didbefore)) {
    SetBits(msr, fid, 4, 5);
    SetBits(msr, did, 0, 4);

    assert(info.VID >= CPUMAXVIDunderclocked);
    assert(info.VID <= CPUMINVIDunderclocked);
    SetBits(msr, info.VID, 9, 7);

    fprintf(stdout,"!! Write PState(2of3) write:%d did:%d vid:%d (multi:%02.2f) ...\n", fid, did, info.VID, info.Multi);
    Wrmsr(regIndex, msr);
    fprintf(stdout,"!! Write PState(3of3) write: done.\n");
    return true;
  } else {
    fprintf(stdout,"!! Write PState(2of3 3of3) no write needed: same values. Done.\n");
    return false;
  }
}


int Info::GetCurrentPState() const {
  const uint64_t msr = Rdmsr(0xc0010071);
  const int i = GetBits(msr, 16, 3);//0..7
  return i;
}

void Info::SetCurrentPState(int index) const {
  if (index < 0 || index >= NUMPSTATES)
    throw ExceptionWithMessage("P-state index out of range");

  index -= 1;//NumBoostStates;
  if (index < 0)
    index = 0;

  const uint32_t regIndex = 0xc0010062;
  uint64_t msr = Rdmsr(regIndex);
  SetBits(msr, index, 0, 3);
  Wrmsr(regIndex, msr);
}



inline double Info::DecodeMulti(const int fid, const int did) const {
  return (fid + 16) / DIVISORS_12[did];
}

inline void Info::EncodeMulti(const double multi, int& fid, int& did) const {
  const int minNumerator = 16; // numerator: 0x10 = 16 as fixed offset
  const int maxNumerator = 31 + minNumerator; // 5 bits => max 2^5-1 = 31

  int numerator, divisorIndex;
  FindFraction(multi, DIVISORS_12, numerator, divisorIndex, minNumerator, maxNumerator);

  fid = numerator - minNumerator;
  did = divisorIndex;
}


double Info::DecodeVID(const int vid) const {
  return V155 - vid * CPUVIDSTEP;
}

int Info::EncodeVID(double vid) const {
  assert(CPUVIDSTEP > 0);
  assert(vid > 0.0);
  assert(vid < V155);
  //^ wanna catch the mistake rather than just round to the limits

  //XXX: here, just making sure input vid doesn't exceed 1.325V ! (my CPU)
  vid = max(0.0, min(V1325, vid));//done: use a less than 1.55 max voltage there, depending on reported one which is 1.325V for my cpu eg. 1.45 shouldn't be allowed!; OK, maybe that 1.55 is something else... in which case ignore all this.

  assert(vid<=V1325);
  assert(vid >= CPUMINVOLTAGEunderclocked); //that's the lowest (pstate7) stable voltage for my CPU, multi:8x
  //    assert(vid<=1.0875); //that's highest (pstate0) stable voltage for my CPU, multi:22x; but initially it's 1.325V at 8x pstate0, before the downclocking!
  assert(vid <= CPUMAXVOLTAGE);//when not underclocked, this is tops
  // round to nearest step
  int r = (int)(vid / CPUVIDSTEP + 0.5);

  //1.55 / VIDStep = highest VID (124)
  int res= (int)(V155 / CPUVIDSTEP) - r;//VIDStep is 0.0125; so, 124 - 87(for 1.0875 aka 22x multi) = 37
  assert(res >= CPUMAXVID);//multi 23x, fid 30, did 2, vid 18, pstate0 (highest) normal clocked
  assert(res <= CPUMINVIDunderclocked);//multi 8x, fid 0, did 2 vid 67, pstate7(lowest) underclocked
  return res;
}



