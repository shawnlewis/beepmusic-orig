OUTPUT_FORMAT("elf32-tradbigmips", "elf32-tradbigmips", "elf32-tradbigmips")
OUTPUT_ARCH(mips)
START_ADDR = 0xa0640000;
ENTRY(START_ADDR)
SECTIONS
{
  . = START_ADDR;

  . = ALIGN(4);
  .text           :
  {
    *(.text)
  }

  . = ALIGN(4);
  .rodata         : { *(.rodata .rodata.* .gnu.linkonce.r.*) }

  .data           :
  {
    _fdata = . ;
    *(.data .data.* .gnu.linkonce.d.*)
    SORT(CONSTRUCTORS)
  }

  .sdata          :
  {
    *(.sdata .sdata.* .gnu.linkonce.s.*)
  }

  . = ALIGN(4);
  .sbss           :
  {
    *(.dynsbss)
    *(.sbss .sbss.* .gnu.linkonce.sb.*)
    *(.scommon)
  }
  .bss            :
  {
   *(.dynbss)
   *(.bss .bss.* .gnu.linkonce.b.*)
   *(COMMON)
   . = ALIGN(. != 0 ? 32 / 8 : 1);
  }

  /DISCARD/ : { *(*) }
}
