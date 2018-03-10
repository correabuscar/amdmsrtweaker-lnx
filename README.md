amdmsrtweaker-lnx
=======

amdmsrt (amdmsrtweaker-lnx) is a Linux port of the AmdMsrTweaker tool written by Martin Kinkelin and extended by Marcus Pollice.

=======
The current version that you're looking at is modified by xftroxgpx and is meant to run only on my own laptop!
Please do not use this one but instead, look at the original repo. which is meant to work generally: https://github.com/johkra/amdmsrtweaker-lnx  



Changes to frequency will not be reflected by `sudo cat /sys/devices/system/cpu/cpufreq/policy*/cpuinfo_cur_freq`, but a quick benchmark such as "openssl speed sha1" should show a speed difference.  

Changes to frequency will be reflected by:  
`cat /sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq`  
`cat /proc/cpuinfo`  
(tested kernel 4.16.0-rc4-g661e50bc8532, but forgot which kernel introduced it!)  

Compilation
-----------

Clone the repo and make sure you have gcc and make installed. Then execute "make" in the cloned directory. Optionally copy the file "amdmsrt" to a directory in $PATH such as /usr/bin or /usr/local/bin.

Usage
-----

Make sure you have the `msr` module loaded (cpuid not required). ("modprobe msr" as root) The program will otherwise exit with a corresponding error message.

See the file "readme.txt" for usage examples. The program has to executed as root and the executable is called "amdmsrt".


License
-------

This was originally under GNU GPL (see oldLICENSE file), however my modifications/additions etc. are under UNLICENSE and/or CC0 (ie. public domain)


