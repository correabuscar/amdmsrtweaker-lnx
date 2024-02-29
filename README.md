amdmsrtweaker-lnx
======

amdmsrt (amdmsrtweaker-lnx) is a Linux port of the AmdMsrTweaker tool written by Martin Kinkelin and extended by Marcus Pollice.

======
The current version that you're looking at is modified by CExftNSroxORgpxED(prev.ver.of me(deleted) that I didn't wanna mention, but too late now) and is meant to run only on my own laptop!  
Please do not use this one but instead, look at the original repo. which is meant to work generally: https://github.com/johkra/amdmsrtweaker-lnx  



Changes to frequency will not be reflected by `sudo cat /sys/devices/system/cpu/cpufreq/policy*/cpuinfo_cur_freq`, but a quick benchmark such as "openssl speed sha1" should show a speed difference.  

Changes to frequency will be reflected by:  
`cat /sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq`  
`cat /proc/cpuinfo`  
(tested kernel 4.16.0-rc4-g661e50bc8532, but forgot which kernel introduced it!)  
To get accurate Khz results, try running `openssl speed sha512` first.  
eg. idle:  
  
```bash
$ cat /sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq
2527605
2570716
2444120
2612325
```
  
and with 1 core in use (by `openssl speed sha512` in another terminal):  
```bash
$ cat /sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq
2994707
3002469
3000750
2999555
```
  
or in MHz, continously:  
```bash
watch -d -n0.5 \( cat /proc/cpuinfo\|grep MHz \)
```

Compilation
-----------

Clone the repo and make sure you have gcc and make installed. Then execute "make" in the cloned directory. Optionally copy the file "amdmsrt" to a directory in $PATH such as /usr/bin or /usr/local/bin.

Usage
-----

Make sure you have the `msr` module loaded (cpuid not required). ("modprobe msr" as root) The program will otherwise exit with a corresponding error message.

See the file "readme.txt" for usage examples. The program has to executed as root and the executable is called "amdmsrt".


License
-------

This was originally under GNU GPL (see oldLICENSE file), however my modifications/additions etc. are under UNLICENSE and/or CC0 (ie. public domain) and/or GNU GPL and/or MIT and/or Apache v2.  


