/*--------------------------------------------------------------------*/
/* data2ddl.c - infers data type from literals in csv file            */
/* author marco gessner                                               */
/*--------------------------------------------------------------------*/
/*                                                                    */
/*--------------------------------------------------------------------*/

/*------ Imported Modules --------------------------------------------*/
#define DEBUGN
#define BUFLEN  131072
#define FIELDLEN 65536
#define MAXCOLS 1024
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#ifndef _WIN32
#define max(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((a)<(b)?(a):(b))
#endif
typedef enum{
  D2L_NOTYPE       //0
, D2L_NUMBER       //1
, D2L_STRING       //2
, D2L_NSTRING      //3
, D2L_DATE         //4
, D2L_DATETIME     //5
, D2L_TIMESTAMP    //6
, D2L_TIME         //7
, D2L_FLOAT        //8
} ColType;
char gColDel  [sizeof(unsigned long long)]="";
char gCharDel [sizeof(unsigned long long)]="";
char gRecDel  [sizeof(unsigned long long)]="\n";
char gNullChar[sizeof(unsigned long long)]="";
char gDecPoint[sizeof(unsigned long long)];
int withTitle=1;
int debug=0;
int verbose=0;
int piped=0;
char getsbuf[16];
char buffer[BUFLEN];
char colbuf[MAXCOLS][FIELDLEN]; // array of MAXCOLS pointers to FIELDLEN characters each
unsigned int colCount=MAXCOLS;
ColType gThisType=D2L_NOTYPE;
ColType gColType[MAXCOLS];
int gColIsNull[MAXCOLS];
int gColIsNullable[MAXCOLS];
unsigned long long gColLen[MAXCOLS];
unsigned long long gColPrec[MAXCOLS];
unsigned long long gColScale[MAXCOLS];
char colName[MAXCOLS][128];
char colTypeName[MAXCOLS][128];
char loadFlName[512];
size_t readCount = 0 ;

static int Stricmp(char *s1, char* s2) {
  size_t i=0;
  while (s1[i] && s2[i] && toupper(s1[i])==toupper(s2[i])) {
    i++;
  }
  if(toupper(s1[i])>toupper(s2[i])) return(1);
  else if(toupper(s1[i])<toupper(s2[i])) return(-1);
  else return(0);
}

static int Strnicmp(char *s1, char* s2, size_t len) {
  size_t i=0;
  while (i<len-1 && s1[i] && s2[i] && toupper(s1[i])==toupper(s2[i])) {
    i++;
  }
  if(toupper(s1[i])>toupper(s2[i])) return(1);
  else if(toupper(s1[i])<toupper(s2[i])) return(-1);
  else return(0);
}

int ChangeStrDefault(char *s, char *def, unsigned int len) {
  unsigned int i,j,cD; char *chrDel="'\"/";
  if(*s==':'||*s=='=') s++;
  for(cD=0;cD<3;cD++) if (s[0]==chrDel[cD]) break;
  if(cD<3) s++;
  j=0;
  while(*s&& j<len) {
    if(!Strnicmp(s,"TAB",3)) {
      def[j]='\t';while(isalpha(*s)) s++; s--;
    } else if(!Strnicmp(s,"COMMA",5)) {
      def[j]=',';while(isalpha(*s)) s++; s--;
    } else if(!Strnicmp(s,"SEMI",4)) {
      def[j]=';';while(isalpha(*s)) s++; s--;
    } else if(!Strnicmp(s,"APO",3)) {
      def[j]='\'';while(isalpha(*s)) s++; s--;
    } else if(!Strnicmp(s,"PIPE",4)) {
      def[j]='|';while(isalpha(*s)) s++; s--;
    } else if(!Strnicmp(s,"BAR",3)) {
      def[j]='|';while(isalpha(*s)) s++; s--;
    } else if(!Strnicmp(s,"QUOTE",5)) {
      def[j]='\"';while(isalpha(*s)) s++; s--;
    } else  if(*s=='#') {
      s++; def[j]=(unsigned char)atoi(s); while(isdigit(*s)) s++; s--;
    } else if(*s=='\\') {
      s++;
      switch(*s) {
        case 'b':def[j]='\b';break;
        case 'f':def[j]='\f';break;
        case 'n':def[j]='\n';break;
        case 'r':def[j]='\r';break;
        case 't':def[j]='\t';break;
        case 'v':def[j]='\v';break;
        case '\'':def[j]='\'';break;
        case '\"':def[j]='\"';break;
        case 'x': case 'X':
          s++; i=0;sscanf(s,"%x",&i);def[j]=(unsigned char)i;
          while(isdigit(*s)|| (toupper(*s)>='A'&& toupper(*s)<='F')){s++;} s--;
        break;
        default:def[j]=*s;break;
      }
    } else if(*s==chrDel[cD]) {
      s++;
      if(*s!=chrDel[cD]) break; else def[j]=*s;
    } else {
      def[j]=*s;
    }
    s++; j++;
  }
  def[j]=0;
  return(0);
}

