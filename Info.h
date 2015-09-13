/*
 * Copyright (c) Martin Kinkelin
 *
 * See the "License.txt" file in the root directory for infos
 * about permitted and prohibited uses of this code.
 */

#pragma once


struct PStateInfo {
    int Index;    // hardware index
    double Multi; // internal one for 100 MHz reference
    int VID;
};

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

class Info {
public:

    Info() {}

    PStateInfo ReadPState(int index) const;
    bool WritePState(const PStateInfo& info) const;

    int GetCurrentPState() const;
    void SetCurrentPState(int index) const;

    double DecodeVID(const int vid) const;//won't work as inline due to being used in constructor setting fields above; ok, that's not it, it's because it's being used in another .cpp file apparently!
    int EncodeVID(double vid) const;//can't inline due to being used in another .cpp file 

private:

    inline double DecodeMulti(const int fid, const int did) const;
    inline void EncodeMulti(double multi, int& fid, int& did) const;

};

