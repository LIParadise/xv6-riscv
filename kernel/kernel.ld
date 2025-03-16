OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  /* set VMA (and LMA since no `AT`) for qemu */
  . = 0x80000000;

  .text : {
   /*
    * ensure that entry.S / _entry is at 0x80000000,
    * where qemu's -kernel jumps:
    * this line helps `Makefile` to be order-agnostic
    */
    kernel/entry.o(.text)
    *(.text .text.*)
    . = ALIGN(0x1000);
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    PROVIDE(etext = .);
  }

  .rela.dyn : {
    PROVIDE(rela_dyn_start = .);
  }

  .rodata : {
    PROVIDE(rela_dyn_end = .);
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
    . = ALIGN(16);
  }
  
  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  PROVIDE(kernel_end_marked_by_ld = .);
}