char *Replace(char *baseString, char *searchString, char *replaceString) {
  char *cB, *cE;
  int i;
  for(cB=baseString;*cB;cB++) {
    if(*cB==*searchString) {
      i=0; cE=cB;
      do {
        cE++;i++;
        if(searchString[i]=='*') {
          while(searchString[i]=='*') { i++; }
          while(*cE && *cE!=searchString[i]) { cE++; }
          if(!*cE) break;
        }
      } while(*cE==searchString[i] && searchString[i]);
      memmove(cE+strlen(replaceString)-(cE-cB), cE, strlen(cE)+1);
      strncpy(cB,replaceString,strlen(replaceString));
    }
  }
  return(baseString);
}

static char *Trim(char *s) {
  char *cp=s-1+strlen(s);
  while(isspace(*cp) && cp>=s) {*cp=0; cp--;}
  cp=s;while(isspace(*cp)) cp++;
  memmove(s,cp,strlen(cp)+1);
  return(s);
}

unsigned int UnterminatedString(char *s, char *charDel) {
  unsigned int isUnTerminated=0;
  unsigned int iCharDel=*(unsigned int*)charDel;
  while (*s) {
    if( (unsigned int)*s++ == iCharDel ) {
      isUnTerminated=!isUnTerminated;
    }
  }
  return(isUnTerminated);
}

static char *Fgets(char *s, unsigned int n, FILE *f, char *charDel, char *recDel)
{
  unsigned int iRecDel =*(unsigned int*)recDel;
  unsigned int stdRecDel = (iRecDel == (unsigned int) '\n');
  static char compareBuf[16]=""; /* 'rolling' compare buffer */
  char *ret=s, *cp=s;
  unsigned int i=0, len=0, unterminatedString=0;
  int ch;
  do {
    if(stdRecDel) {
      ret=fgets(cp,n-len,f);
    } else {
      i=0; len=(int)strlen(recDel)-1;
      while((ch=fgetc(f))!=EOF && i<=n) {
        /* add if (ch!= whatever) if you want to ignore chars */
        s[i++]=(char)ch;
        memmove(compareBuf,compareBuf+1,len+1); compareBuf[len]=(char)ch;
        if(!strcmp(compareBuf,recDel)) break;
      }
      if(ch==EOF) ret=NULL;
      strcpy(s+i-len,"\n");
      if(i>(n-len)) {
        fprintf(stderr,
         "specified record delimiter never found in first %d bytes\n",
          n);
         ret=NULL;
      }
    }
    unterminatedString = UnterminatedString(s,charDel);
    if(ret && unterminatedString) {
      while(*cp) cp++;
      len=(unsigned int)(cp-s);
    }
  } while(ret && unterminatedString);
  Trim(s);
  if(ret) ret=s;
  return(ret);
}

