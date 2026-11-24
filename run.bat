"C:\Program Files\qemu\qemu-system-x86_64.exe" ^
  -drive if=pflash,format=raw,readonly=on,file="C:\Program Files\qemu\share\edk2-x86_64-code.fd" ^
  -drive if=pflash,format=raw,file="OVMF_VARS.fd" ^
  -drive file="fat:rw:sysroot",format=raw ^
  -cpu Skylake-Client-v4,+x2apic ^
  -smp 4 ^
  -m 1G ^
  -no-shutdown -no-reboot -monitor stdio -d int > qemu.log 2>&1