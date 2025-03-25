| Task | who and what| 
|------|-------------------------------|
| Fix traps such as page faults | |


| File | Can changes be undone? | Clean up debug prints | Clean up code | 
|------|------------------------|-----------------------|---------------|
|sys/src/9/boot/bootfs.proto|	| | |
|sys/src/9/boot/bootrc|	| | |
|sys/src/9/boot/disk.proto|	| | |
|sys/src/9/boot/etheriwl.proto|	| | |
|sys/src/9/l64sipi.s|	| | |
|sys/src/9/pc/apic.c|	| | |
|sys/src/9/pc/fns.h|	| | |
|sys/src/9/pc/io.h|	| | |
|sys/src/9/pc64/DIT|	| | |
|sys/src/9/pc64/acore.c|	| | |
|sys/src/9/pc64/brekky.c|	| | removed! |
|sys/src/9/pc64/dat.h|	| | |
|sys/src/9/pc64/fns.h|	| | |
|sys/src/9/pc64/fpu.c| yes, reverted 	| | |
|sys/src/9/pc64/main.c|	| | |
|sys/src/9/pc64/mkfile|	| | |
|sys/src/9/pc64/mmu.c|	| | |
|sys/src/9/pc64/nix.s|	| | |
|sys/src/9/pc64/squidboy.c|	| | |
|sys/src/9/pc64/struct.c|	| | |
|sys/src/9/pc64/tcore.c|	| | |
|sys/src/9/pc64/trap.c|	| | |
|sys/src/9/port/devcons.c|	| | |
|sys/src/9/port/fault.c|	| | |
|sys/src/9/port/lib.h|	| | |
|sys/src/9/port/portdat.h|	| | |
|sys/src/9/port/portfns.h|	| | |
|sys/src/9/port/proc.c|	| | |
|sys/src/9/port/segment.c|	| | |
|sys/src/9/port/syscallfmt.c|	| | |
|sys/src/9/port/sysproc.c|	| | |
|sys/src/cmd/execac.c|	| | |
|sys/src/libc/9syscall/sys.h|	| | |
