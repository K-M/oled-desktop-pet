// sam_render.c — host-side SAM renderer used by render_pet_me.sh.
// Runs SAM exactly as ESP8266SAM::Say() does, writing unsigned 8-bit samples @ 22050 Hz.
// Host renderer: runs SAM exactly as ESP8266SAM::Say does and writes unsigned 8-bit @ 22050 Hz.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sam.h"
#include "reciter.h"
#include "SamData.h"
SamData *samdata;
static FILE *out; static long n;
static void cb(void *d, unsigned char b){ (void)d; fputc(b,out); n++; }
int main(int argc,char**argv){
  // usage: sam_render "text" speed pitch throat mouth out.raw [p]   (p = phonetic input)
  samdata = calloc(1,sizeof(SamData));
  out = fopen(argv[6],"wb");
  EnableSingmode(0);
  SetSpeed(atoi(argv[2])); SetPitch(atoi(argv[3])); SetThroat(atoi(argv[4])); SetMouth(atoi(argv[5]));
  char input[256]; int i;
  for(i=0; argv[1][i]; i++) input[i]=toupper((unsigned char)argv[1][i]); input[i]=0;
  if(argc>7 && argv[7][0]=='p'){ strncat(input,"\x9b",255); }        /* phonetic input, as ESP8266SAM SetPhonetic(true) */
  else { strncat(input,"[",255); if(!TextToPhonemes(input)){ fprintf(stderr,"phoneme error\n"); return 1; } }
  SetInput(input);
  SAMMain(cb,0);
  fclose(out); fprintf(stderr,"%ld samples (%.3f s)\n",n,n/22050.0);
  return 0;
}
