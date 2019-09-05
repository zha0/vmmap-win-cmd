## vmmap-win-cmd
get process vmmap on windows, just like cat /proc/PID/maps in linux
the src code changed from https://github.com/twpol/vmmap , but I complete the protection str

## usage
> 1. vmmap -pid \<pid>

## install
1. download the release source code, and use vs2017 to compile it into 64bits and 32bits
    + there are vmmap compiled(both 64bits and 32 bits) in the release package

2. if you want to use with wibe(https://github.com/Byzero512/wibe), you need:   
    + rename vmmap.exe(64bits) as vmmap64.exe, and puts both vmmap.exe(32 bits) and vmmap64.exe into PATH of os

## photos

![](https://github.com/Byzero512/vmmap_windows_comandline/raw/master/show.jpg)