static int GetStringN(char *t, char *s, char *colDel, char *charDel, char *recDel, size_t len)
{
  unsigned long long p=0, q=0, quoted;
  unsigned long long iCharDel  =*(unsigned long long*)charDel;
  char fieldEnd[3]={colDel[0],recDel[0],0};
  
  quoted=(iCharDel && (unsigned long long)s[0]==iCharDel);
  /* string is quoted if a char delimiter is set and
     1st char of string is the char delimiter. */
  if(!quoted) {
    p=(unsigned long long)strcspn(s,fieldEnd);
    strncpy(t,s,min(p,len));
    if(p>len) return(-2); // return negative number if string truncated
    return((int)min(p,len));
  }
  p++; // we are quoted here; move one forward; skip initial quote char
  while(s[p] && quoted) {
    if((unsigned long long)s[p] == iCharDel) { /* if quoted && chardel found */
      if((unsigned long long)s[p+1] == iCharDel)
        p++;                      /* if chardel doubled skip one */
      else
        quoted=0;                 /* else string is no longer quoted */
    }
    if(quoted) {
      if(q<len) {
        t[q++]=s[p];
      } else {
        return(-2);
      }
    }
    p++;
  }
  t[q]='\0';
  return(p);
}

static int GetColData(
  char *buf
, char (*colbuf)[FIELDLEN]
, char *colDel
, char *charDel
, char *recDel
, char *nullChar
, int forHeader
) {
  int r;
  unsigned long long i, pos=0;
  unsigned long long iColDel   =*(unsigned long long *)colDel;
  unsigned long long iRecDel   =*(unsigned long long *)recDel;
  unsigned long long iNullChar =*(unsigned long long *)nullChar;
  char *p;
  char typeName[16];
  for(i=0; i<colCount && i<MAXCOLS; i++) {
    // navigate to first non-space
    while(isspace(buf[pos])&&(unsigned long long)buf[pos]!=iColDel) pos++; 
    gColIsNull[i]=0;
    if(!buf[pos]) {
      colCount=i;
      break;
    } 
// determine if it's a NULL field before parsing the following string
// a) we're pointing at the zero terminator
// b) we're pointing at the next column delimiter
// c) we're pointing at the record delimiter
// d) the null char is set and we're pointing at it
    if((unsigned long long)buf[pos]==iColDel   
    || (unsigned long long)buf[pos]==iRecDel
    || ( iNullChar && iNullChar == *(unsigned long long*) (buf+pos) )
     ) {
      gColIsNull[i]= 1;r=0;
//      if(withTitle && forHeader) { // no NULLs in Header file; end here if found
//        colCount=i;
//        break;
//      }
      if(buf[pos]) {
        pos++;
        continue;
      }
    } else { // so not a NULL
      p=colbuf[i];
      memset(p,0,FIELDLEN);
      strcpy(typeName, "string");
      r=GetStringN(p, buf+pos, colDel, charDel, recDel, FIELDLEN-1);
      if(r==0) gColIsNull[i]=1;
    }
    if(r<0) {
      fprintf(stderr,"%s(%zd)\t<%.*s...>\ncolumn %d, %s:",
              loadFlName, (unsigned long long)readCount, (int)min(30,strlen(buf)-1), buf, (int)i+1, colName[i]);
      if(r==-1)
        fprintf(stderr, " bad %s conversion\n",typeName);
      if(r==-2)
        fprintf(stderr, " string truncation, source length %zd, buffer length %d\n", strlen(buf+pos), FIELDLEN-1);
        return(-1);
    } else {
      pos+=r;
    }
    if((unsigned long long)buf[pos]==iColDel && buf[pos+1])pos++;
    Trim(p);
  }
  return(0);
}

static int SIsAscii(char*s) {
  int i;
  for(i=0;!!s[i];i++) {
    if(!isascii(s[i]))
      return(0);
  }
  return(1);
}

