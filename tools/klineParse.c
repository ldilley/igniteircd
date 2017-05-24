/* klineParse.c   
 * 1.0  17Dec98 Darkshde -- Initial bug ridden release
 * 2.0  07Jan99 Darkshde -- added redundant kline checking
 * 2.1  11Jan99 Darkshde -- added -f made verbose display optional with -v
 *
 * This program will take a kline.conf and remove duplicate and redundant 
 * entries from it, creating a kline.conf.parsed file
 * $Id: klineParse.c,v 1.1.1.1 2006/03/08 23:28:14 malign Exp $
 */

#define kParseVer "2.12"

#define KLINEFILE "kline.conf"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>

typedef struct {
    char userid[15];
    char domain[128];
    char klinedata[256];
    char valid;
    struct KlineList *next;
    struct KlineList *previous;
} KlineList;


KlineList *AddKline (KlineList *, KlineList *);
KlineList *RemoveKline (KlineList *);
KlineList *Read_Kline_File (char *);
int        Array_Search (KlineList *, KlineList *, short);
int        RemoveDupes (char *, char *);
int        RemoveRedundant (char *, char *, short);
int        strToLower(unsigned char *);
void       ClearKlines (KlineList * );

int main (int argc, char *argv[]) {
    
    char *parsedFile, *klineFile, appName[128];
    short dupeCheck, altInput, altOutput, showMatches;
    
    dupeCheck = altInput = altOutput = showMatches =0;
    memset(appName,'\0',128);    
    
    if (argc > 1) {
        if (argv[1][0] == '-') {
        
            if (strspn(argv[1],"-dvf\0") != strlen(argv[1])) {
                goto paramReturn;
            }
            if (strchr(argv[1], 'f')) {
                if (argc > 3) {
                    parsedFile = (char *)  malloc(strlen(argv[3])+1);
                    strcpy(parsedFile,argv[3]);
                    altOutput =1;
                }
                if (argc >2) {
                    klineFile = (char *)  malloc(strlen(argv[2])+1);
                    strcpy(klineFile,argv[2]);
                    altInput =1;
                }
            }
            
            if (strchr(argv[1], 'v')) {
                showMatches = 1;
            }
            
            if (strchr (argv[1], 'd')) {
                dupeCheck = 1;
            }
        } else {
            goto paramReturn;
        }
    }

    if (!altOutput) {
        parsedFile = (char *)  malloc(strlen(KLINEFILE)+8);
        strcpy(parsedFile,KLINEFILE);
        strcat(parsedFile,".parsed");
    }
    if (!altInput) {
        klineFile = (char *)  malloc(strlen(KLINEFILE));
        strcpy(klineFile,KLINEFILE);
    }
    if (dupeCheck) {
        if (!RemoveDupes(klineFile,"kline.conf.parsed.tmp")) { 
            RemoveRedundant("kline.conf.parsed.tmp",parsedFile, showMatches);
            remove("kline.conf.parsed.tmp");
        } else {
            printf("\n");
            goto paramReturn;
        }
    } else {
        if (RemoveRedundant(klineFile,parsedFile, showMatches)) {
            printf("\n");
            goto paramReturn;
        }
    }
    

    printf("\nklineParse %s has finished mangling %s.\n",kParseVer,
        klineFile);
    printf("The new version has been saved as %s.\n",parsedFile);
    printf("It is a good idea to diff the two files to ensure no insane "
        "changes were made.\n\n");

    free(parsedFile);
    free(klineFile);

    return (EXIT_SUCCESS);
    
    paramReturn:
    if (strchr(argv[0],'/')) {
        strcat(appName, strrchr(argv[0],'/')+1);
    } else {
        strcpy(appName,argv[0]);
    }
    printf ("usage: %s [-dfv] [<input file>] [<output file>]\n",
        appName);
    printf (" -d remove duplicate consecutive klines\n");
    printf (" -v verbose display during redundant search\n");
    printf (" -f use alterate file names, instead of kline.conf and ");
    printf ("kline.conf.parsed\n ");
    if (altOutput) {
        free(parsedFile);
    }
    if (altInput) {
        free(klineFile);
    }
    return (EXIT_FAILURE);

}

