#pragma once


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