#define setnumlen(l,s){int _i=0;char *_cp=(s);for(_i=0;_cp[_i]&&isdigit(_cp[_i]);_i++){}; l=_i;}
#define trimtrail0(s){int _i=strlen(s)-1;while(_i > 1 && s[_i]=='0') {s[_i]='\0';_i--;}};
ColType GetDataType(int i, char *s, unsigned long long *colLen, unsigned long long *colPrec, unsigned long long *colScale) {
  char *cp;
  double db;
  char cb[8];
  unsigned long long thisType, thisPrec=0, thisScale=0;
  *colLen=max(*colLen,(unsigned long long)strlen(s));
  if(!s[0]) {
    thisType=D2L_NOTYPE;
    thisScale=thisPrec=0;
  } else if(!SIsAscii(s)) {
    thisType=D2L_NSTRING;
  } else if ( 
   (   isdigit(s[0])
    && isdigit(s[1])
    && isdigit(s[2])
    && isdigit(s[3])
    && ispunct(s[4])
    && isdigit(s[5])
    && isdigit(s[6])
    && ispunct(s[7])
    && isdigit(s[8])
    && isdigit(s[9])
    && !      s[10]
   ) || (
       isdigit(s[0])
    && isdigit(s[1])
    && ispunct(s[2])
    && isdigit(s[3])
    && isdigit(s[4])
    && ispunct(s[5])
    && isdigit(s[6])
    && isdigit(s[7])
    && isdigit(s[8])
    && isdigit(s[9])
    && !      s[10]
   ) || (
       isdigit(s[0]) // 0 
    && isdigit(s[1]) // 1 
    && ispunct(s[2]) // - 
    && isalpha(s[3]) // f 
    && isalpha(s[4]) // e 
    && isalpha(s[5]) // b 
    && ispunct(s[6]) // - 
    && isdigit(s[7]) // 1 
    && isdigit(s[8]) // 9 
    &&(!       s[9]||(isdigit(s[9])&&isdigit(s[10])&&!s[11]))
   )
  ) {
    thisType=D2L_DATE;
  } else if(
       isdigit(s[ 0])
    && isdigit(s[ 1])
    && isdigit(s[ 2])
    && isdigit(s[ 3])
    && ispunct(s[ 4])
    && isdigit(s[ 5])
    && isdigit(s[ 6])
    && ispunct(s[ 7])
    && isdigit(s[ 8])
    && isdigit(s[ 9])
    && (s[10]==' '||toupper(s[10])=='T')
    && isdigit(s[11])
    && isdigit(s[12])
    && ispunct(s[13])
    && isdigit(s[14])
    && isdigit(s[15])
    && ispunct(s[16])
    && isdigit(s[17])
    && isdigit(s[18])
    && !s[19]||toupper(s[19])=='Z'
  ) {
    thisType=D2L_DATETIME;
    if(toupper(s[19])=='Z') 
      s[19]=0;
    if(!strcmp(s+11,"00:00:00")) {
      thisType=D2L_DATE;
    }
  } else if (
       isdigit(s[ 0])
    && isdigit(s[ 1])
    && ':' ==  s[ 2]
    && isdigit(s[ 3])
    && isdigit(s[ 4])
    && ':' ==  s[ 5]
    && isdigit(s[ 6])
    && isdigit(s[ 7])
  ) {
    thisType=D2L_TIME;
    if(s[8]=='.') {
      s=s+9;
      trimtrail0(s);
      setnumlen(thisScale,s);
      if(s[thisScale])
        thisType=D2L_STRING;
    } else {
      thisScale=0;
      if(s[8])
        thisType=D2L_STRING;
    }
  } else if(
       isdigit(s[ 0])
    && isdigit(s[ 1])
    && isdigit(s[ 2])
    && isdigit(s[ 3])
    && ispunct(s[ 4])
    && isdigit(s[ 5])
    && isdigit(s[ 6])
    && ispunct(s[ 7])
    && isdigit(s[ 8])
    && isdigit(s[ 9])
    && (s[10]==' '||toupper(s[10])=='T')
    && isdigit(s[11])
    && isdigit(s[12])
    && ispunct(s[13])
    && isdigit(s[14])
    && isdigit(s[15])
    && ispunct(s[16])
    && isdigit(s[17])
    && isdigit(s[18])
    && s[19]=='.'
    && isdigit(s[20])
  ) {
    thisType=D2L_TIMESTAMP;
    s=s+20;
    if(toupper(s[strlen(s)-1])=='Z') { 
      s[strlen(s)-1]=0;
    }
    trimtrail0(s);
    setnumlen(thisScale,s);
    if(s[thisScale]) {
      if((s[thisScale+0]=='+'||s[thisScale+0]=='-')
        &&isdigit(s[thisScale+1])
        &&isdigit(s[thisScale+2])
        &&ispunct(s[thisScale+3])
      ) {
        thisType=D2L_TIMESTAMP;
      } else {
        thisType=D2L_STRING;
        thisScale=thisPrec=0;
      }
    }
  } else if (strchr("+-.0123456789",s[0])) {
    thisType=D2L_NUMBER;
    if(strchr("+-",s[0])) {
      if(isdigit(s[1])) {
        s=s+1;
      } else if(toupper(s[1])=='E' && isdigit(s[2]) && isdigit(s[3]) && ! s[4]) {
        thisType=D2L_FLOAT;
        thisPrec=thisScale=0;
        return(thisType);
      } else {
        thisType=D2L_STRING;
        thisPrec=thisScale=0;
        return(thisType);
      }
    } else {
      s=s;
    }
    setnumlen(thisPrec,s);
    if(!s[thisPrec]) {
      thisScale=0;
    } else {
      if(s[thisPrec]=='.') {
        if(s[0]=='0' && s[1] && s[1] !='.') { // if the first character is a zero and not followed by a decimal point, it's not numeric
          thisType=D2L_STRING;
          thisPrec=thisScale=0;
        } else {
          s+=thisPrec+1;
          if(thisPrec==0) {
            thisPrec++;
          }
          trimtrail0(s);
          if(!strcmp(s,"0")) *s=0;
          setnumlen(thisScale,s);
          if(toupper(s[thisScale])=='E' && strcat(s,"test") && sscanf(s,"%lf%s", &db,cb)==2 && !strcmp(cb,"test")) {
            thisType=D2L_FLOAT;
            thisPrec=thisScale=0;
            return(thisType);
          }
          if(s[thisScale]) {
            int j;
            for(j=0;s[thisScale+j]&&s[thisScale+j]=='0';j++);
            if(s[thisScale+j]) {
              thisType=D2L_STRING;
              thisScale=thisPrec=0;
            }
          } else {
            thisPrec+=thisScale;
          }
        }
      } else {
        thisType=D2L_STRING;
        thisScale=thisPrec=0;
      }
    }
  } else {
    thisType=D2L_STRING;
    thisScale=thisPrec=0;
  }
  *colPrec =(unsigned long long)max(gColPrec[i],thisPrec);
  *colScale=(unsigned long long)max(gColScale[i],thisScale);
  return (thisType);
}

