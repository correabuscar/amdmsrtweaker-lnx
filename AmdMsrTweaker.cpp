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

#include <assert.h>

using std::cout;
using std::cerr;
using std::endl;


void PrintInfo(const Info& info);

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
    Info info;
    if ((argc > 1)and(0 == strncmp("I wanna brick my system!", argv[1],25))) {//we make sure, because we're about to apply preset voltages!(hardcoded in source code)
      Worker worker(info);

      worker.ParseParams();

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
  for (int i = 0; i < NUMPSTATES; i++) {
    const PStateInfo pi = info.ReadPState(i);

    cout << "  P" << i << ": " << pi.Multi << "x at " << info.DecodeVID(pi.VID) << "V vid:"<< pi.VID << endl;

  }
}

