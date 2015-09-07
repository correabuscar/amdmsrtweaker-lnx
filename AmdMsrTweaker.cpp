/*
 * Copyright (c) Martin Kinkelin
 *
 * See the "License.txt" file in the root directory for infos
 * about permitted and prohibited uses of this code.
 */

#include <cstdio>
#include <iostream>
#include "Info.h"
#include "Worker.h"
#include "WinRing0.h"

#include <string.h>

using std::cout;
using std::cerr;
using std::endl;


void PrintInfo(const Info& info);

const int count=1+8;
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
};

/// <summary>Entry point for the program.</summary>
int main(int argc, const char* argv[]) {
    cout << endl;
    cout << "AmdMsrTweaker v1.1 modified for my own Lenovo Z575 ONLY!!! (voltages are fixed, params ignored!)" << endl;
    cout << endl;
    try {
        Info info;
        if (!info.Initialize()) {
            cout << "ERROR: unsupported CPU" << endl;
            return 2;
        }


    cout << ".:. General" << endl << "---" << endl;
    cout << "  AMD family 0x" << std::hex << info.Family << std::dec << " (" << info.Family << " dec)" << std::hex << ", model 0x" << info.Model << std::dec << " CPU, " << info.NumCores << " cores" << endl;
    cout << "  Default reference clock: " << info.multiScaleFactor * 100 << " MHz" << endl;
    cout << "  Available multipliers: " << (info.MinMulti / info.multiScaleFactor) << " .. " << (info.MaxSoftwareMulti / info.multiScaleFactor) << endl;
    cout << "  Available voltage IDs: " << info.MinVID << " .. " << info.MaxVID << " (" << info.VIDStep << " steps)" << endl;
    cout << endl;

        if ((argc > 1)and(0 == strncmp("I wanna brick my system!", argv[1],25))) {//we make sure, because we're about to apply preset(in source code) voltages!!
            Worker worker(info);

            if (!worker.ParseParams(count, params)) {
//            if (!worker.ParseParams(argc, argv)) 
                return 3;
            }

            fprintf(stdout,"Before:\n");
            PrintInfo(info);
            worker.ApplyChanges();
            fprintf(stdout,"After:\n");
            PrintInfo(info);
        } else {
            PrintInfo(info);
        }
    } catch (const std::exception& e) {
        cerr << "ERROR: " << e.what() << endl;
        return 10;
    }

    return 0;
}


void PrintInfo(const Info& info) {

    cout << ".:. Turbo" << endl << "---" << endl;
    if (!info.IsBoostSupported)
        cout << "  not supported" << endl;
    else {
        cout << "  " << (info.IsBoostEnabled ? "enabled" : "disabled") << endl;
        cout << "  " << (info.IsBoostLocked ? "locked" : "unlocked") << endl;

        if (info.MaxMulti != info.MaxSoftwareMulti)
            cout << "  Max multiplier: " << (info.MaxMulti / info.multiScaleFactor) << endl;
    }
    cout << endl;

    cout << ".:. P-states" << endl << "---" << endl;
    cout << "  " << info.NumPStates << " of " << (info.Family == 0x10 ? 5 : 8) << " enabled (P0 .. P" << (info.NumPStates - 1) << ")" << endl;

    if (info.IsBoostSupported && info.NumBoostStates > 0) {
        cout << "  Turbo P-states:";
        for (int i = 0; i < info.NumBoostStates; i++)
            cout << " P" << i;
        cout << endl;
    }

    cout << "  ---" << endl;

    for (int i = 0; i < info.NumPStates; i++) {
        const PStateInfo pi = info.ReadPState(i);

        cout << "  P" << i << ": " << (pi.Multi / info.multiScaleFactor) << "x at " << info.DecodeVID(pi.VID) << "V vid:"<< pi.VID << endl;

/*        if (pi.NBPState >= 0) {
            cout << "      NorthBridge in NB_P" << pi.NBPState;
            if (pi.NBVID >= 0)
                cout << " at " << info.DecodeVID(pi.NBVID) << "V";
            cout << endl;
        }*/
    }

/*    if (info.Family == 0x15) {
        cout << "  ---" << endl;

        for (int i = 0; i < info.NumNBPStates; i++) {
            const NBPStateInfo pi = info.ReadNBPState(i);
            cout << "  NB_P" << i << ": " << pi.Multi << "x at " << info.DecodeVID(pi.VID) << "V" << endl;
        }
    }*/
}
