/*********************************************************************************************\
 *
 *  Code for produce all potential haplotype k-mers with a single SNP in the center of
 *    the k-mers
 *
 *  Author:  Gene Myers
 *  Date  :  October, 2020
 *
 *********************************************************************************************/
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <math.h>

#undef DEBUG_PARTITION

#include "gene_core.h"

static char *Usage = "[-h<int>] <source_root>.K<k>";


/****************************************************************************************
 *
 *  The interface you may want to lift for your own use
 *
 *****************************************************************************************/

typedef struct
  { int    kmer;    //  Kmer length
    int    kbyte;   //  Kmer encoding in bytes
    int    tbyte;   //  Kmer+count entry in bytes
    int64  nels;    //  # of unique, sorted k-mers in the table
    uint8 *table;   //  The (huge) table in memory
  } Kmer_Table;

#define  KMER(i)  (table+(i)*tbyte)
#define  COUNT(i) (*((uint16 *) (table+(i)*tbyte+kbyte)))
#define  COUNT_OF(p) (*((uint16 *) (p+kbyte)))

Kmer_Table *Load_Kmer_Table(char *name, int cut_freq);
void        Free_Kmer_Table(Kmer_Table *T);

void        List_Kmer_Table(Kmer_Table *T);
void        Check_Kmer_Table(Kmer_Table *T);
int         Find_Kmer(Kmer_Table *T, char *kseq);


/****************************************************************************************
 *
 *  Print & compare utilities
 *
 *****************************************************************************************/

static char dna[4] = { 'a', 'c', 'g', 't' };

static char *fmer[256], _fmer[1280];

static void setup_fmer_table()
{ char *t;
  int   i, l3, l2, l1, l0;

  i = 0;
  t = _fmer;
  for (l3 = 0; l3 < 4; l3++)
   for (l2 = 0; l2 < 4; l2++)
    for (l1 = 0; l1 < 4; l1++)
     for (l0 = 0; l0 < 4; l0++)
       { fmer[i] = t;
         *t++ = dna[l3];
         *t++ = dna[l2];
         *t++ = dna[l1];
         *t++ = dna[l0];
         *t++ = 0;
         i += 1;
       }
}

static void print_seq(uint8 *seq, int len)
{ int i, b, k;

  b = len >> 2;
  for (i = 0; i < b; i++)
    printf("%s",fmer[seq[i]]);
  k = 6;
  for (i = b << 2; i < len; i++)
    { printf("%c",dna[seq[b] >> k]);
      k -= 2;
    }
}

static void print_pack(uint8 *seq, int len)
{ int i;

  for (i = 0; i < (len+3)/4; i++)
    printf(" %02x",seq[i]);
}

static inline int mycmp(uint8 *a, uint8 *b, int n)
{ while (n-- > 0)
    { if (*a++ != *b++)
        return (a[-1] < b[-1] ? -1 : 1);
    }
  return (0);
}

static inline void mycpy(uint8 *a, uint8 *b, int n)
{ while (n--)
    *a++ = *b++;
}


/****************************************************************************************
 *
 *  Read in a table and return as Kmer_Table object
 *
 *****************************************************************************************/

Kmer_Table *Load_Kmer_Table(char *name, int cut_freq)
{ Kmer_Table *T;
  int         kmer, tbyte, kbyte;
  int64       nels;
  uint8      *table;

  FILE  *f;
  int    p;
  int64  n;

  //  Find all parts and accumulate total size

  nels = 0;
  for (p = 1; 1; p++)
    { f = fopen(Catenate(name,Numbered_Suffix(".T",p,""),"",""),"r");
      if (f == NULL)
        break;
      fread(&kmer,sizeof(int),1,f);
      fread(&n,sizeof(int64),1,f);
      nels += n;
      fclose(f);
    }
  if (p == 1)
    { fprintf(stderr,"%s: Cannot find table files for %s\n",Prog_Name,name);
      exit (1);
    }

  //  Allocate in-memory table

  kbyte = (kmer+3)>>2;
  tbyte = kbyte+2;
  table = Malloc(nels*tbyte,"Allocating k-mer table\n");
  if (table == NULL)
    exit (1);

  //  Load the parts into memory

  fprintf(stderr,"Loading %d-mer table with ",kmer);
  Print_Number(nels,0,stderr);
  fprintf(stderr," entries in %d parts\n",p-1);
  fflush(stderr);

  nels = 0;
  for (p = 1; 1; p++)
    { f = fopen(Catenate(name,Numbered_Suffix(".T",p,""),"",""),"r");
      if (f == NULL)
        break;
      fread(&kmer,sizeof(int),1,f);
      fread(&n,sizeof(int64),1,f);
      fread(KMER(nels),n*tbyte,1,f);
      nels += n;
      fclose(f);
    }

  if (cut_freq > 1)
    { int64 i, j;
      uint8 *iptr, *jptr;

      jptr = table;
      for (i = 0; i < nels; i++)
        { iptr = KMER(i);
          if (COUNT_OF(iptr) >= cut_freq)
            { mycpy(jptr,iptr,tbyte);
              jptr += tbyte;
            }
        }
      j = (jptr-table)/tbyte;
      if (j < nels)
        { nels = j;
          table = Realloc(table,nels*tbyte,"Reallocating table");
        }
    }

  T = Malloc(sizeof(Kmer_Table),"Allocating table record");
  if (T == NULL)
    exit (1);

  T->kmer  = kmer;
  T->tbyte = tbyte;
  T->kbyte = kbyte;
  T->nels  = nels;
  T->table = table;

  return (T);
}

