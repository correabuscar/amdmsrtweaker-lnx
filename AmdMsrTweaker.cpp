/*
 * Copyright (c) Martin Kinkelin
 *
 * See the "License.txt" file in the root directory for infos
 * about permitted and prohibited uses of this code.
 */

#include <cstdio>
#include <iostream>
#include "mumu.h"

#include <unistd.h> //for sleep(sec)
#include <stdlib.h> //for exit

#include <vector>

#include <string.h>
//#include <stdint.h> //for uint32_t uint64_t
#include <inttypes.h> //for uint32_t uint64_t

#include <assert.h>

#include <fcntl.h> //for O_RDONLY

using std::cout;
using std::cerr;
using std::endl;


void PrintInfo();//forward declaration
void ParseParams();
void ApplyChanges();

/*const int count=1+8;
  const char* params[count]={
  "self"
  ,"P0=22@1.0875"
  ,"P1=20@1.0250"
  ,"P2=18@0.9625"
  ,"P3=17@0.9375"
  ,"P4=16@0.9"
  ,"P5=14@0.8625"
  ,"P6=12@0.8125"
  ,"P7=8@0.7125"
  };*/

/// <summary>Entry point for the program.</summary>
int main(int argc, const char* argv[]) {
  cout << endl;
  cout << "AmdMsrTweaker v1.1 modified for my own Lenovo Z575 ONLY!!! (voltages are fixed, params ignored!)" << endl;
  cout << "argv[0] is: " << argv[0] << endl;
  try {
    if ((argc > 1)and(0 == strncmp("I wanna brick my system!", argv[1],25))) {//we make sure, because we're about to apply preset voltages!(hardcoded in source code)
      ParseParams();

      fprintf(stdout,"Before:\n");
      PrintInfo();
      ApplyChanges();
      fprintf(stdout,"After:\n");
      PrintInfo();
    } else {
      PrintInfo();
    }
  } catch (const std::exception& e) {
    cerr << "ERROR: " << e.what() << endl;
    return 10;
  }

  return 0;
}

int inline get_num_cpu() {
    return 4;//assume 4 cores - my CPU
}

uint64_t Rdmsr(uint32_t index) {
    uint64_t result[4]={0,0,0,0};
    char path[255]= "\0";

    for (int i = 0; i < get_num_cpu(); i++) {
      int ret=sprintf(path, "/dev/cpu/%d/msr", i);
      if (ret<0) {
        pERR("snprintf failed");
        fprintf(stderr,"!! snprintf ret=%d\n",ret);
        exit(-1);
      }

      fprintf(stdout, startYELLOWcolortext "  !! Rdmsr: %s idx:%x ... %lu bytes ... ", path, index, sizeof(result[i]));
      int msr = open(path, O_RDONLY);
      if (msr == -1) {
        pERR("Failed to open msr device for reading. You need: # modprobe msr");
        exit(-1);
      }
      if (sizeof(result[i]) != pread(msr, &(result[i]), sizeof(result[i]), index)) {//read 8 bytes
        pERR("Failed to read from msr device");
      }
      close(msr);
      fprintf(stdout," done. (result==%"PRIu64" hex:%08x%08x)" endcolor "\n", result[i], (unsigned int)(result[i] >> 32), (unsigned int)(result[i] & 0xFFFFFFFF));
      if (i>0) {
        if (result[i-1] != result[i]) {
          pERR("Rdmsr: different results for cores");
          fprintf(stderr,"!! core[%d]==%"PRIu64" != core[%d]==%"PRIu64"\n", i-1, result[i-1], i, result[i]);
        }
      }
    }

    return result[0];
}

void Wrmsr(uint32_t index, const uint64_t& value) {
    char path[255]= "\0";

    for (int i = 0; i < get_num_cpu(); i++) {
        int ret=sprintf(path, "/dev/cpu/%d/msr", i);
        if (ret<0) {
          pERR("snprintf failed");
          exit(-1);
        }
        //fprintf(stdout,"!! Wrmsr: %s idx:%"PRIu32" val:%"PRIu64"\n", path, index, value);
        fprintf(stdout, startPURPLEcolortext "  !! Wrmsr: %s idx:%x val:%"PRIu64" valx:%08x%08x... ", path, index, value, (unsigned int)(value >> 32), (unsigned int)(value & 0xFFFFFFFF));
        int msr = open(path, O_WRONLY);
        if (msr == -1) {
            pERR("Failed to open msr device for writing");
            exit(-1);
        }
        if(pwrite(msr, &value, sizeof(value), index) != sizeof(value)) {
            pERR("Failed to write to msr device");
        }
        close(msr);
        fprintf(stdout," done." endcolor "\n");
    }
}


