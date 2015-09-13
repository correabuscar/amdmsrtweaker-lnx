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
//    int NBPState;
//    int NBVID; // family 0x10 only
};

//struct NBPStateInfo {
//    int Index;
//    double Multi; // for 200 MHz reference
//    int VID;
//};

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

//    int Family;
//    int Model;
//    int NumCores;
//    int NumPStates;
//    int NumNBPStates;

//    double MinMulti, MaxMulti; // internal ones for 100 MHz reference
//    double MaxSoftwareMulti; // for software (i.e., non-boost) P-states
//    double MinVID, MaxVID;
//    double VIDStep;
//    double multiScaleFactor;

//    bool IsBoostSupported;
//    bool IsBoostEnabled;
//    bool IsBoostLocked;
//    int NumBoostStates;


    Info() //:
//        : Family(0x12) //my CPU
//        , Model(0x1)
//        , NumCores(4)
//        , NumPStates(NUMPSTATES)
//        , NumNBPStates(2) //except family 0x15, we have at least 2 NB P-States
//        MinMulti(1.0),
//        MaxMulti(40.0)//24+16
//        , MaxSoftwareMulti(0.0)
//         MinVID(DecodeVID(88))
//        , MaxVID(DecodeVID(18))//not an error, it's less! that is: 124-18=106
//        , VIDStep(0.0125) //default step for pre SVI2 platforms
//        , multiScaleFactor(1.0) //default for 100MHz REFCLK
//        , IsBoostSupported(false)
//        , IsBoostEnabled(false)
//        , IsBoostLocked(false)
//        , NumBoostStates(0)
        {}

//    bool Initialize();

    PStateInfo ReadPState(int index) const;
    bool WritePState(const PStateInfo& info) const;

//    NBPStateInfo ReadNBPState(int index) const;
//    void WriteNBPState(const NBPStateInfo& info) const;

//    void SetCPBDis(bool enabled) const;
//    void SetBoostSource(bool enabled) const;
//    void SetAPM(bool enabled) const;

    int GetCurrentPState() const;
    void SetCurrentPState(int index) const;

    double DecodeVID(const int vid) const;//won't work as inline due to being used in constructor setting fields above; ok, that's not it, it's because it's being used in another .cpp file apparently!
    int EncodeVID(double vid) const;//can't inline due to being used in another .cpp file 

private:

    inline double DecodeMulti(const int fid, const int did) const;
    inline void EncodeMulti(double multi, int& fid, int& did) const;

};

