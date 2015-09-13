/*
 * Copyright (c) Martin Kinkelin
 *
 * See the "License.txt" file in the root directory for infos
 * about permitted and prohibited uses of this code.
 */

#include <cstdio>
#include <cstdlib>

#include <fcntl.h>
#include <unistd.h>

#include "WinRing0.h"
#include "StringUtils.h"

#include <inttypes.h>

#include <assert.h>

//#include <include/linux/smp.h>

using std::exception;
using std::string;

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

/*
uint32_t ReadPciConfig(uint32_t device, uint32_t function, uint32_t regAddress) {
    uint32_t result;
    char path[255]= "\0";
    sprintf(path, "/proc/bus/pci/00/%x.%x", device, function);
    fprintf(stdout,"!! Reading: %s ... ", path);

    int pci = open(path, O_RDONLY);
    if (pci == -1) {
        perror("Failed to open pci device for reading");
        exit(-1);
    }
    pread(pci, &result, sizeof(result), regAddress);
    close(pci);
    fprintf(stdout," done.\n");

    return result;
}

void WritePciConfig(uint32_t device, uint32_t function, uint32_t regAddress, uint32_t value) {
    char path[255]= "\0";
    sprintf(path, "/proc/bus/pci/00/%x.%x", device, function);
    fprintf(stdout,"!! Writing: %s dev:%x func:%x regAddr:%x val:%x... ", path, device, function, regAddress, value);

    int pci = open(path, O_WRONLY);
    if (pci == -1) {
        perror("Failed to open pci device for writing");
        exit(-1);
    }
    if(pwrite(pci, &value, sizeof(value), regAddress) != sizeof(value)) {
        perror("Failed to write to pci device");
    }
    close(pci);
    fprintf(stdout," done.\n");
}
*/

int inline get_num_cpu() {
//    CpuidRegs regs = Cpuid(0x80000008);
//    return 1 + (regs.ecx&0xff);
    return 4;//assume 4 cores - my CPU
}

uint64_t Rdmsr(uint32_t index) {
    uint64_t result[4]={0,0,0,0};
    char path[255]= "\0";

    for (int i = 0; i < get_num_cpu(); i++) {
      int ret=sprintf(path, "/dev/cpu/%d/msr", i);
      if (ret<0) {
        pERR("snprintf failed");
        fprintf(stderr,"!! snprintf ret=%d\n",ret);
        exit(-1);
      }

      fprintf(stdout, startYELLOWcolortext "  !! Rdmsr: %s idx:%x ... %lu bytes ... ", path, index, sizeof(result[i]));
      int msr = open(path, O_RDONLY);
      if (msr == -1) {
        pERR("Failed to open msr device for reading. You need: # modprobe msr");
        exit(-1);
      }
      if (sizeof(result[i]) != pread(msr, &(result[i]), sizeof(result[i]), index)) {//read 8 bytes
        pERR("Failed to read from msr device");
      }
      close(msr);
      fprintf(stdout," done. (result==%"PRIu64")" endcolor "\n", result[i]);
      if (i>0) {
        if (result[i-1] != result[i]) {
          pERR("Rdmsr: different results for cores");
          fprintf(stderr,"!! core[%d]==%"PRIu64" != core[%d]==%"PRIu64"\n", i-1, result[i-1], i, result[i]);
        }
      }
    }

    return result[0];
}

void Wrmsr(uint32_t index, const uint64_t& value) {
    char path[255]= "\0";

    for (int i = 0; i < get_num_cpu(); i++) {
        int ret=sprintf(path, "/dev/cpu/%d/msr", i);
        if (ret<0) {
          pERR("snprintf failed");
          exit(-1);
        }
        //fprintf(stdout,"!! Wrmsr: %s idx:%"PRIu32" val:%"PRIu64"\n", path, index, value);
        fprintf(stdout, startPURPLEcolortext "  !! Wrmsr: %s idx:%x val:%"PRIu64" ... ", path, index, value);
        int msr = open(path, O_WRONLY);
        if (msr == -1) {
            pERR("Failed to open msr device for writing");
            exit(-1);
        }
        if(pwrite(msr, &value, sizeof(value), index) != sizeof(value)) {
            pERR("Failed to write to msr device");
        }
        close(msr);
        fprintf(stdout," done." endcolor "\n");
    }
}


/*
CpuidRegs Cpuid(uint32_t index) {
    CpuidRegs result;

    fprintf(stdout,"!! cpuid: /dev/cpu/0/cpuid %x ... ", index);
    FILE* cpuid = fopen("/dev/cpu/0/cpuid", "r");
    if (cpuid == NULL) {
        perror("Failed to open cpuid device for reading");
        exit(-1);
    }
    // _IOFBF Full buffering: On output, data is written once the buffer is full (or flushed). On Input, the buffer is filled when an input operation is requested and the buffer is empty.
    // _IOLBF Line buffering: On output, data is written when a newline character is inserted into the stream or when the buffer is full (or flushed), whatever happens first. On Input, the buffer is filled up to the next newline character when an input operation is requested and the buffer is empty.
    // _IONBF No buffering: No buffer is used. Each I/O operation is written as soon as possible. In this case, the buffer and size parameters are ignored.
    setvbuf(cpuid, NULL, _IOFBF, 16);//see kernel's: ./arch/x86/kernel/cpuid.c
    //http://www.cplusplus.com/reference/cstdio/setvbuf/
    fseek(cpuid, index, SEEK_SET);
//    result.eax=0xFFFFFFFF;
//    fprintf(stderr, "!! sizeof = %lu eax=%x\n", sizeof(result.eax), result.eax); //4, I knew it! so, how come this works:
    fread(&(result.eax), sizeof(result.eax), 1, cpuid);//shouldn't it fail since it's not 16 bytes?(4*1=4)
    //count is 1, so if (count % 16) return -EINVAL ... well? see kernel's: ./arch/x86/kernel/cpuid.c
    //it doesn't fail because the buffer is 8192 (if I remember correctly; that's 512*16)
//    fprintf(stderr, "!! after fread, eax=%x\n", result.eax);
    fread(&(result.ebx), sizeof(result.ebx), 1, cpuid);
    fread(&(result.ecx), sizeof(result.ecx), 1, cpuid);
    fread(&(result.edx), sizeof(result.edx), 1, cpuid);
    fclose(cpuid);
    fprintf(stdout," done.\n");

//    int cpu=0;
//    struct cpuid_regs {
//        u32 eax, ebx, ecx, edx;
//    };    

//    smp_call_function_single(cpu, cpuid_smp_cpuid, &cmd, 1);  can't :/

    return result;
}
*/