void Free_Kmer_Table(Kmer_Table *T)
{ free(T->table);
  free(T);
}


/****************************************************************************************
 *
 *  Find Haplotype Pairs
 *
 *****************************************************************************************/

static int HAPLO_X;

static inline int mypref(uint8 *a, uint8 *b, int n)
{ int   i;
  uint8 x, y;
  
  for (i = 0; i <= n; i += 4)
    { if (*a != *b)
        { x = *a;
          y = *b;
          if ((x & 0xf0) != (y & 0xf0))
            if ((x & 0xc0) != (y & 0xc0))
              return (i);
            else
              return (i + 1);
          else
            if ((x & 0xfc) != (y & 0xfc))
              return (i + 2);
            else
              return (i + 3);
        }
      a += 1;
      b += 1;
    }
  return (n+1);
}

void Find_Haplo_Pairs(Kmer_Table *T)
{ int    kmer  = T->kmer;
  int    tbyte = T->tbyte;
  int    kbyte = T->kbyte;
  int64  nels  = T->nels;
  uint8 *table = T->table;

  uint8  prefs[] = { 0x3f, 0x0f, 0x03, 0x00 };

  int    khalf;
  uint8 *iptr, *jptr, *nptr;
  uint8 *finger[5];
  uint8 *flimit[4];
  int    n, i, c, f, x, v;
  int    mc, hc;
  uint8 *mr, *hr;
  int    mask, offs, rem;

  khalf = kmer/2;

  mask = prefs[khalf&0x3]; 
  offs = (khalf >> 2) + 1;
  rem  = ((kmer+3) >> 2) - offs;

  setup_fmer_table();

#ifdef DEBUG_PARTITION
  printf("Extension = K[%d]&%02x . K[%d..%d)\n",offs-1,mask,offs,offs+rem);
#endif

  nptr = KMER(nels);
  for (iptr = table + 368645*tbyte; iptr < nptr; iptr = jptr)
    { f = 0;
      finger[f++] = iptr;
      for (jptr = iptr+tbyte; jptr < nptr; jptr += tbyte)
        { x = mypref(jptr-tbyte,jptr,khalf); 
          if (x < khalf)
            break;
          if (x == khalf)
            finger[f++] = jptr;
        }
#ifdef DEBUG_PARTITION
      printf("part %d",f);
      for (i = 0; i < f; i++)
        printf(" %ld",(finger[i]-table)/tbyte);
      printf(" %ld\n",(jptr-table)/tbyte);
#endif
      if (f <= 1)
        continue;

      finger[f] = jptr;
      for (i = 0; i < f; i++)
        flimit[i] = finger[i+1];

      for (n = (jptr-iptr)/tbyte; n > 1; n--)
        { x = 0;
          while (finger[x] >= flimit[x])
            x += 1;
          mr = finger[x]+offs;
          mc = mr[-1] & mask;
          c  = 1;
          for (i = x+1; i < f; i++)
            if (finger[i] < flimit[i])
              { hr = finger[i]+offs;
                hc = hr[-1] & mask;

                if (hc == mc)
                  { v = mycmp(hr,mr,rem);
                    if (v == 0)
                      c += 1;
                    else if (v < 0)
                      { mc = hc;
                        mr = hr;
                        c = 1;
                        x = i;
                      }
                  }
                else if (hc < mc)
                  { mc = hc;
                    mr = hr;
                    c = 1;
                    x = i;
                  }
              }
#ifdef DEBUG_PARTITION
          printf("Min %d cnt %d\n",x,c);
#endif
          if (c > 1)
            { print_seq(finger[x],kmer);
              printf(" %d <%d>\n",COUNT_OF(finger[x]),x);
              for (i = x+1; i < f; i++)
                if (finger[i] < flimit[i])
                  { hr = finger[i]+offs;
                    hc = hr[-1] & mask;
                    if (hc == mc && mycmp(hr,mr,rem) == 0)
                      { if (c > 1)
                          { print_seq(finger[i],kmer);
                            printf(" %d <%d>\n",COUNT_OF(finger[i]),i);
                          }
                        finger[i] += tbyte;
                      }
                  }
              printf("\n");
            }
          finger[x] += tbyte;
        }
    }
}
          

/****************************************************************************************
 *
 *  Main
 *
 *****************************************************************************************/

int main(int argc, char *argv[])
{ Kmer_Table *T;

  { int    i, j, k;
    int    flags[128];
    char  *eptr;

    ARG_INIT("Haplex");

    j = 1;
    for (i = 1; i < argc; i++)
      if (argv[i][0] == '-')
        switch (argv[i][1])
        { default:
            ARG_FLAGS("")
            break;
          case 'h':
            ARG_POSITIVE(HAPLO_X,"Mean Haplotype Coverage")
            break;
        }
      else
        argv[j++] = argv[i];
    argc = j;

    if (argc != 2)
      { fprintf(stderr,"Usage: %s %s\n",Prog_Name,Usage);
        exit (1);
      }
  }

  T = Load_Kmer_Table(argv[1],1);

  Find_Haplo_Pairs(T);

  Free_Kmer_Table(T);
  exit (0);
}
