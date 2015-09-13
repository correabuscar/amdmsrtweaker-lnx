/*
 * Copyright (c) Martin Kinkelin
 *
 * See the "License.txt" file in the root directory for infos
 * about permitted and prohibited uses of this code.
 */

#include <algorithm>
#include <iostream>
#include <locale>

#include <unistd.h>
#include <stdio.h>

#include "Worker.h"
#include "StringUtils.h"
#include "WinRing0.h"

#include <assert.h>

using std::cerr;
using std::endl;
using std::min;
using std::max;
using std::string;
using std::tolower;
using std::vector;


void Worker::ParseParams() {
    const Info& info = *_info;

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
        _pStates[i].VID = info.EncodeVID(allpsi[i].strvid /*eg. 1.0875*/);//atof(vid.c_str()));
        assert( allpsi[i].VID/*eg. 37*/ == _pStates[i].VID);
        assert( i == _pStates[i].Index );
            fprintf(stdout,"pstate:%d multi:%02.2f vid:%d\n",// voltage:%d\n", 
                i, 
                _pStates[i].Multi,
                _pStates[i].VID
                  );
    }
}


void Worker::ApplyChanges() {
    const Info& info = *_info;

    //pstates stuff:
    bool modded=false;
    for (size_t i = 0; i < _pStates.size(); i++) {
      modded=info.WritePState(_pStates[i]) | modded;
    }

    if (modded) {
      fprintf(stdout, "Switching to another p-state temporarily so to ensure current one uses newly applied values\n");

    const int currentPState = info.GetCurrentPState();

    //we switch to another pstate temporarily, then back again so that it takes effect (apparently that's why, unsure, it's not my coding)
//        if (ContainsChanges(_pStates[currentPState])) {
    const int lastpstate= NUMPSTATES - 1;//aka the lowest speed one
            const int tempPState = (currentPState == lastpstate ? 0 : lastpstate);
          fprintf(stdout,"!! currentpstate:%d temppstate:%d\n", currentPState, tempPState);
            info.SetCurrentPState(tempPState);
            sleep(1);//1 second
            info.SetCurrentPState(currentPState);
    }
}

