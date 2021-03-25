/*
Generate SRT subtitles using Mozilla DeepSpeech.

Copyright (C) 2020  Kaj-Michael Lang <milang@tal.org>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <sndfile.h>

#include <rnnoise.h>

#include <libresample.h>

#include <deepspeech.h>

#define FRAME_SIZE 480

#define BUFFER_SIZE (8192*1024)

#define MODEL "deepspeech-models.pbmm"
#define SCORER "deepspeech-models.scorer"

ModelState *ms;

static void fprint_srt_time(FILE *stream, float seconds)
{
int hours=0,minutes=0;
int whole=floor(seconds);
int fraction=(seconds-whole)*1000.0;
int isec=(int)seconds;

if (seconds>60) {
 hours=(int)floor(seconds/3600.0);
 minutes=(seconds/60.0)-hours;
}
isec=isec % 60;

fprintf(stream, "%02d:%02d:%02d,%03d", hours, minutes, isec, fraction);
}

void print_result(float pos, const CandidateTranscript* ts)
{
int i,r=0;
float spos=pos,epos=pos;
char *buffer;

buffer=malloc(ts->num_tokens);

for (i=0;i<ts->num_tokens;i++) {
 TokenMetadata t=ts->tokens[i];

 if (i==0) {
  spos=t.start_time+pos;
 } else if (i==ts->num_tokens-1) {
  epos=t.start_time+pos;
 }
 r++;
 if (r>48 && *t.text==' ') {
  buffer[i]='\n';
  r=0;
 } else {
  buffer[i]=*t.text;
 }
}
buffer[i]=(char)NULL;

fprint_srt_time(stdout, spos);
fprintf(stdout, " --> ");
fprint_srt_time(stdout, epos);
fprintf(stdout, "\n%s", buffer);
fprintf(stdout, "\n\n");

free(buffer);
}

void process_buffer(float pos, const short* aBuffer, size_t aBufferSize)
{
int j;
Metadata *result = DS_SpeechToTextWithMetadata(ms, aBuffer, aBufferSize, 1);

if (result->num_transcripts==0)
 return;

for (j=0; j < result->num_transcripts; ++j) {
 const CandidateTranscript* ts = &result->transcripts[j];

 print_result(pos, ts);
}

DS_FreeMetadata(result);
}

int main(int argc, char **argv)
{
const char *file_input;
SNDFILE *snd_input=NULL;
SF_INFO info_in;
DenoiseState **st;
uint channels=1,ch;
float *data;
float *ds_data;
int ds_i,dsd_size;
int split=0,splits=0;
int sframes=0,nframes=0;
float cur_sec=0.0, split_sec=0.0, splited_sec=0.0, base_sec=0.0;
float rn[4]={0.0, 0.0}, prn[4]={0.0, 0.0};
float silence=0.003;
int dsb, dss;

double ratio;
void *rsh;

if (argc<2) {
 fprintf(stderr, "Usage: %s input.wav [splitframes [minlength]]\n", argv[0]);
 return 1;
}

file_input=argv[1];

if (argc>2) {
 split=atoi(argv[2]);
} else {
 split=16;
}
fprintf(stderr, "Split on silence frames: %d\n", split);

if (argc>3) {
 split_sec=atof(argv[3]);
} else {
 split_sec=1;
}
fprintf(stderr, "Minimum length: %f seconds\n", split_sec);

memset(&info_in, 0, sizeof(info_in));

if ((snd_input=sf_open(file_input, SFM_READ, &info_in)) == NULL) {
 fprintf(stderr, "Failed to open input file: %s (%s)\n", file_input, sf_strerror (NULL));
 return 1;
}

if (info_in.channels>2) {
 fprintf(stderr, "Only mono or stereo input file supported.\n");
 return 1;
}
channels=info_in.channels;

if (info_in.samplerate!=48000) {
 fprintf(stderr, "Only 48kHz samplerate input file supported.\n");
 return 1;
}

dsb=DS_CreateModel(MODEL, &ms);
if (dsb!=0)
	exit(1);

dss=DS_EnableExternalScorer(ms, SCORER);
if (dss!=0) {
	exit(2);
}

st=malloc(channels * sizeof(DenoiseState *));

for (ch=0;ch<channels;ch++) {
 st[ch]=rnnoise_create(NULL);
}

data=malloc(channels * FRAME_SIZE * sizeof(float));
dsd_size=BUFFER_SIZE * sizeof(float);
ds_data=malloc(dsd_size);

if (!data || !ds_data) {
 fprintf(stderr, "Failed to allocate memory for buffers.\n");
 return 1;
}
fprintf(stderr, "Buffer size: %d\n", dsd_size);

/* Don't normalize to -1.0 - 1.0 */
sf_command(snd_input, SFC_SET_NORM_FLOAT, NULL, SF_FALSE);

ds_i=0;
ratio=16000.0/48000.0;

rsh=resample_open(1, ratio, ratio);

while (1) {
 int r, w, frames=FRAME_SIZE, sc;
 float chs[FRAME_SIZE];
 float chd[FRAME_SIZE];
 float *rsd;

 r=sf_readf_float(snd_input, data, frames);
 if (r==0)
   break;

 cur_sec+=r/48000.0;
 splited_sec+=r/48000.0;

 if (ds_i+FRAME_SIZE> dsd_size) {
  // XXX: Realloc
  fprintf(stderr, "Buffer too small!");
  exit(1);
 }

 for (ch=0;ch<channels;ch++) {
  for (sc=0;sc<FRAME_SIZE;sc++)
   chs[sc]=data[sc*channels+ch];

  // Denoise, rnnoise expects floats
  prn[ch]=rn[ch];
  rn[ch]=rnnoise_process_frame(st[ch], chd, chs);

  // Add to DS buffer and normalize for resample
  for (sc=0;sc<FRAME_SIZE;sc++) {
#if 1
   ds_data[ds_i+sc]=chd[sc]; // denoised
#else
   ds_data[ds_i+sc]=chs[sc]; // original
#endif
  }
  ds_i+=sc;
 }

 if (split>0 && prn[0]<silence && rn[0]<silence) {
  sframes++;
 } else if (split>0 && rn[0]>silence) {
  sframes=0;
  nframes++;
 }

 // fprintf(stderr, "%f/%f: %d/%d/%d/%f %f\n", cur_sec, split_sec, r, w, sframes, prn[0], rn[0]);

 if (split>0 && sframes>split && nframes>0 && splited_sec>split_sec) {
  int sn,inu,fi;
  short *fdata;

  splits++;

  rsd=(float *)malloc(ds_i*sizeof(float));
  if (!rsd)
   exit(1);

#if 1
  sn=resample_process(rsh, ratio, ds_data, ds_i, 1, &inu, rsd, ds_i);
#else
  sn=ds_i/3;
  for (sc=0;sc<sn;sc++) {
   rsd[sc]=ds_data[sc*3];
  }
#endif

  // A convert to 16-bit for DS
  fdata=malloc(sn*sizeof(short));
  for (fi=0;fi<sn;fi++) {
   fdata[fi]=(short)(rsd[fi]);
  }

  printf("%d\n", splits);
  process_buffer(base_sec, fdata, sn);

  free(fdata);
  free(rsd);

  sframes=0;
  nframes=0;
  splited_sec=0.0;
  base_sec=cur_sec;
  ds_i=0;
 }

}

for (ch=0;ch<channels;ch++) {
 rnnoise_destroy(st[ch]);
}

free(data);
free(ds_data);
free(st);

sf_close(snd_input);

DS_FreeModel(ms);

return 0;
}

