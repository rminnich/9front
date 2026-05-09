# Milestones to come

- [ ] we can drop `/sys/src/9/riscv64` into 9front, build it, and it boots
  

# TODO

- [ ] upstream changes to `/sys/include/{a.out.h,mach.h}` and `/sys/src/libmach` so
  that `/sys/src/9/riscv64` is all we need here
- [ ] lazy fpu support
- [ ] make sure the newest interrupt controller works
- [ ] usb support
- [ ] bring back ethervirtio10
- [ ] real ethernet
- [ ] graphics
- [ ] support more hardware
  

# Doing

- [ ] boot whole OS, not just the kernel
- [ ] better docs

# Done

- [x] the kernel boots on qemu
- [x] the kernel boots on BPI-F3