/*
 * This function will search out klines that are redundant to wider matching 
 * klines.  In other words, if you had previously klined *userid@*domain.com, 
 * *@machine.domain.com, ~*@*.domain.com, and then finally *@*domain.com, this
 * code will remove all the klines except *@*domain.com.
 *
 */


int RemoveRedundant (char *KlineFile, char *parsedFile, short showMatches) {
    KlineList      *pKlines, anotherKline, *pKlineList;
    FILE           *outFile;
    
    pKlines = NULL;
    pKlines = Read_Kline_File(KlineFile);
    if (pKlines == NULL) {
        return(EXIT_FAILURE);
    }
    pKlineList = pKlines;
    while (pKlines != NULL) {
        if (pKlines->valid == 1 && (!strcmp(pKlines->userid,"*") && 
                pKlines->domain[0] == '*') || !strcmp(pKlines->domain,"*") ||
                (isdigit(pKlines->domain[0]) && strrchr(pKlines->domain,'*'))){
            pKlines->valid == 3;
            Array_Search(pKlines,pKlineList, showMatches);
            pKlines->valid == 1;
        }
        pKlines = (KlineList *) pKlines->next;
    }
    pKlines = pKlineList;
    
    outFile = fopen(parsedFile, "w+");
    if (outFile == NULL) {
        printf("Could not open %s for writing.\n",parsedFile);
        return EXIT_FAILURE;
    }
    
    while (pKlines != NULL) {
        if (pKlines->valid >= 1) {
            fputs(pKlines->klinedata, outFile);
    
        }
        pKlines = (KlineList *) pKlines->next;
        
    }
    fclose(outFile);
    
    pKlines = pKlineList;
    ClearKlines (pKlines);
    return (EXIT_SUCCESS);
}


/* This was the original (1.0) code, which has now be reduced to a command line
 * option, since this problem does not appear to be widespread.  You only need
 * to use this if the the following two commands don't return the same value, or
 * if when doing an unkline on the server you see (2 matches) or some number
 * other than 1.  This procedure only removes the duplicates if they are 
 * consequtive.
 *
 *        grep K: kline.conf | sort -n | uniq | wc -l
 *        grep K: kline.conf | wc -l
 */

int RemoveDupes (char *KlineFile, char *parsedFile) {

    FILE *inFile, *outFile;
    char sLine1[512], sLine2[512];
    char domain1[128],domain2[128],userid1[15],userid2[15];
    
    memset(sLine1,'\0',512);
    memcpy(sLine2,sLine1,512);
    memset(domain1,'\0',128);
    memcpy(domain2,domain1,128);
    memset(userid1,'\0',15);
    memcpy(userid2,userid1,15);
    
    inFile = fopen(KlineFile, "r+");
    if (inFile == NULL) {
        printf("No %s found\n",KlineFile);
        return EXIT_FAILURE;
    }
    outFile = fopen(parsedFile, "w+");
    if (outFile == NULL) {
        fclose(inFile);
        printf("Could not open %s.parsed for writing.\n",KlineFile);
        return EXIT_FAILURE;
    }


    if (fgets(sLine1,512,inFile) == NULL) {
        fclose(inFile);
        printf("%s is empty.\n",KlineFile);
        return EXIT_FAILURE;
    }
    
    while (!feof(inFile)) {
        fgets(sLine2,512,inFile);
        
        if (sLine1[0] == 'K' && sLine2[0] == 'K') {
            strncat(domain1, sLine1+2, strchr(sLine1+2,':')-sLine1-2);
            strcat(userid1, strrchr(sLine1,':')+1);
            strncat(domain2, sLine2+2, strchr(sLine2+2,':')-sLine2-2);
            strcat(userid2, strrchr(sLine2,':')+1);
            if (strcmp(domain1,domain2) != 0 || strcmp(userid1,userid2) != 0) {
                fputs(sLine1, outFile);
            }
            domain1[0] = userid1[0] = domain2[0] = userid2[0] = '\0';
        } else {
            fputs(sLine1, outFile);
        }
        strcpy(sLine1, sLine2);
        memset(sLine2,'\0',512);        
    }
    fputs(sLine2, outFile);
    fclose(inFile);
    fclose(outFile);
    
    return EXIT_SUCCESS;
}

