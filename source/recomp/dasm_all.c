/* Disassemble every even address of a 68000 ROM image: "addr len opcode text" per line. */
#include <stdio.h>
#include <stdlib.h>
#include "m68k.h"
static unsigned char *rom; static long sz;
unsigned int m68k_read_disassembler_16(unsigned int a){a&=0xffffff; return a+1<(unsigned)sz?(rom[a]<<8)|rom[a+1]:0;}
unsigned int m68k_read_disassembler_32(unsigned int a){return (m68k_read_disassembler_16(a)<<16)|m68k_read_disassembler_16(a+2);}
unsigned int m68k_read_memory_8(unsigned int a){return 0;} unsigned int m68k_read_memory_16(unsigned int a){return 0;} unsigned int m68k_read_memory_32(unsigned int a){return 0;}
void m68k_write_memory_8(unsigned int a,unsigned int v){} void m68k_write_memory_16(unsigned int a,unsigned int v){} void m68k_write_memory_32(unsigned int a,unsigned int v){}
void your_reset_handler_function(void){} void your_instruction_hook_function(unsigned int pc){}
int main(int c,char**v){FILE*f=fopen(v[1],"rb");fseek(f,0,2);sz=ftell(f);rewind(f);rom=malloc(sz);fread(rom,1,sz,f);fclose(f);
 long end=strtol(v[2],0,16); char buf[256];
 for(long pc=0;pc<end;pc+=2){int n=m68k_disassemble(buf,pc,M68K_CPU_TYPE_68000); printf("%lX %d %04X %s\n",pc,n,m68k_read_disassembler_16(pc),buf);} return 0;}
