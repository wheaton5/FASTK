/*********************************************************************************************\
 *
 *  Code to produce histograms of k-mers in Venn diagram of input k-mer tables
 *
 *  Author:  Gene Myers
 *  Date  :  October, 2020
 *
 *********************************************************************************************/
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#include <fcntl.h>

#undef DEBUG_PARTITION

#include "libfastk.h"

static char *Usage = "[-h[<int(1)>:]<int(100)>] <source_1>[.ktab] <source_2>[.ktab] ...";

/****************************************************************************************
 *
 *  Print & compare utilities
 *
 *****************************************************************************************/


#define  COUNT_OF(p) (*((uint16 *) (p+kbyte)))

static inline int mycmp(uint8 *a, uint8 *b, int n)
{ while (n-- > 0)
    { if (*a++ != *b++)
        return (a[-1] < b[-1] ? -1 : 1);
    }
  return (0);
}

/****************************************************************************************
 *
 *  Find Venn Histograms
 *
 *****************************************************************************************/

static int HIST_LOW, HIST_HGH;

void Venn2(Kmer_Stream **Tv, int64 **comb)
{ int kbyte = Tv[0]->kbyte;

  Kmer_Stream *T, *U;
  int64       *Inter, *AminB, *BminA;

  uint8 *iptr, *jptr;
  int64 *h;
  int    c, d, v;

  T = Tv[0];
  U = Tv[1];

  AminB = comb[0];
  BminA = comb[1];
  Inter = comb[2];

  iptr = First_Kmer_Entry(T);
  jptr = First_Kmer_Entry(U);
  while (1)
    { if (iptr == NULL)
        { T = U;
          iptr = jptr;
          AminB = BminA;
          break;
        }
      if (jptr == NULL)
        break;
      v = mycmp(iptr,jptr,kbyte);
      if (v == 0)
        { h = Inter;
          c = COUNT_OF(iptr);
          d = COUNT_OF(jptr);
          if (c > d)
            c = d;
          iptr = Next_Kmer_Entry(T);
          jptr = Next_Kmer_Entry(U);
        }
      else if (v < 0)
        { h = AminB;
          c = COUNT_OF(iptr);
          iptr = Next_Kmer_Entry(T);
        }
      else
        { h = BminA;
          c = COUNT_OF(jptr);
          jptr = Next_Kmer_Entry(U);
        }
      if (c <= HIST_LOW)
        h[HIST_LOW] += 1;
      else if (c >= HIST_HGH)
        h[HIST_HGH] += 1;
      else
        h[c] += 1;
    }

  while (iptr != NULL)
    { c = COUNT_OF(iptr);
      iptr = Next_Kmer_Entry(T);
      if (c <= HIST_LOW)
        AminB[HIST_LOW] += 1;
      else if (c >= HIST_HGH)
        AminB[HIST_HGH] += 1;
      else
        AminB[c] += 1;
    }
}

void Venn(Kmer_Stream **T, int64 **comb, int nway)
{ int kbyte = T[0]->kbyte;

  uint8 *ptr[nway];
  int    itop, in[nway], imin;
  int    c, v, x;

  for (c = 0; c < nway; c++)
    ptr[c] = First_Kmer_Entry(T[c]);
  while (1)
    { for (c = 0; c < nway; c++)
        if (ptr[c] != NULL)
          break;
      if (c >= nway)
        break;
      itop = 0;
      in[itop++] = imin = c;
      for (c++; c < nway; c++)
        { if (ptr[c] == NULL)
            continue;
          v = mycmp(ptr[c],ptr[imin],kbyte);
          v = mycmp(ptr[c],ptr[imin],kbyte);
          if (v == 0)
            in[itop++] = c;
          else if (v < 0)
            { itop = 0;
              in[itop++] = imin = c;
	    }
        }

      v = 0;
      for (c = 0; c < itop; c++)
        { x = in[c];
          v |= (1 << x); 
          ptr[x] = Next_Kmer_Entry(T[x]);
        }
      comb[v-1] += 1;
    }
}


/****************************************************************************************
 *
 *  Main
 *
 *****************************************************************************************/

