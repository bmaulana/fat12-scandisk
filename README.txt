To compile: make

To run scandisk: ./dos_scandisk <imagename> e.g. ./dos_scandisk badfloppy2.img

All files need to be extracted to a single directory (including the image)

A description of the scandisk program is provided in description.pdf

Files contained:

scandisk.c -> contains the scandisk program for a FAT12 DOS file system

bootsect.h, bpb.h, direntry.h, dos.c, dos.h, fat.h -> helper functions for scandisk.c

Makefile -> allows compilation using make

output.txt, output.png -> Sample output of the scandisk program ran on badfloppy2.img