void DebugColumns(char (*colbuf)[FIELDLEN]) {
  unsigned int i, max_sym_len=0;
  for(i=0;i<colCount;i++) {
    max_sym_len=max(max_sym_len,(unsigned int)strlen(colName[i])+1);
  }
  for(i=0;i<colCount;i++) {
    printf("%-*s",max_sym_len,colName[i]);
    printf(": ");
    if(gColIsNull[i]) {
      printf("(Null)");
    } else {
      printf("[ %s ]", colbuf[i]);
    }
    printf("\n");
    fflush(stdout);
  }
  return;
}

int main(int argc, char**argv)
{
  FILE *in=NULL;
  char tbName[128]="";
  char ext[32];
  unsigned int i=0;
  char *bncp; // base name char pointer
  char *ecp;  // extension begin char pointer
  unsigned int maxNmLen,maxTpLen;
#ifdef DEBUGN
  char buf[32];
  size_t fpos=0, fsize=0, fportion=0, lineLen=0, estLineCount=0, readCount=0, posShown=0
        , showLineCount=0, showNextLine=0; // variables for selecting lines to show
#else
  size_t showLineCount=0;
#endif
  for(i=0;i<MAXCOLS;i++) {
    sprintf(colName[i],"col%04d", i+1);
  }
  if(argc<2){
#ifdef DEBUGN
    printf("Usage is %s [-coldel=<sep>] [-chardel=<chardel>] [-recdel=<recdel>] [-notitle][-debug[:<n>]][-tbname:<tbname>][-verbose] <file name> [> outfile]\n"
#else
    printf("Usage is %s [-coldel=<sep>] [-chardel=<chardel>] [-recdel=<recdel>] [-notitle][-debug] [-verbose]<file name> [> outfile]\n"
#endif
     "e. g.    %s -coldel:(SEMI|COMMA|BAR|\\x7c|','),' -chardel:(APO|QUOTE) myfile.txt   >myfile.ddl\n"
     "deducts the data type and the column name from first line and following literals in a file.\n"
     "author marco.gessner@hpe.com"
    , argv[0], argv[0]);
    return(-1);
  }
  for(i=1;argv[i]&&argv[i][0]=='-';i++) {
    if(!Strnicmp(argv[i],"-colDel",7)) {
      ChangeStrDefault(argv[i]+7,gColDel,sizeof(gColDel)-1);
    } else if(!Strnicmp(argv[i],"-charDel=",7)) {
      ChangeStrDefault(argv[i]+8,gCharDel,sizeof(gCharDel)-1);
    } else if(!Strnicmp(argv[i],"-recDel=",6)) {
      ChangeStrDefault(argv[i]+7,gRecDel,sizeof(gRecDel)-1);
    } else if(!Strnicmp(argv[i],"-decPoint=",9)) {
      ChangeStrDefault(argv[i]+10,gDecPoint,sizeof(gDecPoint)-1);
    } else if(!Strnicmp(argv[i],"-nullchar",9)) {
      ChangeStrDefault(argv[i]+10,gNullChar,sizeof(gNullChar)-1);
    } else if(!Strnicmp(argv[i],"-tbName=",6)) {
      ChangeStrDefault(argv[i]+7,tbName,sizeof(tbName)-1);
    } else if(!Strnicmp(argv[i],"-debug",6)) {
      debug=1;
      showLineCount=0;
#ifdef DEBUGN
      if(argv[i][6]) {
        ChangeStrDefault(argv[i]+6, buf, sizeof(buf)-1);
        showLineCount=atoi(buf);
      }
#endif
    } else if(!Stricmp(argv[i],"-notitle")) {
      withTitle=0;
    } else if(!Stricmp(argv[i],"-verbose")) {
      verbose=1;
    } else if(!Stricmp(argv[i],"-")) {
      in=stdin;
    }
  }
  argv+=i-1;
  if(in==stdin) {
    fprintf(stderr,"from stdin\n");
    piped=1;
  } else {
    if(!(in=fopen(argv[1],"r"))) {
      perror(argv[1]);
      return(1);
    }
  }
  if(piped) {
    if(!tbName[0]) {
      strcpy(tbName,"stdin"); 
    }
  } else if(!tbName[0]) {
    bncp=strrchr(argv[1],'/');
    if(!bncp) bncp=strrchr(argv[1],'\\');
    if(!bncp) bncp=argv[1]; else bncp++;
    strcpy(tbName,bncp);
    strtok(tbName,".");
    Replace(tbName,"-","_");
    Replace(tbName," ","_");
    Replace(tbName,"#","no");
    Replace(tbName,".","");
    Replace(tbName,":","");
    Replace(tbName,"[","");
    Replace(tbName,"]","");
    if(!gColDel[0] && (ecp=strtok(NULL,"."))) {
      strcpy(ext,ecp);
      if(!Stricmp(ecp,"txt")) {
        strcpy(gColDel,"\t");
      } else if(!Stricmp(ecp,"csv")) {
        strcpy(gColDel,",");
      } else if(!Stricmp(ecp,"ssv")) {
        strcpy(gColDel,";");
      } else if(!Stricmp(ecp,"bsv")) {
        strcpy(gColDel,"|");
      } else if(!Stricmp(ecp,"tsv")) {
        strcpy(gColDel,"\t");
      }
    }
#ifdef DEBUGN
    if(debug && showLineCount) {
      fseek(in,0,SEEK_END); fsize=ftell(in);
      fseek(in,0,SEEK_SET);
      fportion = fsize / (showLineCount+1);
    }
#endif
  }
  if(!gColDel[0]) {
    strcpy(gColDel,"|");
  }
  memset(gColType,0,sizeof(gColType));
  while(Fgets(buffer,BUFLEN,in,gCharDel,gRecDel)) {
    unsigned char bombuf[]= {0xEF, 0xBB, 0xBF, 0x00};
    if(readCount==0 && !strncmp(buffer,(char *)bombuf,3)) {
      memmove(buffer,buffer+3,strlen(buffer));
      fprintf(stderr,"utf BOM removed from first line\n");
    }
    readCount++;
    if(verbose && readCount%10000==0) fprintf(stderr,"readCount:%zd\n", readCount);
#ifdef DEBUGN
    if(debug && showLineCount && !piped && readCount==2) { // if debug switched on, subtract fpos after 1st line from 
      lineLen=strlen(buffer); 
      estLineCount= fsize / lineLen; // rough estimate of line count
    }
#endif
    GetColData(buffer,colbuf,gColDel,gCharDel,gRecDel,gNullChar,readCount==1&&withTitle);
    if(debug){
      if(!showLineCount) { // meaning all
        DebugColumns(colbuf);
        printf("\n");
      } 
      if(piped && showLineCount && readCount==2) {
        printf("from pipe; 2nd line of input:\n");
        DebugColumns(colbuf);
      } 
#ifdef DEBUGN
      if(!piped && readCount > 1) {
        fpos=ftell(in);
        if(posShown <= showLineCount && fpos > (posShown+1) * fportion) {
        posShown++;
        showNextLine=1;
        }
        if(showNextLine) {
          showNextLine=0;
          printf("line %zu of approx %zu\n", readCount, estLineCount);
          DebugColumns(colbuf);
          printf("\n");
        }
      }
#endif
    }
    for(i=0;i<colCount;i++) {
      if(readCount==1) {
        if(withTitle) {
          if(colbuf[i][0]) {
            strcpy(colName[i],colbuf[i]);
            Replace(colName[i],"-"," ");
            Replace(colName[i]," ","_");
            Replace(colName[i],"#","no");
            Replace(colName[i],".","");
            Replace(colName[i],":","");
            Replace(colName[i],"[","_");
            Replace(colName[i],"]","_");
            Replace(colName[i],"(","_");
            Replace(colName[i],")","_");
          }
        } else {
          gThisType=GetDataType(i, colbuf[i], gColLen+i, gColPrec+i, gColScale+i);
        }
      } else {
        if(!gColIsNull[i]) {
          gThisType=GetDataType(i, colbuf[i], gColLen+i, gColPrec+i, gColScale+i);
        } else {
          gColIsNullable[i]=1;
          continue;
        }
      }
      if(!withTitle || readCount > 1) {
				// data type change decision tree. Datatype in this row detected
				// and put into gThisType. Compare with data types until now: gColType[i] .
        if(gColType[i]==D2L_NSTRING) {
					continue; // can't get any worse ...
        }
        if(gColType[i]==D2L_NOTYPE) {
          gColType[i]=gThisType; // no type detected - no op
        } else if (gColType[i] != gThisType) { // only if the currently detected data type differs from the previous
          if(gThisType==D2L_NSTRING) {
          } gColType[i]=D2L_NSTRING; // That's it - can't get any worse
          if(gThisType == D2L_STRING && gColType[i] != D2L_NSTRING) {
            gColType[i]=D2L_STRING; // second worst.
          } else if(gThisType == D2L_NUMBER && gColType[i] != D2L_FLOAT) {
            gColType[i]=D2L_STRING; // if the previous wasn't a D2L_NUMBER and wasn't D2L_FLOAT, that's it -> D2L_STRING
          } else if(gThisType == D2L_FLOAT) {
            if(gColType[i]==D2L_NUMBER) {
              gColType[i]=D2L_FLOAT; // if was numeric and is float now, float is stronger
            } else {
              gColType[i]=D2L_STRING; // not numeric before and double now is string.
            }
          } else if(gThisType == D2L_DATE) {
            if(gColType[i] == D2L_NUMBER) {
              gColType[i]=D2L_STRING; // if this is D2L_DATE and the previous was a D2L_NUMBER, that's it -> D2L_STRING
            } else if(gColType[i] == D2L_DATETIME) {
              gColType[i]=D2L_DATETIME; // if this is D2L_DATE and the previous was a D2L_DATETIME, D2L_DATETIME is stronger
            } else if(gColType[i] == D2L_TIMESTAMP) {
              gColType[i]=D2L_TIMESTAMP; // if this is D2L_DATE and the previous was a D2L_TIMESTAMP, D2L_TIMESTAMP is stronger
            } else if(gColType[i] == D2L_TIME) {
              gColType[i]=D2L_TIMESTAMP; // if this is D2L_DATE and the previous was a D2L_TIME, D2L_TIMESTAMP takes both
            }
          } else if(gThisType == D2L_DATETIME) {
            if(gColType[i] == D2L_NUMBER) {
              gColType[i]=D2L_STRING; // if this is D2L_DATETIME and the previous was a D2L_NUMBER, that's it -> D2L_STRING
            } else if(gColType[i] == D2L_TIMESTAMP) {
              gColType[i]=D2L_TIMESTAMP; // if this is D2L_DATETIME and the previous was a D2L_TIMESTAMP, D2L_TIMESTAMP is stronger
            } else {
              gColType[i]=D2L_DATETIME; // if this is D2L_DATETIME and the previous was a D2L_TIME or D2L_DATE, D2L_DATETIME takes both
            }
          } else if(gThisType == D2L_TIMESTAMP) {
            if(gColType[i] == D2L_NUMBER) {
              gColType[i]=D2L_STRING; // if this is D2L_TIMESTAMP and the previous was a D2L_NUMBER, that's it -> D2L_STRING
            } else {
              gColType[i]=D2L_TIMESTAMP;
            } // can't be D2L_STRING, D2L_NUMBER is done; if it's D2L_TIME,D2L_DATE or D2L_DATETIME, it remains D2L_TIMESTAMP
          } // if gThisType == D2L_TIMESTAMP
        } // if gThisType!=gColType[i]
      } // if rowCount != 1
    } // columns loop
  } // fread loop
  fclose(in);
  if(debug && piped && showLineCount) {
    printf("from pipe; last line of input:\n");DebugColumns(colbuf);
  }
  printf("CREATE TABLE %s (\n", tbName);
  for(i=0;i<colCount;i++) {
  if(gColType[i]==D2L_NOTYPE) {
    strcpy(colTypeName[i],"VARCHAR(32)");
  } else if(gColType[i]==D2L_FLOAT) {
    strcpy(colTypeName[i],"FLOAT");
  } else if(gColType[i]==D2L_NUMBER && gColScale[i]==0) {
      if(gColPrec[i]<5)       strcpy(colTypeName[i],"SMALLINT");
      else if(gColPrec[i]<10) strcpy(colTypeName[i],"INTEGER");
      else if(gColPrec[i]<18) strcpy(colTypeName[i],"BIGINT");
      else sprintf(colTypeName[i],"NUMERIC(%Id)",gColPrec[i]);
    } else if(gColType[i]==D2L_NUMBER) {
      sprintf(colTypeName[i],"NUMERIC(%Id,%Id)",gColPrec[i],gColScale[i]);
    } else if(gColType[i]==D2L_NSTRING) {
      sprintf(colTypeName[i],"%s(%Id)",gColLen[i]>=10?"NCHAR VARYING":"NCHAR", gColLen[i]);
    } else if(gColType[i]==D2L_STRING) {
      sprintf(colTypeName[i],"%sCHAR(%Id)",gColLen[i]>=10?"VAR":"", gColLen[i]);
    } else if(gColType[i]==D2L_DATE) {
      strcpy(colTypeName[i],"DATE");
    } else if(gColType[i]==D2L_DATETIME) {
      strcpy(colTypeName[i],"TIMESTAMP(0)");
    } else if(gColType[i]==D2L_TIMESTAMP) {
      sprintf(colTypeName[i],"TIMESTAMP(%Id)",gColScale[i]);
    } else if(gColType[i]==D2L_TIME) {
      if(!gColScale[i]) strcpy(colTypeName[i],"TIME");
      else sprintf(colTypeName[i],"(%Id)",gColScale[i]);
    }
  }
  maxNmLen=maxTpLen=0;
  for(i=0;i<colCount;i++) {
    maxNmLen=max(maxNmLen,(unsigned int)strlen(colName[i]));
    maxTpLen=max(maxTpLen,(unsigned int)strlen(colTypeName[i]));
  }
  for(i=0;i<colCount;i++) {
    char *nullInfo;
    
    if(!gColIsNullable[i])
      nullInfo="NOT NULL";
    
    printf(
      "%s %-*s %-*s%s\n"
    , (!i?" ":",")
    , maxNmLen
    , colName[i]
    , maxTpLen
    , colTypeName[i]
    , gColIsNullable[i]?(gColType[i]==D2L_NOTYPE?" /*always null*/":""):" NOT NULL");
  }
  printf(");\n");
  return(0);
}