void FindFraction(double value, const double* divisors,
    int& numerator, int& divisorIndex,
    const int minNumerator, const int maxNumerator) {
  // limitations: non-negative value and divisors

  // count the null-terminated and ascendingly ordered divisors
  int numDivisors = 0;
  for (; divisors[numDivisors] > 0; numDivisors++) { }

  // make sure the value is in a valid range
  value = std::max(minNumerator / divisors[numDivisors-1], std::min(maxNumerator / divisors[0], value));

  // search the best-matching combo
  double bestValue = -1.0; // numerator / divisors[divisorIndex]
  for (int i = 0; i < numDivisors; i++) {
    const double d = divisors[i];
    const int n = std::max(minNumerator, std::min(maxNumerator, (int)(value * d)));
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

inline double DecodeMulti(const int fid, const int did) {
  return (fid + 16) / DIVISORS_12[did];
}

inline void EncodeMulti(const double multi, int& fid, int& did) {
  const int minNumerator = 16; // numerator: 0x10 = 16 as fixed offset
  const int maxNumerator = 31 + minNumerator; // 5 bits => max 2^5-1 = 31

  int numerator, divisorIndex;
  FindFraction(multi, DIVISORS_12, numerator, divisorIndex, minNumerator, maxNumerator);

  fid = numerator - minNumerator;
  did = divisorIndex;
}

PStateInfo ReadPState(int index) {
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

bool WritePState(const PStateInfo& info) {
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


int GetCurrentPState() {
  const uint64_t msr = Rdmsr(0xc0010071);
  const int i = GetBits(msr, 16, 3);//0..7
  return i;
}

void SetCurrentPState(int index) {
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





double DecodeVID(const int vid) {
  return V155 - vid * CPUVIDSTEP;
}

int EncodeVID(double vid) {
  assert(CPUVIDSTEP > 0);
  assert(vid > 0.0);
  assert(vid < V155);
  //^ wanna catch the mistake rather than just round to the limits

  //XXX: here, just making sure input vid doesn't exceed 1.325V ! (my CPU)
  vid = std::max(0.0, std::min(V1325, vid));//done: use a less than 1.55 max voltage there, depending on reported one which is 1.325V for my cpu eg. 1.45 shouldn't be allowed!; OK, maybe that 1.55 is something else... in which case ignore all this.

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



using std::cerr;
using std::endl;
using std::min;
using std::max;
using std::string;
using std::tolower;
using std::vector;

std::vector<PStateInfo> _pStates;

void ParseParams() {
  struct somestruct {
    double multi;
    double strvid;
    int VID;
  };
  const somestruct  __attribute__((unused)) bootdefaults_psi[8]={//XXX: fyi only, do not use this!
    {23.0, 1.325, 18}, //P0, boost
    {14.0, 1.0625, 39}, //P1, normal
    {13.0, 1.025, 42},
    {12.0, 0.9875, 45},
    {11.0, 0.975, 46},
    {10.0, 0.9625, 47},
    {9.0, 0.95, 48},
    {8.0, 0.925, 50} //P7, normal
  };
  //bootdefaults_psi;//prevent -Wunused-variable warning; nvm, got statement has no effect  warning. What I actually need is:  __attribute__((unused))  src: https://stackoverflow.com/questions/15053776/how-do-you-disable-the-unused-variable-warnings-coming-out-of-gcc

  const somestruct allpsi[8]={//stable underclocking for my CPU:
    {22.0, 1.0875, 37}, //P0, boost
    {20.0, 1.0250, 42}, //P1, normal
    {18.0, 0.9625, 47},
    {17.0, 0.9375, 49},
    {16.0, 0.9, 52},
    {14.0, 0.8625, 55},
    {12.0, 0.8125, 59},
    {8.0, 0.7125, 67} //P7, normal
  };

  PStateInfo psi;
  psi.Multi = psi.VID = -1; //psi.NBVID = -1;
  fprintf(stdout,"Hardcoded values:\n");
  for (int i = 0; i < NUMPSTATES; i++) {
    _pStates.push_back(psi);
    //        _pStates.back().Index = i;//very important!
    _pStates[i].Index = i;//^ equivalent
    _pStates[i].Multi = allpsi[i].multi;//eg. 22.0
    _pStates[i].VID = EncodeVID(allpsi[i].strvid /*eg. 1.0875*/);//atof(vid.c_str()));
    assert( allpsi[i].VID/*eg. 37*/ == _pStates[i].VID);
    assert( i == _pStates[i].Index );
    fprintf(stdout,"pstate:%d multi:%02.2f vid:%d\n",// voltage:%d\n", 
        i, 
        _pStates[i].Multi,
        _pStates[i].VID
        );
  }
}


void ApplyChanges() {
  //pstates stuff:
  bool modded=false;
  for (size_t i = 0; i < _pStates.size(); i++) {
    modded=WritePState(_pStates[i]) | modded;
  }

  if (modded) {
    fprintf(stdout, "Switching to another p-state temporarily so to ensure current one uses newly applied values\n");

    const int currentPState = GetCurrentPState();

    //we switch to another pstate temporarily, then back again so that it takes effect (apparently that's why, unsure, it's not my coding)
    const int lastpstate= NUMPSTATES - 1;//aka the lowest speed one
    const int tempPState = (currentPState == lastpstate ? 0 : lastpstate);
    fprintf(stdout,"!! currentpstate:%d temppstate:%d\n", currentPState, tempPState);
    SetCurrentPState(tempPState);
    sleep(1);//1 second
    SetCurrentPState(currentPState);
  }
}

void PrintInfo() {
  for (int i = 0; i < NUMPSTATES; i++) {
    const PStateInfo pi = ReadPState(i);

    cout << "  P" << i << ": " << pi.Multi << "x at " << DecodeVID(pi.VID) << "V vid:"<< pi.VID << endl;

  }
}
