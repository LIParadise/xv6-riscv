OUTPUT_ARCH( "riscv" )

SECTIONS
{
 . = 0x0;
 
  .text : {
    *(.text .text.*)
  }

  .rodata : {

    /*
     * Define an extern number if C code doesn't provide definition
     *
     * Note that this somewhat resembles pointer addresses,
     * in that directly saying `PROVIDE(yruear = 42069);` is actually saying that the symbol lives at address `42069`,
     * which is probably not what you want:
     * you end up trying to read a number from an address that you probably don't have correct permission.
     */
    . = ALIGN(16);
    PROVIDE(yruear = .);
    LONG(42069)

    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .eh_frame : {
       *(.eh_frame)
       *(.eh_frame.*)
   }

  . = ALIGN(0x1000);
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

  PROVIDE(end = .);

}
