#pragma once

#include <inttypes.h> //for uint32_t uint64_t

struct PStateInfo {
    double multi; //multiplier ( multiply with the reference clock of 100Mhz eg. multi*REFERENCECLOCK)
    double strvid; //real life voltage eg. 1.325V as a double
    int VID; //vid, eg. 18 (for 1.325V) or 67 (for 1.0875V)
    // compute this VID like this:
    // int vid= (int)(V155 / CPUVIDSTEP) - r;
    // where, int r = (int)(wanted_voltage / CPUVIDSTEP + 0.5);
    // CPUVIDSTEP is 0.0125 volts
    // V155 is 1.55 (as seen in the defs below)
    // so (int)(V155 / CPUVIDSTEP) is == 124
    // example: if wanted_voltage == 1.0875 volts then
    // 124 - 87(for 1.0875 aka 22x multi) = 37 , so VID here is 37
    // XXX: short version: VID = 124 - (int)(wanted_voltage / 0.0125 + 0.5)
    // if you're using 'bc' aka /usr/bin/bc is owned by bc 1.07.1-1 
    // then paste this on cmdline:
    // volts=1.0875; echo -n "VID="; echo "define trunc(x) { auto s; s=scale; scale=0; x=x/1; scale=s; return x } 124 - trunc($volts / 0.0125 + 0.5)" | bc -l
    // set your volts= to the voltage that you want converted to VID
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
  //to see how to compute VID (the last value, that is) seek to the beginning of this file! shift+3 on this word: VID  (in vim) and press 'n' one more time
  {30.0, 1.3250, 18}, //P0, boost
  {30.0, 1.3250, 18}, //P1, //96degC and seg fault during kernel build! - DON'T do this! might work with cpuvary! #added3  sort of untested in linux - unsure if it(boost) ever activated!
//  {22.0, 1.0875, 37}, //P0, boost
  {29.0, 1.2875, 21}, //untested in linux but it's 2nd step from the preliminary fail (see unde.txt)
  {28.0, 1.2625, 23}, //potentially expect auto-throttling to P7(or is it P2? had P2->P0 set to same 800Mhz during tests!) (or does it happen only in win7+k10stat?) for this and any above 2800Mhz! untested in linux
  {27.0, 1.2250, 26}, //no throttling for this and below 2700Mhz. untested in linux!
  {26.0, 1.1875, 29}, //untested in linux
  {25.0, 1.1625, 31}, //untested in linux
//  {24.0, 1.1375, 33} //untested in linux, yeah it's too high when compiling to have this be P7!
//  {24.0, 1.1500, 32}, //P0, normal max #ADDED now for testing (19 feb 2017) yeah this is stable, 08 march 2018!
//  {22.0, 1.0875, 37}, //P0, normal max #ADDED now for testing
//  {20.0, 1.0250, 42}, //P1, normal
//  {18.0, 0.9625, 47},
//  {17.0, 0.9375, 49},
////  {16.0, 0.9, 52}, //temp removed for the 3ghz P1 #removed3
  {14.0, 0.8625, 55}, //#remove2 for testing 3ghz tops
////  {12.0, 0.8125, 59}, #REMOVED now for testing (see above added one)
//  {8.0, 0.7125, 67} //P7, normal
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