KlineList *Read_Kline_File (char *KlineFile) {

    KlineList *pNewKlines, aKline;
    
    FILE *inFile, *outFile;
    char sLine1[256], sLine2[256];
    char domain1[128],domain2[128],userid1[15],userid2[15];
    char *endOffset;
    
    pNewKlines = NULL;

    memset(sLine1,'\0',256);
    memcpy(sLine2,sLine1,256);
    memset(domain1,'\0',128);
    memcpy(domain2,domain1,128);
    memset(userid1,'\0',15);
    memcpy(userid2,userid1,15);
    
    inFile = fopen(KlineFile, "r+");
    if (inFile == NULL) {
        printf("No %s found\n",KlineFile);
        return NULL;
    }
    
    while (!feof(inFile)) {
        fgets(sLine1,256,inFile);
        memset(&aKline,'\0',sizeof(aKline));
        if (sLine1[0] == 'K') {
            strncat(aKline.domain, sLine1+2, strchr(sLine1+2,':')-sLine1-2);
            strToLower(aKline.domain);
            endOffset = strrchr(sLine1,':')+1;
            strncat(aKline.userid, endOffset, strrchr(sLine1,'\n')-endOffset);
            strToLower(aKline.userid);
            /* the kline.conf I developed this for had about 60 *~*@ klines...
             * This will change them to ~* for matching purposes only, so they
             * may well survive if you don't have ~*@* klined.
             */
            if(!strcmp(aKline.userid,"*~*")) {
                strcpy(aKline.userid,"~*");
            }
            strcpy(aKline.klinedata,sLine1);
            aKline.valid = 1;
            aKline.next = NULL;
            aKline.previous = NULL;
            pNewKlines = AddKline(pNewKlines,&aKline);
        } else {
            memset(aKline.userid,'\0',15);
            memset(aKline.domain,'\0',128);
            strcpy(aKline.klinedata,sLine1);
            aKline.valid = 2;
            aKline.next = NULL;
            aKline.previous = NULL;
            pNewKlines = AddKline(pNewKlines,&aKline);
        }
        memset(sLine1,'\0',256);        
    }
    fclose(inFile);
    return pNewKlines;

}

int Array_Search (KlineList *pTargKline, KlineList *pKline, short showMatches) {
    
    KlineList *pKlineTop = pKline;
    char stuff[128], stuff2[128];
    while (pKline != NULL) {
        if (pKline->valid == 1) {
            if (Tcl_StringMatch(pKline->domain,pTargKline->domain) && 
                    Tcl_StringMatch(pKline->userid,pTargKline->userid) &&
                    strcmp(pTargKline->klinedata,pKline->klinedata)) {
                pKline->valid = 0;
                if (showMatches) {
                    printf("%s@%s ",strcpy(stuff,pKline->userid),
                        strcpy(stuff2,pKline->domain));
                    printf("is redundant to %s@%s\n",strcpy(stuff,
                        pTargKline->userid),strcpy(stuff2,pTargKline->domain));
                }
                if (pKline->previous != NULL) {
                    pKline = (KlineList *) pKline->previous;
                    if (pKline->klinedata[0] == '#') {
                        pKline->valid = 0;
                    }
                    pKline = (KlineList *) pKline->next;
                }
            } else if (Tcl_StringMatch(pTargKline->domain,pKline->domain) && 
                    Tcl_StringMatch(pTargKline->userid,pKline->userid) &&
                    strcmp(pTargKline->klinedata,pKline->klinedata)) {
                pTargKline->valid = 0;
                if (showMatches) {
                    printf("%s@%s ",strcpy(stuff,pTargKline->userid),
                        strcpy(stuff2,pTargKline->domain));
                    printf("is redundant to %s@%s\n",strcpy(stuff,
                        pKline->userid),strcpy(stuff2,pKline->domain));
                }
                Array_Search(pKline,pKlineTop, showMatches);
            }   
        }
        pKline = (KlineList *) pKline->next; 
    }        
} 