int main(int argc, char *argv[])
{ int nway;

  { int    i, j, k;
    int    flags[128];
    char  *eptr, *fptr;

    (void) flags;

    ARG_INIT("Vennex");

    HIST_LOW    = 1;
    HIST_HGH    = 100;

    j = 1;
    for (i = 1; i < argc; i++)
      if (argv[i][0] == '-')
        switch (argv[i][1])
        { default:
            ARG_FLAGS("")
            break;
          case 'h':
            HIST_LOW = strtol(argv[i]+2,&eptr,10);
            if (eptr > argv[i]+2)
              { if (HIST_LOW < 1 || HIST_LOW > 0x7fff)
                  { fprintf(stderr,"%s: Histogram count %d is out of range\n",
                                   Prog_Name,HIST_LOW);
                    exit (1);
                  }
                if (*eptr == ':')
                  { HIST_HGH = strtol(eptr+1,&fptr,10);
                    if (fptr > eptr+1 && *fptr == '\0')
                      { if (HIST_LOW > HIST_HGH)
                          { fprintf(stderr,"%s: Histogram range is invalid\n",Prog_Name);
                            exit (1);
                          }
                        break;
                      }
                  }
                else if (*eptr == '\0')
                  { HIST_HGH = HIST_LOW;
                    HIST_LOW = 1;
                    break;
                  }
              }
            fprintf(stderr,"%s: Syntax of -h option invalid -h[<int(1)>:]<int>\n",Prog_Name);
            exit (1);
        }
      else
        argv[j++] = argv[i];
    argc = j;

    nway = argc-1;
    if (nway < 2)
      { fprintf(stderr,"Usage: %s %s\n",Prog_Name,Usage);
        exit (1);
      }
  }

  { Kmer_Stream *T[nway];
    char        *upp[nway];
    char        *low[nway];
    int64      **comb;
    char        *name;
    int          kmer, ncomb;

    { int64 *hist;
      int    i;

      ncomb = (1 << nway) - 1;
      hist  = Malloc(sizeof(int64)*((HIST_HGH-HIST_LOW)+1),"Allocating histograms");
      comb  = Malloc(sizeof(int64 *)*ncomb,"Allocating histograms");

      comb[0] = hist-HIST_LOW;
      for (i = 1; i < ncomb; i++)
        comb[i] = comb[i-1] + (HIST_HGH-HIST_LOW) + 1;

      bzero(hist,sizeof(int64)*((HIST_HGH-HIST_LOW)+1));
    }

    { int c;

      kmer = 0;
      for (c = 0; c < nway; c++)
        { T[c] = Open_Kmer_Stream(argv[c+1],1);
          if (T[c] == NULL)
            { fprintf(stderr,"%s: Cannot open k-mer table %s\n",Prog_Name,argv[c+1]);
              exit (1);
            }
          if (kmer == 0)
            kmer = T[c]->kmer;
          else if (T[c]->kmer != kmer)
            { fprintf(stderr,"%s: K-mer tables do not involve the same K\n",Prog_Name);
              exit (1);
            }
        }
    }

    { int   nlen;
      char *p, *n;
      int   c, j;

      nlen = nway + 10;
      for (c = 0; c < nway; c++)
        { n = argv[c+1];
          p = index(n,'.');
          if (p != NULL)
            *p = '\0';
          nlen += strlen(n);

          n = upp[c] = Strdup(n,"Allocating upper case name");
          for (j = 0; n[j] != '\0'; j++)
            n[j] = toupper(n[j]); 

          n = low[c] = Strdup(n,"Allocating lower case name");
          for (j = 0; n[j] != '\0'; j++)
            n[j] = tolower(n[j]); 

          if (p != NULL)
            *p = '.';
        }
      name = Malloc(nlen,"Allocating name string");
    }

    if (nway == 2)
      Venn2(T,comb);
    else
      Venn(T,comb,nway);

    { int   i, b, f, c;
      char *a;

      for (i = 1; i <= ncomb; i++)
        { a = name;
          b = 1;
          for (c = 0; c < nway; c++)
            { if (c != 0)
                a = stpcpy(a,"_");
              if (b & i)
                a = stpcpy(a,upp[c]);
              else
                a = stpcpy(a,low[c]);
            }
          sprintf(a,".hist");

          f = open(name,O_CREAT|O_TRUNC|O_WRONLY,S_IRWXU);
          write(f,&kmer,sizeof(int));
          write(f,&HIST_LOW,sizeof(int));
          write(f,&HIST_HGH,sizeof(int));
          write(f,comb[i-1]+HIST_LOW,sizeof(int64)*((HIST_HGH-HIST_LOW)+1));
          close(f);
        }
    }

    { int c;

      free(name);
      for (c = 0; c < nway; c++)
        { free(low[c]);
          free(upp[c]);
        }
      for (c = 0; c < nway; c++)
        Free_Kmer_Stream(T[c]);
      free(comb[0]);
    }
  }

  Catenate(NULL,NULL,NULL,NULL);
  Numbered_Suffix(NULL,0,NULL);
  free(Prog_Name);

  exit (0);
}
