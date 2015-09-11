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


/*static void SplitPair(string& left, string& right, const string& str, char delimiter) {
    const size_t i = str.find(delimiter);

    left = str.substr(0, i);

    if (i == string::npos)
        right.clear();
    else
        right = str.substr(i + 1);

}*/


bool Worker::ParseParams() {//int argc, const char* argv[]) {
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
//    psi.NBPState = -1;

//    NBPStateInfo nbpsi;
//    nbpsi.Multi = nbpsi.VID = -1.0;

//    fprintf(stdout,"Parsing command line as:\n");
    fprintf(stdout,"Hardcoded values:\n");
    for (int i = 0; i < info.NumPStates; i++) {
        _pStates.push_back(psi);
//        _pStates.back().Index = i;//very important!
        _pStates[i].Index = i;//^ equivalent
        _pStates[i].Multi = allpsi[i].multi;//eg. 22.0
        _pStates[i].VID = info.EncodeVID(allpsi[i].strvid /*eg. 1.0875*/);//atof(vid.c_str()));
        assert( allpsi[i].VID/*eg. 37*/ == _pStates[i].VID);
        assert( i == _pStates[i].Index );
//        assert(false);
    }
//    for (int i = 0; i < info.NumNBPStates; i++) {
//        _nbPStates.push_back(nbpsi);
//        _nbPStates.back().Index = i;
//    }

    for (int i = 0; i < info.NumPStates; i++) {
            fprintf(stdout,"pstate:%d multi:%02.2f vid:%d\n",// voltage:%d\n", 
                i, 
                _pStates[i].Multi,
                _pStates[i].VID
                //,DecodeVID(_pStates[i].VID
                  );
    }

/*    for (int i = 1; i < argc; i++) {
        const string param(argv[i]);

        string key, value;
        SplitPair(key, value, param, '=');

        if (value.empty()) {
            if (param.length() >= 2 && tolower(param[0]) == 'p') {
                const int index = atoi(param.c_str() + 1);
                if (index >= 0 && index < info.NumPStates) {
                    _pState = index;
            fprintf(stdout,"pstate:%d\n",_pState);
                    continue;
                }
            }
        } else {
            if (key.length() >= 2 && tolower(key[0]) == 'p') {
                const int index = atoi(key.c_str() + 1);
                if (index >= 0 && index < info.NumPStates) {
                    string multi, vid;
                    SplitPair(multi, vid, value, '@');

                    if (!multi.empty())
                        _pStates[index].Multi = info.multiScaleFactor * atof(multi.c_str());
                    if (!vid.empty())
                        _pStates[index].VID = info.EncodeVID(atof(vid.c_str()));

            fprintf(stdout,"pstate:%d multi:%02.2f(%s) vid:%d(%s)\n", 
                index, 
                _pStates[index].Multi, multi.c_str(),
                _pStates[index].VID, vid.c_str());
                    continue;
                }
            }

            if (key.length() >= 5 && strncasecmp(key.c_str(), "NB_P", 4) == 0) {
                const int index = atoi(key.c_str() + 4);
                if (index >= 0 && index < info.NumNBPStates) {
                    string multi, vid;
                    SplitPair(multi, vid, value, '@');

                    if (!multi.empty())
                        _nbPStates[index].Multi = atof(multi.c_str());
                    if (!vid.empty())
                        _nbPStates[index].VID = info.EncodeVID(atof(vid.c_str()));

                    continue;
                }
            }

            if (strcasecmp(key.c_str(), "NB_low") == 0) {
                const int index = atoi(value.c_str());

                int j = 0;
                for (; j < min(index, info.NumPStates); j++)
                    _pStates[j].NBPState = 0;
                for (; j < info.NumPStates; j++)
                    _pStates[j].NBPState = 1;

                continue;
            }*/

/*            if (strcasecmp(key.c_str(), "Turbo") == 0) {
                const int flag = atoi(value.c_str());
                if (flag == 0 || flag == 1) {
                    _turbo = flag;
                    continue;
                }
            }

            if (strcasecmp(key.c_str(), "APM") == 0) {
                const int flag = atoi(value.c_str());
                if (flag == 0 || flag == 1) {
                    _apm = flag;
                    continue;
                }
            }
        }

        cerr << "ERROR: invalid parameter " << param.c_str() << endl;
        return false;
    }*/

    return true;
//    return false;
}


//static bool ContainsChanges(const PStateInfo& info) {
//    return (info.Multi >= 0 || info.VID >= 0 );//|| info.NBVID >= 0 || info.NBPState >= 0);
//}
//static bool ContainsChanges(const NBPStateInfo& info) {
//    return (info.Multi >= 0 || info.VID >= 0);
//}

void Worker::ApplyChanges() {
    const Info& info = *_info;

    //north bridge stuff:
/*    if (info.Family == 0x15) {
        for (size_t i = 0; i < _nbPStates.size(); i++) {
            const NBPStateInfo& nbpsi = _nbPStates[i];
            if (ContainsChanges(nbpsi))
                info.WriteNBPState(nbpsi);
        }
    } else if (info.Family == 0x10 && (_nbPStates[0].VID >= 0 || _nbPStates[1].VID >= 0)) {
        for (size_t i = 0; i < _pStates.size(); i++) {
            PStateInfo& psi = _pStates[i];

            const int nbPState = (psi.NBPState >= 0 ? psi.NBPState :
                                  info.ReadPState(i).NBPState);
            const NBPStateInfo& nbpsi = _nbPStates[nbPState];

            if (nbpsi.VID >= 0)
                psi.NBVID = nbpsi.VID;
        }
    }*/

    //turbo stuff: to enable or disable turbo set _turbo to 1 or 0 (default: -1, to not touch!)
//    if (_turbo >= 0 && info.IsBoostSupported)
//        info.SetBoostSource(_turbo == 1);
//    if (_apm >= 0 && info.Family == 0x15)
//        info.SetAPM(_apm == 1);

    //pstates stuff:
    for (size_t i = 0; i < _pStates.size(); i++) {
//        const PStateInfo& psi = _pStates[i];
//        if (ContainsChanges(psi))
            info.WritePState(_pStates[i]);
    }

//    if (_turbo >= 0 && info.IsBoostSupported)
//        info.SetCPBDis(_turbo == 1);

    const int currentPState = info.GetCurrentPState();
/*    const int newPState = (_pState >= 0 ? _pState : currentPState);

    if (newPState != currentPState)
        info.SetCurrentPState(newPState);
    else*/
    //{

    //we switch to another pstate temporarily, then back again so that it takes effect (apparently that's why, unsure, it's not my coding)
//        if (ContainsChanges(_pStates[currentPState])) {
    const int lastpstate=info.NumPStates - 1;//aka the lowest speed one
            const int tempPState = (currentPState == lastpstate ? 0 : lastpstate);
          fprintf(stdout,"!! currentpstate:%d temppstate:%d\n", currentPState, tempPState);
            info.SetCurrentPState(tempPState);
            sleep(1);//1 second
            info.SetCurrentPState(currentPState);
//        }
//    }
}