KlineList *AddKline (KlineList *pKline, KlineList *pKlineData) {

     KlineList *pCurrentKline, *pOrigKline = pKline;

    if (pKline != NULL) {
        if (pKline->previous != NULL) {
            pKline = (KlineList *) pKline->previous;
        }
        pCurrentKline =  pKline;
        pKline->next = (struct KlineList  *) malloc (sizeof (KlineList));
        pKline = (KlineList *) pKline->next;
        pKline->next = NULL;
        *pKline = *pKlineData;
        pKline->previous =  (struct KlineList *) pCurrentKline;
        pOrigKline->previous = (struct KlineList *) pKline;
        return pOrigKline;
    }
    else {
        pKline = ( KlineList  *) malloc (sizeof (KlineList));
        pKline->next = NULL;
        pKline->previous = NULL;
        *pKline = *pKlineData;
        return pKline;
    }
}

KlineList *RemoveKline (KlineList *pKline) {

    KlineList *tempp;
    tempp = (KlineList *) pKline->next;
    free (pKline);
    return tempp;
}

void ClearKlines (KlineList * listpointer) {

    while (listpointer != NULL) {
        listpointer = RemoveKline (listpointer);
    }
}


int strToLower(unsigned char *aString) {
    
    short i =0;
    
    while (aString[i]) {   
        if (isupper(aString[i])) {
            aString[i] = tolower(aString[i]);
        }
        i++;
    } 
    return 1;
}

/* The following function is taken from the TCL soure code distribution,
 * specifically the file tclUtil.c, I did this because most ircd boxes do
 * not have TCL installed, and it's likely they don't want to install it.
 *
 * Copyright (c) 1987-1993 The Regents of the University of California.
 * Copyright (c) 1994-1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * The file "license.terms" can be found in the tcl source code distribution
 * at ftp://ftp.scriptics.com/pub/tcl/tcl7_6/tcl7.6p2.tar.gz
 *
 */

int 
Tcl_StringMatch(string, pattern)
    char *string;	/* String. */
    char *pattern;	/* Pattern, which may contain
			 * special characters. */
{
    char c2;

    while (1) {
	/* See if we're at the end of both the pattern and the string.
	 * If so, we succeeded.  If we're at the end of the pattern
	 * but not at the end of the string, we failed.
	 */
	
	if (*pattern == 0) {
	    if (*string == 0) {
		return 1;
	    } else {
		return 0;
	    }
	}
	if ((*string == 0) && (*pattern != '*')) {
	    return 0;
	}

	/* Check for a "*" as the next pattern character.  It matches
	 * any substring.  We handle this by calling ourselves
	 * recursively for each postfix of string, until either we
	 * match or we reach the end of the string.
	 */
	
	if (*pattern == '*') {
	    pattern += 1;
	    if (*pattern == 0) {
		return 1;
	    }
	    while (1) {
		if (Tcl_StringMatch(string, pattern)) {
		    return 1;
		}
		if (*string == 0) {
		    return 0;
		}
		string += 1;
	    }
	}
    
	/* Check for a "?" as the next pattern character.  It matches
	 * any single character.
	 */

	if (*pattern == '?') {
	    goto thisCharOK;
	}

	/* Check for a "[" as the next pattern character.  It is followed
	 * by a list of characters that are acceptable, or by a range
	 * (two characters separated by "-").
	 */
	
	if (*pattern == '[') {
	    pattern += 1;
	    while (1) {
		if ((*pattern == ']') || (*pattern == 0)) {
		    return 0;
		}
		if (*pattern == *string) {
		    break;
		}
		if (pattern[1] == '-') {
		    c2 = pattern[2];
		    if (c2 == 0) {
			return 0;
		    }
		    if ((*pattern <= *string) && (c2 >= *string)) {
			break;
		    }
		    if ((*pattern >= *string) && (c2 <= *string)) {
			break;
		    }
		    pattern += 2;
		}
		pattern += 1;
	    }
	    while (*pattern != ']') {
		if (*pattern == 0) {
		    pattern--;
		    break;
		}
		pattern += 1;
	    }
	    goto thisCharOK;
	}
    
	/* If the next pattern character is '/', just strip off the '/'
	 * so we do exact matching on the character that follows.
	 */
	
	if (*pattern == '\\') {
	    pattern += 1;
	    if (*pattern == 0) {
		return 0;
	    }
	}

	/* There's no special character.  Just make sure that the next
	 * characters of each string match.
	 */
	
	if (*pattern != *string) {
	    return 0;
	}

	thisCharOK: pattern += 1;
	string += 1;
    }
}
