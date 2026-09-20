/* Absolute factory image used only by the SWD provisioning script. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 0x000E0000
}

SECTIONS
{
  .bootloader 0x08000000 :
  {
    KEEP(*(.factory_bootloader))
  } > FLASH

  .application 0x08020000 :
  {
    KEEP(*(.factory_application))
  } > FLASH

  .manifest 0x080DFFE0 :
  {
    KEEP(*(.factory_manifest))
  } > FLASH

  /DISCARD/ :
  {
    *(.ARM.attributes)
    *(.comment)
  }
}
