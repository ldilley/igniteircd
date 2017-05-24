/*
fixklines - scan an ircd.conf or kline.conf file
$Id: fixklines.c,v 1.1.1.1 2006/03/08 23:28:14 malign Exp $

  D lines of form
  D:192.168.0.* are converted to D:192.168.0.0/24
  K-lines of form
  K:192.168.0.*:reason:*
  converted to D:192.168.0.0/24
  # marks found in comment field of either D line or K line
  are replaced with a space.

To compile:
  gcc -o fixklines fixklines.c

Typical usage:

  ./fixklines ircd.conf ircd.conf.fix
  mv ircd.conf irc.conf.bak
  mv ircd.conf.fix ircd.conf
  ./fixklines kline.conf kline.conf.fix
  mv kline.conf kline.conf.bak
  mv kline.conf.fix kline.conf

  /rehash

-Dianora

*/
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <ctype.h>
#include <stdlib.h>

#define MAXLINE 1024
#define YES 1
#define NO 0

char linebuf[MAXLINE];
void read_write_klines(char *,char *);
int host_is_legal_dline(char *);

int main(int argc,char *argv[])
{
  if(argc < 3)
    {
      (void)fprintf(stderr,"%s: filein fileout\n",argv[0]);
      (void)fprintf(stderr,"i.e. %s ircd.conf ircd.conf.fix\n",argv[0]);
      exit(-1);
    }

  read_write_klines(argv[1],argv[2]);
  return 0;
}

/*
read_write_klines

input		- file name input
		- file name to output
output		- none
side effects	- kline/dline file read in, D lines of form
		  D:192.168.0.* converted to D:192.168.0.0/24
		  klines of form
		  K:192.168.0.*:reason:*
		  converted to D:192.168.0.0/24
		  # marks found in comment field of either D line or K line
		  are replaced with a space.
*/


void read_write_klines(char *filein,char *fileout)
{
  FILE *fp_in;
  FILE *fp_out;
  char *p;
  char *host;
  char *comment;
  char *name;
  int k_conf_line_count=0;
  int d_conf_line_count=0;
  int hash_in_comment_count=0;
  int k_lines_converted=0;

  if((fp_in = fopen(filein,"r")) == (FILE *)NULL)
    {
      (void)fprintf(stderr,"Cannot open %s for read\n", filein);
      exit(-1);
    }
 
  if((fp_out = fopen(fileout,"w")) == (FILE *)NULL)
    {
      (void)fprintf(stderr,"Cannot open %s for write\n", fileout);
      exit(-1);
    }

  while(fgets(linebuf,MAXLINE-1,fp_in))
    {
      p = strchr(linebuf,'\n');
      if(p)
	*p = '\0';
      if((linebuf[0] == 'K') && (linebuf[1] == ':'))
	{
	  k_conf_line_count++;
	  host = linebuf+2;
	  p = strchr(host,':');
	  if(p == (char *)NULL)
	    continue;
	  *p = '\0';
	  p++;
	  comment = p;
	  p = strchr(comment,':');
	  if(p == (char *)NULL)
	    continue;
	  *p = '\0';
	  p++;
	  name = p;
          p = strchr(comment,'#');
          while(p)
            {
              *p = ' ';
              p = strchr(comment,'#');
              hash_in_comment_count++;
            }
          if((*name == '*') && (*(name+1) == '\0'))
            {
              if(host_is_legal_dline(host))
                {
                  p = strchr(host,'*');
                  if(p)
                    {
                      if(*(p+1) == '\0')
                        {
                          *p = '\0';
                          (void)fprintf(fp_out,"D:%s0/24:%s\n",host,comment);
                          k_lines_converted++;
                        }
                      else
                        {
                          (void)fprintf(fp_out,"D:%s:%s\n",host,comment);
                          k_lines_converted++;
                        }
                    }
                  else
                    (void)fprintf(fp_out,"K:%s:%s:%s\n",host,comment,name);
                }
              else
                (void)fprintf(fp_out,"K:%s:%s:%s\n",host,comment,name);
            }
          else
            (void)fprintf(fp_out,"K:%s:%s:%s\n",host,comment,name);
	}
      else if( (linebuf[0] == 'D') && (linebuf[1] == ':'))
        {
          d_conf_line_count++;
          host = linebuf+2;
          p = strchr(host,':');
          if(p == (char *)NULL)
            continue;
          *p = '\0';
          p++;
          comment = p;
          p = strchr(comment,':');
          if(p != (char *)NULL)
            *p = '\0';

          p = strchr(comment,'#');
          while(p)
            {
              *p = ' ';
              p = strchr(comment,'#');
              hash_in_comment_count++;
            }

          p = strchr(host,'*');
          if(p)
            {
              if(*(p+1) == '\0')
                {
                  *p = '\0';
                  (void)fprintf(fp_out,"D:%s0/24:%s\n",host,comment); 
                }
              else
                {
                  (void)fprintf(fp_out,"D:%s:%s\n",host,comment);
                }
            }
          else
            (void)fprintf(fp_out,"D:%s:%s\n",host,comment);
        }
      else
        (void)fprintf(fp_out,"%s\n",linebuf);
    }
  (void)fclose(fp_in);
  (void)fclose(fp_out);
  (void)fprintf(stderr,"%d K: lines read in\n", k_conf_line_count);
  (void)fprintf(stderr,"%d D: lines read in\n", d_conf_line_count);
  (void)fprintf(stderr,"%d K: lines converted to D: lines\n",
     k_lines_converted);
  (void)fprintf(stderr,
     "%d hash (#) marks in comment fields found and replaced\n",
        hash_in_comment_count);
}


/*
host_is_legal_dline

inputs          - hostname
output          - YES if hostname is ip# only NO if its not
side effects    - NONE

(If you think you've seen this somewhere else, you are right.
ripped out of tcm-dianora basically)

-Diaora
*/

int host_is_legal_dline(char *host_name)
{
  int number_of_dots = 0;

  if(*host_name == '.')return(NO);      /* degenerate case */
  while(*host_name)
    {
      if( *host_name == '.' )
        number_of_dots++;
      else if(*host_name == '*')
        {
          if(*(host_name+1) == '\0')
            {
              if(number_of_dots == 3)
                return(YES);
              else
                return(NO);
            }
          else
            return(NO);
        }
      else if(!isdigit((int) *host_name))
        return(NO);
      host_name++;
    }

  if(number_of_dots == 3 )
    return(YES);
  else
    return(NO);
}

