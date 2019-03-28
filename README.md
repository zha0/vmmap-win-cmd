# vmmap_windows_comandline
get process vmmap on windows.

> 1. VMMAP -pid <pid>: just show image, heap, stack
> 2. VMMAP -all -pid <pid>: show private maped zone
> 3. VMMAP -most -pid <pid>: show all information that about process maps

> i don know why the access right is rwx to many pages. maybe it is error.
> the src code changed from https://github.com/twpol/vmmap 

![](https://github.com/Byzero512/vmmap_windows_comandline/raw/master/show.png)
