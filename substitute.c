/* $Id: substitute.c,v 2.2 1998/10/11 18:36:36 jku Exp $ */
/* Autoconf patching by David Hedbor, neotron@lysator.liu.se */
/*********************************************************************/
/* file: substitute.c - functions related to the substitute command  */
/*                             TINTIN III                            */
/*          (T)he K(I)cki(N) (T)ickin D(I)kumud Clie(N)t             */
/*                     coded by peter unold 1992                     */
/*********************************************************************/
#include "config.h"
#ifdef HAVE_STRING_H
#include <string.h>
#else
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#endif
#include "tintin.h"

extern char *get_arg_in_braces(char *s,char *arg,int flag);
extern struct listnode *search_node_with_wild(struct listnode *listhead, char *cptr);
extern struct listnode *searchnode_list(struct listnode *listhead, char *cptr);
extern int check_one_action(char *line, char *action, pvars_t *vars, int inside, struct session *ses);
extern void deletenode_list(struct listnode *listhead, struct listnode *nptr);
extern void insertnode_list(struct listnode *listhead, char *ltext, char *rtext, char *prtext, int mode);
extern void prepare_actionalias(char *string, char *result, struct session *ses);
extern void check_all_promptactions(char *line, struct session *ses);
extern void prompt(struct session *ses);
extern void show_list(struct listnode *listhead);
extern void shownode_list(struct listnode *nptr);
extern void tintin_printf(struct session *ses, char *format, ...);
extern void substitute_vars(char *arg, char *result);
extern void substitute_myvars(char *arg,char *result,struct session *ses);

extern pvars_t *pvars;
extern int subnum;
extern int mesvar[];
extern char *match_start,*match_end;

/***************************/
/* the #substitute command */
/***************************/
void parse_sub(char *arg,int gag,struct session *ses)
{
    char left[BUFFER_SIZE], right[BUFFER_SIZE];
    struct listnode *mysubs, *ln;
    int flag=0;

    mysubs = ses->subs;
    arg = get_arg_in_braces(arg, left, 0);
    arg = get_arg_in_braces(arg, right, 1);

    if (!*left && !*right)
        strcpy(left, "*");
    if (!*right)
    {
        while ((mysubs = search_node_with_wild(mysubs, left)) != NULL)
            if (gag)
            {
                if (!strcmp(mysubs->right, EMPTY_LINE))
                {
                    if (!flag)
                        tintin_printf(ses,"#THESE GAGS HAVE BEEN DEFINED:");
                    tintin_printf(ses, "{%s~7~}", mysubs->left);
                    flag=1;
                }
            }
            else
            {
                if (!flag)
                    tintin_printf(ses,"#THESE SUBSTITUTES HAVE BEEN DEFINED:");
                flag=1;
                shownode_list(mysubs);
            }
        if (!flag && mesvar[2])
            if (strcmp(left,"*"))
                tintin_printf(ses, "#THAT %s IS NOT DEFINED.", gag? "GAG":"SUBSTITUTE");
            else
                tintin_printf(ses, "#NO %sS HAVE BEEN DEFINED.", gag? "GAG":"SUBSTITUTE");
        prompt(ses);
    }
    else
    {
        if ((ln = searchnode_list(mysubs, left)) != NULL)
            deletenode_list(mysubs, ln);
        insertnode_list(mysubs, left, right, 0, ALPHA);
        subnum++;
        if (mesvar[2])
        {
            if (strcmp(right, EMPTY_LINE))
                tintin_printf(ses, "#Ok. {%s} now replaces {%s}.", right, left);
            else
                tintin_printf(ses, "#Ok. {%s} is now gagged.", left);
        }
    }
}


/*****************************/
/* the #unsubstitute command */
/*****************************/
void unsubstitute_command(char *arg,int gag,struct session *ses)
{
    char left[BUFFER_SIZE];
    struct listnode *mysubs, *ln, *temp;
    int flag;

    flag = FALSE;
    mysubs = ses->subs;
    temp = mysubs;
    arg = get_arg_in_braces(arg, left, 1);
    while ((ln = search_node_with_wild(temp, left)) != NULL)
    {
        if (gag && strcmp(ln->right,EMPTY_LINE))
        {
            temp=ln;
            continue;
        }
        if (mesvar[2])
        {
            if (!strcmp(ln->right,EMPTY_LINE))
                tintin_printf(ses, "#Ok. {%s} is no longer gagged.", ln->left);
            else
                tintin_printf(ses, "#Ok. {%s} is no longer substituted.", ln->left);
        }
        deletenode_list(mysubs, ln);
        flag = TRUE;
        /*  temp=ln; */
    }
    if (!flag && mesvar[2])
        tintin_printf(ses,"#THAT SUBSTITUTE (%s) IS NOT DEFINED.",left);
}

#define APPEND(srch)    if (rlen+len > BUFFER_SIZE-1)           \
                            len=BUFFER_SIZE-1-rlen;             \
                        memcpy(result+rlen,srch,len);               \
                        rlen+=len;

void do_all_sub(char *line, struct session *ses)
{
    struct listnode *ln;
    pvars_t vars,*lastpvars;
    char result[BUFFER_SIZE],tmp1[BUFFER_SIZE],tmp2[BUFFER_SIZE],*l;
    int rlen,len;

    lastpvars=pvars;
    pvars=&vars;

    ln = ses->subs;

    while ((ln = ln->next))
        if (check_one_action(line, ln->left, &vars, 0, ses))
        {
            if (!strcmp(ln->right, EMPTY_LINE))
            {
                strcpy(line, EMPTY_LINE);
                return;
            };
            substitute_vars(ln->right, tmp1);
            substitute_myvars(tmp1, tmp2, ses);
            rlen=match_start-line;
            memcpy(result, line, rlen);
            len=strlen(tmp2);
            APPEND(tmp2);
            while (*match_end)
                if (check_one_action(l=match_end, ln->left, &vars, 1, ses))
                {
                    /* no gags possible here */
                    len=match_start-l;
                    APPEND(l);
                    substitute_vars(ln->right, tmp1);
                    substitute_myvars(tmp1, tmp2, ses);
                    len=strlen(tmp2);
                    APPEND(tmp2);
                }
                else
                {
                    len=strlen(l);
                    APPEND(l);
                    break;
                }
            memcpy(line, result, rlen);
            line[rlen]=0;
        }

    pvars=lastpvars;
}
