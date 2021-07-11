#include "tintin.h"
#include "protos/globals.h"
#include "protos/print.h"
#include "protos/misc.h"
#include "protos/parse.h"
#include "protos/utils.h"
#include "protos/hooks.h"


const int rgbbgr[8]={0,4,2,6,1,5,3,7};

static enum {MUDC_OFF, MUDC_ON, MUDC_NULL, MUDC_NULL_WARN} mudcolors=MUDC_NULL_WARN;
static char *MUDcolors[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

static int getco(const char *restrict txt, const char **err)
{
    if (!isadigit(txt[1]))
    {
        *err=txt+1;
        return txt[0]-'0';
    }

    if (!isadigit(txt[2]))
    {
        *err=txt+2;
        int c = (txt[0]-'0')*10+txt[1]-'0';
        if (c >= 16)
            return c - 16 + 232; /* 24-level color ramp */
        return c;
    }

    if (!isadigit(txt[3]))
    {
        if (txt[0]<'6' && txt[1]<'6' && txt[2]<'6')
        {
            *err=txt+3;
            return 16 + 36*(txt[0]-'0') + 6*(txt[1]-'0') + (txt[2]-'0');
        }
    }

    return -1U>>1;
}

int getcolor(const char *restrict*restrict ptr, int *restrict color, bool allow_minus_token)
{
    unsigned fg, bg, blink;
    const char *txt=*ptr;

    if (*(txt++)!='~')
        return 0;
    if (allow_minus_token&&(*txt=='-')&&(*(txt+1)=='1')&&(*(txt+2)=='~'))
    {
        *color=-1;
        *ptr+=3;
        return 1;
    }
    if (isadigit(*txt))
    {
        const char *err;
        fg=getco(txt, &err);
        if (fg > CFG_MASK)
            return 0;
        txt=err;
    }
    else if (*txt==':')
        fg=(*color==-1)? 7 : ((*color)&CFG_MASK);
    else
        return 0;
    if (*txt=='~')
    {
        *color=fg;
        *ptr=txt;
        return 1;
    }
    if (*txt!=':')
        return 0;
    if (isadigit(*++txt))
    {
        const char *err;
        bg=getco(txt, &err);
        if (bg > CBG_MAX)
            return 0;
        txt=err;
    }
    else
        bg=(*color==-1)? 0 : ((*color&CBG_MASK)>>CBG_AT);
    if (*txt=='~')
    {
        *color=bg << CFG_BITS | fg;
        *ptr=txt;
        return 1;
    }
    if (*txt!=':')
        return 0;
    if (isadigit(*++txt))
    {
        char *err;
        blink=strtol(txt, &err, 10);
        if (blink > CFL_MAX)
            return 0;
        txt=err;
    }
    else
        blink=(*color==-1)? 0 : (*color >> CFL_AT);
    if (*txt!='~')
        return 0;
    *color=blink << CFL_AT | bg << CBG_AT | fg;
    *ptr=txt;
    return 1;
}

static int setco(char *txt, int c)
{
    if (c<10)
        return txt[0]=c+'0', 1;
    if (c<16)
        return txt[0]='1', txt[1]=c+'0'-10, 2;
    if (c>=232)
        return sprintf(txt, "%d", c+16-232);
    c-=16;
    txt[0]='0'+c/36;
    txt[1]='0'+(c/6)%6;
    txt[2]='0'+c%6;
    return 3;
}

int setcolor(char *txt, int c)
{
    if (c==-1)
        return sprintf(txt, "~-1~");
    char *txt0 = txt;
    *txt++='~';
    txt+=setco(txt, c&CFG_MASK);
    if (c < 1<<CBG_AT)
        return *txt++='~', *txt=0, txt-txt0;
    *txt++=':';
    txt+=setco(txt, (c&CBG_MASK)>>CBG_AT);
    if (c < 1<<CFL_AT)
        return *txt++='~', *txt=0, txt-txt0;
    return txt-txt0+sprintf(txt, ":%d~", c>>CFL_AT);
}

typedef unsigned char u8;
struct rgb { u8 r; u8 g; u8 b; };

static inline u8 ramp256_6(int i)
{
    /* 00 5f 87 af d7 ff */
    return i ? 55 + i * 40 : 0;
}

static struct rgb rgb_from_256(int i)
{
    struct rgb c;
    if (i < 8)
    {   /* Standard colours. */
        c.r = i&4 ? 0xaa : 0x00;
        c.g = i&2 ? 0xaa : 0x00;
        c.b = i&1 ? 0xaa : 0x00;
    }
    else if (i < 16)
    {
        c.r = i&4 ? 0xff : 0x55;
        c.g = i&2 ? 0xff : 0x55;
        c.b = i&1 ? 0xff : 0x55;
    }
    else if (i < 232)
    {   /* 6x6x6 colour cube. */
        c.r = ramp256_6((i - 16) / 36);
        c.g = ramp256_6((i - 16) / 6 % 6);
        c.b = ramp256_6((i - 16) % 6);
    }
    else/* Grayscale ramp. */
        c.r = c.g = c.b = i * 10 - 2312;
    return c;
}

static int rgb_to_16(struct rgb c)
{
    u8 fg, max = c.r;
    if (c.g > max)
        max = c.g;
    if (c.b > max)
        max = c.b;
    fg = (c.r > max/2 + 32 ? 4 : 0)
       | (c.g > max/2 + 32 ? 2 : 0)
       | (c.b > max/2 + 32 ? 1 : 0);
    if (fg == 7 && max <= 0x70)
        return 8;
    else if (max > 0xc0)
        return fg+8;
    else
        return fg;
}

static int sqrd(unsigned char a, unsigned char b)
{
    int _ = ((int)a) - ((int)b);
    return _>=0?_:-_;
}

// Riermersma's formula
static uint32_t rgb_diff(struct rgb x, struct rgb y)
{
    int rm = (x.r+y.r)/2;
    return 2*sqrd(x.r, y.r)
         + 4*sqrd(x.g, y.g)
         + 3*sqrd(x.b, y.b)
         + rm*(sqrd(x.r, y.r)-sqrd(x.b, y.b))/256;
}

static uint32_t m6(uint32_t x)
{
    x = (x+5)/40;
    return x? x-1 : 0;
}

static uint32_t rgb_to_color_cube(struct rgb c)
{
    return 16 + m6(c.r)*36 + m6(c.g)*6 + m6(c.b);
}

static uint32_t rgb_to_grayscale_gradient(struct rgb c)
{
    int x = 232 + (c.r*2 + c.g*3 + c.b) / 60;
    return (x > 255)? 255 : x;
}

static int rgb_to_256(struct rgb c)
{
    uint32_t c1 = rgb_to_16(c);
    uint32_t bd = rgb_diff(c, rgb_from_256(c1));
    uint32_t res = c1;

    uint32_t c2 = rgb_to_color_cube(c);
    uint32_t d = rgb_diff(c, rgb_from_256(c2));
    if (d < bd)
        bd = d, res = c2;

    uint32_t c3 = rgb_to_grayscale_gradient(c);
    d = rgb_diff(c, rgb_from_256(c3));
    if (d < bd)
        res = c3;
    return res;
}

#define MAXTOK 16

void do_in_MUD_colors(char *txt, bool quotetype, struct session *ses)
{
    static int ccolor=7;
    /* worst case: buffer full of FormFeeds, with color=1023 */
    /* TODO: not anymore, it's much shorter now */
    char OUT[BUFFER_SIZE*20], *out, *back, *TXT=txt;
    unsigned int tok[MAXTOK], nt;
    int dummy=0;

    for (out=OUT;*txt;txt++)
        switch (*txt)
        {
        case 27:
            if (*(txt+1)=='[')
            {
                back=txt++;
                if (*(txt+1)=='?')
                {
                    txt++;
                    txt++;
                    while (*txt==';'||(*txt>='0'&&*txt<='9'))
                        txt++;
                    break;
                }

                tok[0]=nt=0;
again:
                switch (*++txt)
                {
                case 0:
                    goto error;
                case ';':
                    if (++nt==MAXTOK)
                        goto error;
                    tok[nt]=0;
                    goto again;
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                    tok[nt]=tok[nt]*10+*txt-'0';
                    goto again;
                case 'm':
                    if (*(txt-1)!='[')
                        nt++;
                    else
                        ccolor=7;
                    for (unsigned int i=0;i<nt;i++)
                        switch (tok[i])
                        {
                        case 0:
                            ccolor=7;
                            break;
                        case 1:
                            ccolor|=8;
                            break;
                        case 2:
                            ccolor=(ccolor&~CFG_MASK)|8;
                            break;
                        case 3:
                            ccolor|=CFL_ITALIC;
                            break;
                        case 4:
                            ccolor|=CFL_UNDERLINE;
                            break;
                        case 5:
                            ccolor|=CFL_BLINK;
                            break;
                        case 7:
                            ccolor=(ccolor&~(CFG_MASK|CBG_MASK))
                                | (ccolor&CBG_MASK)>>CBG_AT
                                | (ccolor&CFG_MASK)<<CBG_AT;
                            /* inverse should propagate... oh well */
                            break;
                        case 9:
                            ccolor|=CFL_STRIKETHRU;
                            break;
                        case 21:
                            if ((ccolor&CFG_MASK) < 16)
                                ccolor&=~8;
                            break;
                        case 22:
                            if ((ccolor&CFG_MASK) < 16)
                            {
                                ccolor&=~8;
                                if (!(ccolor&CBG_MASK))
                                    ccolor|=7;
                            }
                            break;
                        case 23:
                            ccolor&=~CFL_ITALIC;
                            break;
                        case 24:
                            ccolor&=~CFL_UNDERLINE;
                            break;
                        case 25:
                            ccolor&=~CFL_BLINK;
                            break;
                        case 29:
                            ccolor&=~CFL_STRIKETHRU;
                            break;
                        case 38:
                            i++;
                            if (i>=nt)
                                break;
                            if (tok[i]==5 && i+1<nt)
                            {   /* 256 colours */
                                i++;
                                int k = tok[i];
                                if (k < 256)
                                {
                                    if (k < 16)
                                        k = (k&8) | rgbbgr[k&7];
                                    ccolor=(ccolor&~CFG_MASK) | k;
                                }
                            }
                            else if (tok[i]==2 && i+3<nt)
                            {   /* 24 bit */
                                struct rgb c =
                                {
                                    .r = tok[i+1],
                                    .g = tok[i+2],
                                    .b = tok[i+3],
                                };
                                ccolor=(ccolor&~CFG_MASK) | rgb_to_256(c);
                                i+=3;
                            }
                            /* Subcommands 3 (CMY) and 4 (CMYK) are so insane
                             * there's no point in supporting them.
                             */
                            break;
                        case 48:
                            i++;
                            if (i>=nt)
                                break;
                            if (tok[i]==5 && i+1<nt)
                            {   /* 256 colours */
                                i++;
                                int k = tok[i];
                                if (k < 256)
                                {
                                    if (k < 16)
                                        k = (k&8) | rgbbgr[k&7];
                                    ccolor=(ccolor&~CBG_MASK) | k<<CBG_AT;
                                }
                            }
                            else if (tok[i]==2 && i+3<nt)
                            {   /* 24 bit */
                                struct rgb c =
                                {
                                    .r = tok[i+1],
                                    .g = tok[i+2],
                                    .b = tok[i+3],
                                };
                                ccolor=(ccolor&~CBG_MASK) | rgb_to_256(c)<<CBG_AT;
                                i+=3;
                            }
                            break;
                        case 39:
                            ccolor&=~CFG_MASK;
                            ccolor|=7;
                            break;
                        case 49:
                            ccolor&=~CBG_MASK;
                            break;
                        default:
                            if (tok[i]>=30 && tok[i]<38)
                                ccolor=(ccolor&~0x07)|rgbbgr[tok[i]-30];
                            else if (tok[i]>=40 && tok[i]<48)
                                ccolor=(ccolor&~CBG_MASK)|rgbbgr[tok[i]-40]<<CBG_AT;
                            else if (tok[i]>=90 && tok[i]<98)
                                ccolor=(ccolor&~0x07)|8|rgbbgr[tok[i]-90];
                            else if (tok[i]>=100 && tok[i]<108)
                                ccolor=(ccolor&~CBG_MASK)|(rgbbgr[tok[i]-100]|8)<<CBG_AT;
                            /* ignore unknown attributes */
                        }
                    out+=setcolor(out, ccolor);
                    break;
                case 'C':
                    if (out-OUT+tok[0]>INPUT_CHUNK*2)
                        break;       /* something fishy is going on */
                    for (unsigned int i=0;i<tok[0];i++)
                        *out++=' ';
                    break;
                case 'D': /* this interpretation is badly invalid... */
                case 'K':
                    out=OUT;
                    out+=setcolor(out, ccolor);
                    break;
                case 'J':
                    if (tok[0])
                        *out++=12; /* Form Feed */
                    break;
                default:
error:
                    txt=back;
                }
            }
            else if (*(txt+1)=='%' && *(txt+2)=='G')
                txt+=2;
            else if (*(txt+1)==']')
            {
                txt+=2;
                if (!isadigit(*txt)) /* malformed */
                    break;
                nt=0;
                while (isadigit(*txt))
                    nt=nt*10+*txt++-'0';
                if (*txt!=';')
                    break;
                back=++txt;
                while (*txt && *txt!=7 && *txt!=27)
                    txt++;
                if (*txt==27)
                    *txt++=0;
                else if (*txt==7)
                    *txt=0;
                else
                    break;
                switch (nt)
                {
                case 0: /* set window title */
                    if (!ses)
                        break;
                    if (back-TXT<=ses->lastintitle)
                        break;
                    ses->lastintitle=back-TXT;
                    do_hook(ses, HOOK_TITLE, back, true);
                }
            }
            break;
        case '~':
            back=txt;
            if (getcolor((const char**)&txt, &dummy, 1))
            {
                if (quotetype)
                {
                    *out++='~';
                    *out++='~';
                    *out++=':';
                    *out++='~';
                }
                else
                    *out++='`';
                back++;
                while (back<txt)
                    *out++=*back++;
                *out++=quotetype?'~':'`';
                break;
            }
        default:
            *out++=*txt;
        }
    if (out-OUT>=BUFFER_SIZE) /* can happen only if there's a lot of FFs */
        out=OUT+BUFFER_SIZE-1;
    *out=0;
    strcpy(TXT, OUT);
}

void do_out_MUD_colors(char *line)
{
    char buf[BUFFER_SIZE], *txt=buf;
    int c=7;

    if (!mudcolors)
        return;
    for (char *pos=line;*pos;pos++)
    {
        if (*pos=='~')
            if (getcolor((const char**)&pos, &c, 0))
                goto color;
        *txt++=*pos;
        continue;
color:
        switch (mudcolors)
        {
        case MUDC_OFF:
            abort();
        case MUDC_NULL_WARN:
            tintin_printf(0, "#Warning: no color codes set, use #mudcolors");
            mudcolors=MUDC_NULL;
        case MUDC_NULL:
            break;
        case MUDC_ON:;
            int k = rgb_to_16(rgb_from_256(c&CFG_MASK));
            strcpy(txt, MUDcolors[k]);
            txt+=strlen(MUDcolors[k]);
        }
    }
    *txt=0;
    strcpy(line, buf);
}

/**************************/
/* the #mudcolors command */
/**************************/
void mudcolors_command(const char *arg, struct session *ses)
{
    char cc[BUFFER_SIZE][16], buf[BUFFER_SIZE];

    if (!*arg)
    {
error_msg:
        tintin_eprintf(ses, "#ERROR: valid syntax is: #mudcolors OFF, #mudcolors {} or #mudcolors {c0} {c1} ... {c15}");
        return;
    }
    if (!yes_no(arg))
    {
        mudcolors=MUDC_OFF;
        tintin_printf(ses, "#outgoing color codes (~n~) are now sent verbatim.");
        return;
    }
    if (!*get_arg_in_braces(arg, buf, 0))
    {
        arg=buf;
        if (!*arg)
            goto null_codes;
    }
    /* we allow BOTH {a} {b} {c} _and_ {{a} {b} {c}} - inconsistency, but it's ok */
    for (int nc=0;nc<16;nc++)
    {
        if (!*arg)
        {
            if ((nc==1)&&!*cc[0])
            {
null_codes:
                mudcolors=MUDC_NULL;
                tintin_printf(ses, "#outgoing color codes are now ignored.");
                return;
            }
            else
                goto error_msg;
        }
        arg=get_arg_in_braces(arg, cc[nc], 0);
    }
    if (*arg)
        goto error_msg;
    mudcolors=MUDC_ON;
    for (int nc=0;nc<16;nc++)
    {
        SFREE(MUDcolors[nc]);
        MUDcolors[nc]=mystrdup(cc[nc]);
    }
    tintin_printf(ses, "#outgoing color codes table initialized");
}

char *ansicolor(char *s, int c)
{
    *s++=27, *s++='[', *s++='0';
    int k = c & CFG_MASK;
    if (k < 16)
    {
        if (!(k&8))
            *s++=';', *s++='3';
        else if (bold)
            *s++=';', *s++='1', *s++=';', *s++='3';
        else
            *s++=';', *s++='9';
        *s++='0'+rgbbgr[k&7];
    }
    else
        s+=sprintf(s, ";38;5;%d", k);
    k = (c & CBG_MASK) >> CBG_AT;
    if (k < 16)
    {
        if (k&8)
            *s++=';', *s++='1', *s++='0';
        else
            *s++=';', *s++='4';
        *s++='0'+rgbbgr[k&7];
    }
    else
        s+=sprintf(s, ";48;5;%d", k);
    if (c>>=CFL_AT)
    {
        if (c&C_BLINK)
            *s++=';', *s++='5';
        if (c&C_ITALIC)
            *s++=';', *s++='3';
        if (c&C_UNDERLINE)
            *s++=';', *s++='4';
        if (c&C_STRIKETHRU)
            *s++=';', *s++='9';
    }
    *s++='m';
    return s;
}
