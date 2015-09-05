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

using std::exception;
using std::string;

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


uint64_t Rdmsr(uint32_t index) {
    uint64_t result;

    fprintf(stdout,"!! Rdmsr: %x ... ", index);
    int msr = open("/dev/cpu/0/msr", O_RDONLY);
    if (msr == -1) {
        perror("Failed to open msr device for reading");
        exit(-1);
    }
    pread(msr, &result, sizeof(result), index);
    close(msr);
    fprintf(stdout," done.\n");

    return result;
}

int get_num_cpu() {
    CpuidRegs regs = Cpuid(0x80000008);
    return 1 + (regs.ecx&0xff);
}

void Wrmsr(uint32_t index, const uint64_t& value) {
    char path[255]= "\0";

    for (int i = 0; i < get_num_cpu(); i++) {
        sprintf(path, "/dev/cpu/%d/msr", i);
        //fprintf(stdout,"!! Wrmsr: %s idx:%"PRIu32" val:%"PRIu64"\n", path, index, value);
        fprintf(stdout,"!! Wrmsr: %s idx:%x val:%"PRIu64" ... ", path, index, value);
        int msr = open(path, O_WRONLY);
        if (msr == -1) {
            perror("Failed to open msr device for writing");
            exit(-1);
        }
        if(pwrite(msr, &value, sizeof(value), index) != sizeof(value)) {
            perror("Failed to write to msr device");
        }
        close(msr);
        fprintf(stdout," done.\n");
    }
}


CpuidRegs Cpuid(uint32_t index) {
    CpuidRegs result;

    fprintf(stdout,"!! cpuid: /dev/cpu/0/cpuid %x ... ", index);
    FILE* cpuid = fopen("/dev/cpu/0/cpuid", "r");
    if (cpuid == NULL) {
        perror("Failed to open cpuid device for reading");
        exit(-1);
    }
    fseek(cpuid, index, SEEK_SET);
    fread(&(result.eax), sizeof(result.eax), 1, cpuid);
    fread(&(result.ebx), sizeof(result.ebx), 1, cpuid);
    fread(&(result.ecx), sizeof(result.ecx), 1, cpuid);
    fread(&(result.edx), sizeof(result.edx), 1, cpuid);
    fclose(cpuid);
    fprintf(stdout," done.\n");

    return result;
}

