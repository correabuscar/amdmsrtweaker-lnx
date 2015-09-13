#pragma once

#include <inttypes.h> //for uint32_t uint64_t

struct PStateInfo {
  int Index;    // hardware index
  double Multi; // internal one for 100 MHz reference
  int VID;
};

// special divisors for family 0x12 (aka 18 in decimal)
static const double DIVISORS_12[] = { 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 0.0 };

//top voltage, fixed value used for calculating VIDs and stuff, do not change!!!
#define V155 1.55
#define V1325 1.325 //my original turbo state voltage!

#define DEFAULTREFERENCECLOCK 100 //MHz
#define REFERENCECLOCK DEFAULTREFERENCECLOCK //for my CPU, is the same 100MHz (unused)

#define NUMPSTATES 8
#define CPUFAMILY 0x12
#define CPUMODEL 0x1
#define CPUMINMULTI 1.0
#define CPUMAXMULTI 40.0 //24+16
#define CPUVIDSTEP 0.0125 //fixed; default step for pre SVI2 platforms

#define CPUMINVID 88 //1.55 - 88*0.0125 = 0.450
#define CPUMINVOLTAGE 0.4500

#define CPUMAXVID 18 //1.55 - 18*0.0125 = 1.325; multi 23x, fid 30, did 2, vid 18, pstate0 (highest) normal clocked
#define CPUMAXVOLTAGE 1.3250 //multi 23x, fid 30, did 2, vid 18, pstate0 (highest) normal clocked

#define CPUMAXVIDunderclocked 37 //1.0875V fid:6 did:0 multi:22.00 vid:37
#define CPUMAXVOLTAGEunderclocked 1.0875 //1.55 - 37*0.0125 = 1.0875; fid:6 did:0 multi:22.00 vid:37

#define CPUMINMULTIunderclocked 8 //multi 8x, fid 0, did 2 vid 67, pstate7(lowest) underclocked
#define CPUMAXMULTIunderclocked 22 //1.0875V fid:6 did:0 multi:22.00 vid:37

#define CPUMINVIDunderclocked 67 //multi 8x, fid 0, did 2 vid 67, pstate7(lowest) underclocked
#define CPUMINVOLTAGEunderclocked 0.7125 //1.55 - 67*0.0125 = .7125

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


