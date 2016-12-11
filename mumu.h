#pragma once

#include <inttypes.h> //for uint32_t uint64_t

struct PStateInfo {
    double multi; //multiplier ( multiply with the reference clock of 100Mhz eg. multi*REFERENCECLOCK)
    double strvid; //real life voltage eg. 1.325V as a double
    int VID; //vid, eg. 18 (for 1.325V) or 67 (for 1.0875V)
};

// special divisors for family 0x12 (aka 18 in decimal)
static const double DIVISORS_12[] = { 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 0.0 };

//top voltage, fixed value used for calculating VIDs and stuff, do not change!!!
#define V155 1.55
#define V1325 1.325 //my original turbo state voltage!

#define DEFAULTREFERENCECLOCK 100 //MHz
#define REFERENCECLOCK DEFAULTREFERENCECLOCK //for my CPU, is the same 100MHz (unused)

#define NUMPSTATES 8
#define NUMCPUCORES 4 //4 cores, for my CPU
#define CPUFAMILY 0x12
#define CPUMODEL 0x1
#define CPUMINMULTI 1.0
#define CPUMAXMULTI 40.0 //24+16
#define CPUVIDSTEP 0.0125 //fixed; default step for pre SVI2 platforms

#define CPUMINVID 88 //1.55 - 88*0.0125 = 0.450
#define CPUMINVOLTAGE 0.4500

#define CPUMAXVID 18 //1.55 - 18*0.0125 = 1.325; multi 23x, fid 30, did 2, vid 18, pstate0 (highest) normal clocked
#define CPUMAXVOLTAGE 1.3250 //multi 23x, fid 30, did 2, vid 18, pstate0 (highest) normal clocked

//#define CPUMAXVIDunderclocked 37 //1.0875V fid:6 did:0 multi:22.00 vid:37
#define CPUMAXVIDunderclocked 18 //1.3250V fid:14 did:0 multi:30.00 vid:18
#define CPUMAXVOLTAGEunderclocked 1.0875 //1.55 - 37*0.0125 = 1.0875; fid:6 did:0 multi:22.00 vid:37

#define CPUMINMULTIunderclocked 8 //multi 8x, fid 0, did 2 vid 67, pstate7(lowest) underclocked
//#define CPUMAXMULTIunderclocked 22 //1.0875V fid:6 did:0 multi:22.00 vid:37
#define CPUMAXMULTIunderclocked 30 //1.3250V fid:14 did:0 multi:30.00 vid:18

#define CPUMINVIDunderclocked 67 //multi 8x, fid 0, did 2 vid 67, pstate7(lowest) underclocked
#define CPUMINVOLTAGEunderclocked 0.7125 //1.55 - 67*0.0125 = .7125

const struct PStateInfo  __attribute__((unused)) bootdefaults_psi[NUMPSTATES]={//XXX: fyi only, do not use this!
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
const struct PStateInfo allpsi[NUMPSTATES]={//stable underclocking for my CPU:
  {30.0, 1.3250, 18}, //P0, boost
//  {22.0, 1.0875, 37}, //P0, boost
  {22.0, 1.0875, 37}, //P0, normal max #ADDED now for testing
  {20.0, 1.0250, 42}, //P1, normal
  {18.0, 0.9625, 47},
  {17.0, 0.9375, 49},
  {16.0, 0.9, 52},
  {14.0, 0.8625, 55},
//  {12.0, 0.8125, 59}, #REMOVED now for testing (see above added one)
  {8.0, 0.7125, 67} //P7, normal
};

template <typename T> uint32_t GetBits(T value, unsigned char offset, unsigned char numBits) {
    const T mask = (((T)1 << numBits) - (T)1); // 2^numBits - 1; after right-shift
    return (uint32_t)((value >> offset) & mask);
}

template <typename T> void SetBits(T& value, uint32_t bits, unsigned char offset, unsigned char numBits) {
    const T mask = (((T)1 << numBits) - (T)1) << offset; // 2^numBits - 1, shifted by offset to the left
    value = (value & ~mask) | (((T)bits << offset) & mask);
}

class ExceptionWithMessage: public std::exception {
    const char* what;

public:

    ExceptionWithMessage(std::string msg) {
        this->what = msg.c_str();
    }
};

//thanks to: https://stackoverflow.com/questions/5459868/c-preprocessor-concatenate-int-to-string?answertab=active#tab-top
#define STR_HELPER(x) #x

#define termESC "\033"
#define termESC_openparen termESC "["
#define termCOLOR(type, color) \
  termESC_openparen STR_HELPER(type) ";5;" STR_HELPER(color) "m"
#define fgCOLOR 38
#define bgCOLOR 48

#define termCOLORfg(color) \
  termCOLOR(fgCOLOR, color)

#define termCOLORbg(color) \
  termCOLOR(bgCOLOR, color)

#define termCOLORreset \
  termESC_openparen "m" termESC "(B"

#define startREDcolortext \
    termCOLORfg(1)

#define startPURPLEcolortext \
    termCOLORfg(5)

#define endcolor \
    termCOLORreset

#define startYELLOWcolortext \
    termCOLORfg(3)

#define ERRORtext(txt) \
    startREDcolortext txt endcolor

#define pERR(txt) \
  perror(ERRORtext(txt))


