﻿/*******************************************************************************
* HISTORY***********************************************************************
* 16/10/2019  |                                             | Daich
* Description: package some _hal function
*******************************************************************************/
#include "rtcm.h"

/*--------------------------------------------------------*/

#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "rtklib_core.h"

#ifndef WIN32
//#ifdef INT_SEC_SEND
	//#include "main.h"
//#endif
#endif

#define AS2R (D2R / 3600.0)    /* arc sec to radian */


/* set the default week numner for real-time system without a UTC time */
static uint16_t default_week_number = 2068;
static uint8_t dayofweek = 3;
static int GNSS_ID[11] = {
	_SYS_GPS_, _SYS_GLO_, _SYS_QZS_, _SYS_GAL_, _SYS_SBS_, _SYS_NONE_,
	_SYS_NONE_,_SYS_BDS_,_SYS_NONE_,_SYS_NONE_,_SYS_IRN_};
#ifdef INT_SEC_SEND
TIME_S sensor_time_s;
#endif

RTK_RAM_CODE extern void set_glo_frq(int prn, int frq)
{
	int default_glo_frq_table[30] = {
	1, -4, 05, 06, 01, -4, 05, 06, -2, -7, 00, -1, -2, -7, 00, -1,
	04, -3, 03, 02, 04, -3, 03, 02, 0, -5, -99, -99, -99, -99 };

	int max_prn;
	max_prn = sizeof(default_glo_frq_table) / sizeof(int);
    if (prn <= max_prn)
    {
        default_glo_frq_table[prn - 1] = frq;
    }
    return;
}

RTK_RAM_CODE extern int get_glo_frq(int prn)
{
	int default_glo_frq_table[30] = {
	1, -4, 05, 06, 01, -4, 05, 06, -2, -7, 00, -1, -2, -7, 00, -1,
	04, -3, 03, 02, 04, -3, 03, 02, 0, -5, -99, -99, -99, -99 };

	int max_prn;
	max_prn = sizeof(default_glo_frq_table) / sizeof(int);
    int frq = -99;
    if (prn <= max_prn)
    {
        frq = default_glo_frq_table[prn - 1];
    }
    return frq;
}

RTK_RAM_CODE extern void set_week_number(int week)
{
    default_week_number = week;
    return;
}

RTK_RAM_CODE extern int get_week_number()
{
    return default_week_number;
}


/* extract unsigned/signed bits ------------------------------------------------
* extract unsigned/signed bits from byte data
* args   : unsigned char *buff I byte data
*          int    pos    I      bit position from start of data (bits)
*          int    len    I      bit length (bits) (len<=32)
* return : extracted unsigned/signed bits
*-----------------------------------------------------------------------------*/
RTK_RAM_CODE unsigned int rtcm_getbitu(const unsigned char *buff, int pos, int len)
{
    unsigned int bits = 0;
    int i;
    for (i = pos; i < pos + len; i++)
        bits = (bits << 1) + ((buff[i / 8] >> (7 - i % 8)) & 1u);
    return bits;
}
RTK_RAM_CODE uint64_t rtcm_getbitu_64(const unsigned char *buff, int pos, int len)
{
	uint64_t bits = 0;
	int i;
	for (i = pos; i < pos + len; i++)
		bits = (bits << 1) + ((buff[i / 8] >> (7 - i % 8)) & 1u);
	return bits;
}
RTK_RAM_CODE int rtcm_getbits(const unsigned char *buff, int pos, int len)
{
    unsigned int bits = rtcm_getbitu(buff, pos, len);
    if (len <= 0 || 32 <= len || !(bits & (1u << (len - 1))))
        return (int)bits;
    return (int)(bits | (~0u << len)); /* extend sign */
}

/* get sign-magnitude bits ---------------------------------------------------*/
RTK_RAM_CODE static double getbitg(const unsigned char *buff, int pos, int len)
{
    double value = rtcm_getbitu(buff, pos + 1, len - 1);
    return rtcm_getbitu(buff, pos, 1) ? -value : value;
}
/* get signed 38bit field ----------------------------------------------------*/
RTK_RAM_CODE static double rtcm_getbits_38(const unsigned char *buff, int pos)
{
    return (double)rtcm_getbits(buff, pos, 32) * 64.0 + rtcm_getbitu(buff, pos + 32, 6);
}

typedef struct
{                               /* multi-signal-message header type */
    unsigned char iod;          /* issue of data station */
    unsigned char time_s;       /* cumulative session transmitting time */
    unsigned char clk_str;      /* clock steering indicator */
    unsigned char clk_ext;      /* external clock indicator */
    unsigned char smooth;       /* divergence free smoothing indicator */
    unsigned char tint_s;       /* soothing interval */
    unsigned char nsat, nsig;   /* number of satellites/signals */
    unsigned char sats[64];     /* satellites */
    unsigned char sigs[32];     /* signals */
    unsigned char cellmask[64]; /* cell mask */
} msm_h_t;

#define ROUND_U(x) ((unsigned int)floor((x) + 0.5))


/* ssr update intervals ------------------------------------------------------*/
static const double ssrudint[16] = {
    1, 2, 5, 10, 15, 30, 60, 120, 240, 300, 600, 900, 1800, 3600, 7200, 10800};

#define PRUNIT_GPS 299792.458     /* rtcm ver.3 unit of gps pseudorange (m) */
#define PRUNIT_GLO 599584.916     /* rtcm ver.3 unit of glonass pseudorange (m) */

/* msm signal id table -------------------------------------------------------*/
const char *rtcm_msm_sig_gps[32] = {
    /* GPS: ref [13] table 3.5-87, ref [14][15] table 3.5-91 */
    "", "1C", "1P", "1W", "1Y", "1M", "", "2C", "2P", "2W", "2Y", "2M", /*  1-12 */
    "", "", "2S", "2L", "2X", "", "", "", "", "5I", "5Q", "5X",         /* 13-24 */
    "", "", "", "", "", "1S", "1L", "1X"                                /* 25-32 */
};
const char *rtcm_msm_sig_glo[32] = {
    /* GLONASS: ref [13] table 3.5-93, ref [14][15] table 3.5-97 */
    "", "1C", "1P", "", "", "", "", "2C", "2P", "", "3I", "3Q",
    "3X", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", ""};
const char* msm_sig_gps[32] = {
	/* GPS: ref [13] table 3.5-87, ref [14][15] table 3.5-91 */
	""  ,"1C","1P","1W","1Y","1M",""  ,"2C","2P","2W","2Y","2M", /*  1-12 */
	""  ,""  ,"2S","2L","2X",""  ,""  ,""  ,""  ,"5I","5Q","5X", /* 13-24 */
	""  ,""  ,""  ,""  ,""  ,"1S","1L","1X"                      /* 25-32 */
};
const char* msm_sig_glo[32] = {
	/* GLONASS: ref [13] table 3.5-93, ref [14][15] table 3.5-97 */
	""  ,"1C","1P",""  ,""  ,""  ,""  ,"2C","2P",""  ,"3I","3Q",
	"3X",""  ,""  ,""  ,""  ,""  ,""  ,""  ,""  ,""  ,""  ,""  ,
	""  ,""  ,""  ,""  ,""  ,""  ,""  ,""
};
const char *msm_sig_gal[32] = {
    /* Galileo: ref [15] table 3.5-100 */
    "", "1C", "1A", "1B", "1X", "1Z", "", "6C", "6A", "6B", "6X", "6Z",
    "", "7I", "7Q", "7X", "", "8I", "8Q", "8X", "", "5I", "5Q", "5X",
    "", "", "", "", "", "", "", ""};
const char *msm_sig_qzs[32] = {
    /* QZSS: ref [15] table 3.5-103 */
    "", "1C", "", "", "", "", "", "", "6S", "6L", "6X", "",
    "", "", "2S", "2L", "2X", "", "", "", "", "5I", "5Q", "5X",
    "", "", "", "", "", "1S", "1L", "1X"};
const char *msm_sig_sbs[32] = {
    /* SBAS: ref [13] table 3.5-T+005 */
    "", "1C", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "5I", "5Q", "5X",
    "", "", "", "", "", "", "", ""};
const char *msm_sig_cmp[32] = {
	/* BeiDou: ref [15] table 3.5-106 */
	"", "1I", "1Q", "1X", ""  , ""  , "", "6I", "6Q", "6X", ""  , ""  ,
	"", "7I", "7Q", "7X", ""  , "5Q"  , "", ""  , ""  , "5I", "5Q", "5X",
	"", ""  , ""  , ""  , ""  , ""  , "", "" };

/* ssr signal and tracking mode ids ------------------------------------------*/
static const int codes_gps[] = {
    CODE_L1C, CODE_L1P, CODE_L1W, CODE_L1Y, CODE_L1M, CODE_L2C, CODE_L2D, CODE_L2S,
    CODE_L2L, CODE_L2X, CODE_L2P, CODE_L2W, CODE_L2Y, CODE_L2M, CODE_L5I, CODE_L5Q,
    CODE_L5X};
static const int codes_glo[] = {
    CODE_L1C, CODE_L1P, CODE_L2C, CODE_L2P};
static const int codes_gal[] = {
    CODE_L1A, CODE_L1B, CODE_L1C, CODE_L1X, CODE_L1Z, CODE_L5I, CODE_L5Q, CODE_L5X,
    CODE_L7I, CODE_L7Q, CODE_L7X, CODE_L8I, CODE_L8Q, CODE_L8X, CODE_L6A, CODE_L6B,
    CODE_L6C, CODE_L6X, CODE_L6Z};
static const int codes_qzs[] = {
    CODE_L1C, CODE_L1S, CODE_L1L, CODE_L2S, CODE_L2L, CODE_L2X, CODE_L5I, CODE_L5Q,
    CODE_L5X, CODE_L6S, CODE_L6L, CODE_L6X, CODE_L1X};
static const int codes_bds[] = {
    CODE_L1I, CODE_L1Q, CODE_L1X, CODE_L7I, CODE_L7Q, CODE_L7X, CODE_L6I, CODE_L6Q,
    CODE_L6X, CODE_L5I, CODE_L5Q, CODE_L5X};
static const int codes_sbs[] = {
    CODE_L1C, CODE_L5I, CODE_L5Q, CODE_L5X};

/* get observation data index ------------------------------------------------*/
RTK_RAM_CODE static int obsindex(obs_t *obs, gtime_t time, int sat)
{
    unsigned int i;

    double tt = timediff(obs->time, time);

    if (fabs(tt) > 0.01)
    {
        /* new epoch, reset the n and obsflag */
        obs->n = 0;
        obs->obsflag = 0;
    }
    if (obs->n == 0)
    {
        /* first obs, set the time tag */
        obs->time = time;
    }
    for (i = 0; i < obs->n; i++)
    {
		if (obs->data[i].sat == sat)
			break; /* field already exists */
    }
	if (i == obs->n)
	{
		/* add new field */
		if (obs->n < MAXOBS)
		{
			memset(obs->data + i, 0, sizeof(obsd_t));
			obs->data[i].sat = (unsigned char)sat;
			obs->n++;
		}
		else
		{
			i = -1;
		}
	}
	else
	{
		/* duplicated satellites */
		memset(obs->data + i, 0, sizeof(obsd_t));
		obs->data[i].sat = (unsigned char)sat;
	}
    return i;
}
/* test station id consistency -----------------------------------------------*/
RTK_RAM_CODE static int test_staid(obs_t *obs, int staid)
{
    /*int type;*/

    /* save station id */
    if (obs->staid == 0 || obs->obsflag)
    {
        obs->staid = staid;
    }
    else if (staid != obs->staid)
    {
        /* reset station id if station id error */
        obs->staid = 0;
        return 0;
    }
    return 1;
}


char *obscodes[] = {
    /* observation code strings */

    "", "1C", "1P", "1W", "1Y", "1M", "1N", "1S", "1L", "1E",   /*  0- 9 */
    "1A", "1B", "1X", "1Z", "2C", "2D", "2S", "2L", "2X", "2P", /* 10-19 */
    "2W", "2Y", "2M", "2N", "5I", "5Q", "5X", "7I", "7Q", "7X", /* 20-29 */
    "6A", "6B", "6C", "6X", "6Z", "6S", "6L", "8L", "8Q", "8X", /* 30-39 */
    "2I", "2Q", "6I", "6Q", "3I", "3Q", "3X", "1I", "1Q", "5A", /* 40-49 */
    "5B", "5C", "9A", "9B", "9C", "9X", "", "", "", ""          /* 50-59 */
};
/* GPS  */
static unsigned char obsfreqs_gps[] = {
    /* 1:L1, 2:L2, 3:L5 */
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, /*  0- 9 */
    0, 0, 1, 1, 2, 2, 2, 2, 2, 2, /* 10-19 */
    2, 2, 2, 2, 3, 3, 3, 0, 0, 0, /* 20-29 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 30-39 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 40-49 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0  /* 50-59 */
};
/* GLO */
static unsigned char obsfreqs_glo[] = {
    /* 1:G1, 2:G2, 3:G3 */
    0, 1, 1, 0, 0, 0, 0, 0, 0, 0, /*  0- 9 */
    0, 0, 0, 0, 2, 0, 0, 0, 0, 2, /* 10-19 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 20-29 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 30-39 */
    0, 0, 0, 0, 3, 3, 3, 0, 0, 0, /* 40-49 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0  /* 50-59 */
};
/* GAL */
static unsigned char obsfreqs_gal[] = {
    /* 1:E1, 2:E5b, 3:E5a, 4:E5(a+b), 5:E6 */
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, /*  0- 9 */
    1, 1, 1, 1, 0, 0, 0, 0, 0, 0, /* 10-19 */
    0, 0, 0, 0, 3, 3, 3, 2, 2, 2, /* 20-29 */
    5, 5, 5, 5, 5, 0, 0, 4, 4, 4, /* 30-39 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 40-49 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0  /* 50-59 */
};
/* QZSS */
static unsigned char obsfreqs_qzs[] = {
    /* 1:L1, 2:L2, 3:L5, 4:LEX, 5:S */
    0, 1, 0, 0, 0, 0, 0, 1, 1, 0, /*  0- 9 */
    0, 0, 1, 1, 0, 0, 2, 2, 2, 0, /* 10-19 */
    0, 0, 0, 0, 3, 3, 3, 0, 0, 0, /* 20-29 */
    0, 0, 0, 4, 0, 4, 4, 0, 0, 0, /* 30-39 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 40-49 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0  /* 50-59 */
};
/* SBS */
static unsigned char obsfreqs_sbs[] = {
    /* 1:L1, 2:L5 */
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, /*  0- 9 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 10-19 */
    0, 0, 0, 0, 2, 2, 2, 0, 0, 0, /* 20-29 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 30-39 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 40-49 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0  /* 50-59 */
};
/* BDS */
static unsigned char obsfreqs_cmp[] = {
    /* 1:B1, 2:B3, 3:B2 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /*  0- 9 */
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, /* 10-19 */
    0, 0, 0, 0, 4, 4, 4, 3, 3, 3, /* 20-29 */
    0, 0, 0, 2, 0, 0, 0, 0, 0, 0, /* 30-39 */
    1, 1, 2, 2, 0, 0, 0, 1, 1, 0, /* 40-49 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0  /* 50-59 */
};
/* IRN */
static unsigned char obsfreqs_irn[] = {
    /* 1:L5, 2:S */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /*  0- 9 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 10-19 */
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, /* 20-29 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 30-39 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, /* 40-49 */
    1, 1, 2, 2, 2, 2, 0, 0, 0, 0  /* 50-59 */
};

static char codepris[7][MAXFREQ][16] = {
    /* code priority table */

    {"CPYWMNSL", "PYWCMNDSLX", "IQX", "", "", "", ""}, /* GPS 1:L1, 2:L2, 3:L5 */
    {"PC", "PC", "IQX", "", "", "", ""},               /* GLO 1:G1, 2:G2, 3:G3 */
    {"ABCXZ", "IQX", "IQX", "IQX", "ABCXZ", "", ""},   /* GAL 1:E1, 2:E5b, 3:E5a, 4:E5(a+b), 5:E6 */
    {"CSLXZ", "SLX", "IQX", "SLX", "", "", ""},        /* QZS 1:L1, 2:L2, 3:L5, 4:LEX */
    {"C", "IQX", "", "", "", "", ""},                  /* SBS 1:L1, 2:L5 */
    {"IQX", "IQX", "IQX", "IQX", "", "", ""},          /* BDS 1:B1, 2:B3, 3:B2, 4:B2a */
    {"ABCX", "ABCX", "", "", "", "", ""}               /* IRN 1:L5, 2:S */
};

const double lam_carr[] = {/* carrier wave length (m) */
	CLIGHT / FREQ1, CLIGHT / FREQ2, CLIGHT / FREQ5, CLIGHT / FREQ6, 
	CLIGHT / FREQ7, CLIGHT / FREQ8, CLIGHT / FREQ9};

RTK_RAM_CODE extern char satid(int sat, int *prn)
{
    char sys = ' ';
    if (sat <= 0 || MAXSAT < sat)
        sat = 0;
    else if (sat <= NSATGPS)
    {
        sys = 'G';
        sat += MINPRNGPS - 1;
    }
    else if ((sat -= NSATGPS) <= NSATGLO)
    {
        sys = 'R';
        sat += MINPRNGLO - 1;
    }
    else if ((sat -= NSATGLO) <= NSATGAL)
    {
        sys = 'E';
        sat += MINPRNGAL - 1;
    }
    //else if ((sat -= NSATGAL) <= NSATQZS)
    //{
    //    sys = 'J';
    //    sat += MINPRNQZS - 1;
    //}
    else if ((sat -= NSATGAL) <= NSATCMP)
    {
        sys = 'C';
        sat += MINPRNCMP - 1;
    }
    else if ((sat -= NSATCMP) <= NSATLEO)
    {
        sys = 'L';
        sat += MINPRNLEO - 1;
    }
    else if ((sat -= NSATLEO) <= NSATSBS)
    {
        sys = 'S';
        sat += MINPRNSBS - 1;
    }
    else
        sat = 0;
    if (prn)
        *prn = sat;
    return sys;
}

/* only use GPS, GLO, GAL, BDS */
RTK_RAM_CODE extern int satidx(int sat, int *prn)
{
    int sys = -1;
    if (sat <= 0 || MAXSAT < sat)
        sat = 0;
    else if (sat <= NSATGPS)
    {
        sys = 0;
        sat += MINPRNGPS - 1; /* GPS */
    }
    else if ((sat -= NSATGPS) <= NSATGLO)
    {
        sys = 3;
        sat += MINPRNGLO - 1; /* GLO */
    }
    else if ((sat -= NSATGLO) <= NSATGAL)
    {
        sys = 1;
        sat += MINPRNGAL - 1; /* GAL */
    }
    //else if ((sat -= NSATGAL) <= NSATQZS)
    //{
    //    sys = -1;
    //    sat += MINPRNQZS - 1; /* QZSS */
    //}
    else if ((sat -= NSATGAL) <= NSATCMP)
    {
        sys = 2;
        sat += MINPRNCMP - 1; /* BDS */
    }
    else if ((sat -= NSATCMP) <= NSATLEO)
    {
        sys = -1;
        sat += MINPRNLEO - 1; /* LEO */
    }
    else if ((sat -= NSATLEO) <= NSATSBS)
    {
        sys = -1;
        sat += MINPRNSBS - 1; /* SBS */
    }
    else
        sat = 0;
    if (prn)
        *prn = sat;
    return sys;
}

RTK_RAM_CODE extern char sys2char(int sys)
{
    int s_char = ' ';
    if (sys == _SYS_GPS_)
        s_char = 'G';
    else if (sys == _SYS_GLO_)
        s_char = 'R';
    else if (sys == _SYS_GAL_)
        s_char = 'E';
    else if (sys == _SYS_QZS_)
        s_char = 'J';
    else if (sys == _SYS_BDS_)
        s_char = 'C';
    else if (sys == _SYS_LEO_)
        s_char = 'L';
    else if (sys == _SYS_SBS_)
        s_char = 'S';
    return s_char;
}

RTK_RAM_CODE extern int code2frq(int sys, int code)
{
	int frq = -1;
	if (code <= CODE_NONE || code >= MAXCODE)
		return -1;
	if (sys == _SYS_GPS_ || sys == _SYS_QZS_)
	{
		if (code >= CODE_L1C && code <= CODE_L1L)
		{
			frq = 0;
		}
		else if (code >= CODE_L2C && code <= CODE_L2N)
		{
			frq = 1;
		}
		else if (code >= CODE_L5I && code <= CODE_L5X)
		{
			frq = 2;
		}
	}
	else if (sys == _SYS_GAL_)
	{
		if ((code >= CODE_L1A && code <= CODE_L1Z)|| code == CODE_L1C)
		{
			frq = 0;
		}
		else if (code >= CODE_L7I && code <= CODE_L7X)
		{
			frq = 1;
		}
		else if (code >= CODE_L5I && code <= CODE_L5X)
		{
			frq = 2;
		}
		else if (code >= CODE_L8I && code <= CODE_L8X)
		{
			frq = 3;
		}
		else if (code >= CODE_L6A && code <= CODE_L6Z)
		{
			frq = 4;
		}
	}
	else if (sys == _SYS_BDS_)
	{
		if (code == CODE_L2I || code == CODE_L2Q || code == CODE_L1I || 
			code == CODE_L1Q || code == CODE_L2X)
		{
			frq = 0;
		}
		else if (code >= CODE_L7I && code <= CODE_L7X)
		{
			frq = 1;
		}
		else if (code == CODE_L6X || code == CODE_L6I || code == CODE_L6Q)
		{
			frq = 2;
		}
		else if (code >= CODE_L5I && code <= CODE_L5X)
		{
			frq = 3;
		}
	}
	else if (sys == _SYS_GLO_)
	{
		if (code == CODE_L1C || code == CODE_L1P)
		{
			frq = 0;
		}
		else if (code == CODE_L2C || code == CODE_L2P)
		{
			frq = 1;
		}
		else if (code == CODE_L3I || code == CODE_L3X)
		{
			frq = 2;
		}
	}
	
	return frq;

}

/* satellite carrier wave length -----------------------------------------------
* get satellite carrier wave lengths
* args   : int    sat       I   satellite number
*          int    frq       I   frequency index (0:L1,1:L2,2:L5/3,...)
*          nav_t  *nav      I   navigation messages
* return : carrier wave length (m) (0.0: error)
*-----------------------------------------------------------------------------*/
RTK_RAM_CODE extern double satwavelenbyfreq(int sat, int frq)
{
    const double freq_glo[] = { FREQ1_GLO, FREQ2_GLO };
    const double dfrq_glo[] = { DFRQ1_GLO, DFRQ2_GLO };
    int prn = 0, sys = satsys(sat, &prn);
    int frqnum = get_glo_frq(prn);

    if (sys == _SYS_GLO_)
    {
        if (frqnum == -99)
            return 0.0;
        if (0 <= frq && frq <= 1)
        {
            return CLIGHT / (freq_glo[frq] + dfrq_glo[frq] * frqnum);
        }
        else if (frq == 2)
        { /* L3 */
            return CLIGHT / FREQ3_GLO;
        }
    }
    else if (sys == _SYS_BDS_)
    {
        if (frq == 0)
            return CLIGHT / FREQ1_CMP; /* B1 */
        else if (frq == 1)
            return CLIGHT / FREQ3_CMP; /* B3 */
        else if (frq == 2)
            return CLIGHT / FREQ2_CMP; /* B2 */
		else if (frq == 3)
			return CLIGHT / FREQ5;     /* B2a */
    }
    else if (sys == _SYS_GAL_)
    {
        if (frq == 0)
            return CLIGHT / FREQ1; /* E1 */
        else if (frq == 1)
            return CLIGHT / FREQ7; /* E5b */
        else if (frq == 2)
            return CLIGHT / FREQ5; /* E5a */
        else if (frq == 3)
            return CLIGHT / FREQ8; /* E5a+b */
        else if (frq == 4)
            return CLIGHT / FREQ6; /* E6 */
    }
    else if (sys == _SYS_QZS_)
    {
        if (frq == 0)
            return CLIGHT / FREQ1; /* L1 */
        else if (frq == 1)
            return CLIGHT / FREQ2; /* L2 */
        else if (frq == 2)
            return CLIGHT / FREQ5; /* L5 */
        else if (frq == 3)
            return CLIGHT / FREQ6; /* LEX */
    }
    else if (sys == _SYS_GPS_)
    {
        if (frq == 0)
            return CLIGHT / FREQ1; /* L1 */
        else if (frq == 1)
            return CLIGHT / FREQ2; /* L2 */
        else if (frq == 2)
            return CLIGHT / FREQ5; /* L5 */
    }
    return 0.0;
}

/* satellite carrier wave length -----------------------------------------------
* get satellite carrier wave lengths
* args   : int    sat       I   satellite number
*          int    frq       I   frequency index (0:L1,1:L2,2:L5/3,...)
*          nav_t  *nav      I   navigation messages
* return : carrier wave length (m) (0.0: error)
*-----------------------------------------------------------------------------*/
RTK_RAM_CODE extern double satwavelen(int sat, int code)
{
    const double freq_glo[] = {FREQ1_GLO, FREQ2_GLO};
    const double dfrq_glo[] = {DFRQ1_GLO, DFRQ2_GLO};
    int prn = 0, frq, sys = satsys(sat, &prn);
    int frqnum = get_glo_frq(prn);

	frq = code2frq(sys, code);

    if (sys == _SYS_GLO_)
    {
		if (frqnum == -99)
			return 0.0;
        if (0 <= frq && frq <= 1)
        {
            return CLIGHT / (freq_glo[frq] + dfrq_glo[frq] * frqnum);
        }
        else if (frq == 2)
        { /* L3 */
            return CLIGHT / FREQ3_GLO;
        }
    }
    else if (sys == _SYS_BDS_)
    {
		if (frq == 0)
			return CLIGHT / FREQ1_CMP; /* B1 */
		else if (frq == 1)
		{
			return CLIGHT / FREQ2_CMP; /* B2 */
		}
		else if (frq == 2)
		{
			return CLIGHT / FREQ3_CMP; /* B3 */
		}
		else if (frq == 3)
		{
			return CLIGHT / FREQ5; /* B2a */
		}
    }
    else if (sys == _SYS_GAL_)
    {
        if (frq == 0)
            return CLIGHT / FREQ1; /* E1 */
		else if (frq == 1)
		{
			return CLIGHT / FREQ7; /* E5b */
		}
		else if (frq == 2)
		{
			return CLIGHT / FREQ5; /* E5a */
		}
		else if (frq == 3)
		{
			return CLIGHT / FREQ8; /* E5a+b */
		}
		else if (frq == 4)
		{
			return CLIGHT / FREQ6; /* E6 */
		}
    }
    else if (sys == _SYS_QZS_)
    {
        if (frq == 0)
            return CLIGHT / FREQ1; /* L1 */
        else if (frq == 1)
            return CLIGHT / FREQ2; /* L2 */
        else if (frq == 2)
            return CLIGHT / FREQ5; /* L5 */
        else if (frq == 3)
            return CLIGHT / FREQ6; /* LEX */
    }
    else if (sys == _SYS_GPS_)
    {
        if (frq == 0)
            return CLIGHT / FREQ1; /* L1 */
		else if (frq == 1)
		{
			return CLIGHT / FREQ2; /* L2 */
		}
		else if (frq == 2)
		{
			return CLIGHT / FREQ5; /* L5 */
		}
    }
    return 0.0;
}

/* obs type string to obs code -------------------------------------------------
* convert obs code type string to obs code
* args   : char   *str   I      obs code string ("1C","1P","1Y",...)
*          int    *freq  IO     frequency (1:L1,2:L2,3:L5,4:L6,5:L7,6:L8,0:err)
*                               (NULL: no output)
* return : obs code (CODE_???)
* notes  : obs codes are based on reference [6] and qzss extension
*-----------------------------------------------------------------------------*/
RTK_RAM_CODE extern unsigned char obs2code(int sys, const char *obs, int *freq)
{
    int i;

	if (freq)
	{
		*freq = 0;
	}
    for (i = 1; *obscodes[i]; i++)
    {
		if (strcmp(obscodes[i], obs))
		{
			continue;
		}
        if (freq)
        {
			if (sys == _SYS_GPS_)
			{
				*freq = obsfreqs_gps[i];
			}
			else if (sys == _SYS_GLO_)
			{
				*freq = obsfreqs_glo[i];
			}
			else if (sys == _SYS_GAL_)
			{
				*freq = obsfreqs_gal[i];
			}
			else if (sys == _SYS_QZS_)
			{
				*freq = obsfreqs_qzs[i];
			}
			else if (sys == _SYS_SBS_)
			{
				*freq = obsfreqs_sbs[i];
			}
			else if (sys == _SYS_BDS_)
			{
				*freq = obsfreqs_cmp[i];
			}
			else if (sys == _SYS_IRN_)
			{
				*freq = obsfreqs_irn[i];
			}
			*freq = NFREQ + 1;
        }

        return (unsigned char)i;
    }

    return CODE_NONE;
}

/* obs type string to obs code -------------------------------------------------
* convert obs code type string to obs code
* args   : char   *str   I      obs code string ("1C","1P","1Y",...)
*          int    *freq  IO     frequency (1:L1,2:L2,3:L5,4:L6,5:L7,6:L8,0:err)
*                               (NULL: no output)
* return : obs code (CODE_???)
* notes  : obs codes are based on reference [6] and qzss extension
*-----------------------------------------------------------------------------*/
RTK_RAM_CODE extern unsigned char obs2coderinex(int sys, const char *obs, int *freq)
{
	int i;

	if (freq)
	{
		*freq = 0;
	}

	for (i = 1; *obscodes[i]; i++)
	{
		if (strcmp(obscodes[i], obs))
		{
			continue;
		}
		if (freq)
		{
			if (sys == _SYS_GPS_)
			{
				*freq = obsfreqs_gps[i];
			}
			else if (sys == _SYS_GLO_)
			{
				*freq = obsfreqs_glo[i];
			}
			else if (sys == _SYS_GAL_)
			{
				*freq = obsfreqs_gal[i];
			}
			else if (sys == _SYS_QZS_)
			{
				*freq = obsfreqs_qzs[i];
			}
			else if (sys == _SYS_SBS_)
			{
				*freq = obsfreqs_sbs[i];
			}
			else if (sys == _SYS_BDS_)
			{
				*freq = obsfreqs_cmp[i];
			}
			else if (sys == _SYS_IRN_)
			{
				*freq = obsfreqs_irn[i];
			}
		}

		return (unsigned char)i;
	}

	return CODE_NONE;
}

/* obs code to obs code string -------------------------------------------------
* convert obs code to obs code string
* args   : unsigned char code I obs code (CODE_???)
*          int    *freq  IO     frequency (NULL: no output)
*                               (1:L1/E1, 2:L2/B1, 3:L5/E5a/L3, 4:L6/LEX/B3,
                                 5:E5b/B2, 6:E5(a+b), 7:S)
* return : obs code string ("1C","1P","1P",...)
* notes  : obs codes are based on reference [6] and qzss extension
*-----------------------------------------------------------------------------*/
RTK_RAM_CODE extern char *code2obs(int sys, unsigned char code, int *freq)
{
	if (freq)
	{
		*freq = 0;
	}
	if (code <= CODE_NONE || MAXCODE < code)
	{
		return "";
	}
    if (freq)
    {
		if (sys == _SYS_GPS_)
		{
			*freq = obsfreqs_gps[code];
		}
		else if (sys == _SYS_GLO_)
		{
			*freq = obsfreqs_glo[code];
		}
		else if (sys == _SYS_GAL_)
		{
			*freq = obsfreqs_gal[code];
		}
		else if (sys == _SYS_QZS_)
		{
			*freq = obsfreqs_qzs[code];
		}
		else if (sys == _SYS_SBS_)
		{
			*freq = obsfreqs_sbs[code];
		}
		else if (sys == _SYS_BDS_)
		{
			*freq = obsfreqs_cmp[code];
		}
		else if (sys == _SYS_IRN_)
		{
			*freq = obsfreqs_irn[code];
		}
    }
    return obscodes[code];
}
/* set code priority -----------------------------------------------------------
* set code priority for multiple codes in a frequency
* args   : int    sys     I     system (or of SYS_???)
*          int    freq    I     frequency (1:L1,2:L2,3:L5,4:L6,5:L7,6:L8,7:L9)
*          char   *pri    I     priority of codes (series of code characters)
*                               (higher priority precedes lower)
* return : none
*-----------------------------------------------------------------------------*/
RTK_RAM_CODE extern void setcodepri(int sys, int freq, const char *pri)
{
	if (freq <= 0 || MAXFREQ < freq)
	{
		return;
	}

	if (sys & _SYS_GPS_)
	{
		strcpy(codepris[0][freq - 1], pri);
	}
	if (sys & _SYS_GLO_)
	{
		strcpy(codepris[1][freq - 1], pri);
	}
	if (sys & _SYS_GAL_)
	{
		strcpy(codepris[2][freq - 1], pri);
	}
	if (sys & _SYS_QZS_)
	{
		strcpy(codepris[3][freq - 1], pri);
	}
	if (sys & _SYS_SBS_)
	{
		strcpy(codepris[4][freq - 1], pri);
	}
	if (sys & _SYS_BDS_)
	{
		strcpy(codepris[5][freq - 1], pri);
	}
	if (sys & _SYS_IRN_)
	{
		strcpy(codepris[6][freq - 1], pri);
	}

	return;
}
/* get code priority -----------------------------------------------------------
* get code priority for multiple codes in a frequency
* args   : int    sys     I     system (SYS_???)
*          unsigned char code I obs code (CODE_???)
*          char   *opt    I     code options (NULL:no option)
* return : priority (15:highest-1:lowest,0:error)
*-----------------------------------------------------------------------------*/
RTK_RAM_CODE extern int getcodepri(int sys, unsigned char code, const char *opt)
{
    const char *p, *optstr;
    char *obs, str[8] = "";
    int i, j;

    switch (sys)
    {
    case _SYS_GPS_:
        i = 0;
        optstr = "-GL%2s";
        break;
    case _SYS_GLO_:
        i = 1;
        optstr = "-RL%2s";
        break;
    case _SYS_GAL_:
        i = 2;
        optstr = "-EL%2s";
        break;
    case _SYS_QZS_:
        i = 3;
        optstr = "-JL%2s";
        break;
    case _SYS_SBS_:
        i = 4;
        optstr = "-SL%2s";
        break;
    case _SYS_BDS_:
        i = 5;
        optstr = "-CL%2s";
        break;
    case _SYS_IRN_:
        i = 6;
        optstr = "-IL%2s";
        break;
    default:
        return 0;
    }
    obs = code2obs(sys, code, &j);

    /* parse code options */
    for (p = opt; p && (p = strchr(p, '-')); p++)
    {
        if (sscanf(p, optstr, str) < 1 || str[0] != obs[0])
            continue;
        return str[1] == obs[1] ? 15 : 0;
    }

    /* search code priority */
    return (p = strchr(codepris[i][j - 1], obs[1])) ? 14 - (int)(p - codepris[i][j - 1]) : 0;
}

/* get signal index ----------------------------------------------------------*/
RTK_RAM_CODE static void sigindex(int sys, const unsigned char *code, const int *freq, int n,
                     const char *opt, int *ind)
{
    int i, nex, pri, pri_h[8] = {0}, index[8] = {0}, ex[32] = {0};

    /* test code priority */
    for (i = 0; i < n; i++)
    {
        if (!code[i])
            continue;

        if (freq[i] > NFREQ)
        { /* save as extended signal if freq > NFREQ */
            ex[i] = 1;
            continue;
        }
        /* code priority */
        pri = getcodepri(sys, code[i], opt);

        /* select highest priority signal */
        if (pri > pri_h[freq[i] - 1])
        {
			if (index[freq[i] - 1])
			{
				ex[index[freq[i] - 1] - 1] = 1;
			}
            pri_h[freq[i] - 1] = pri;
            index[freq[i] - 1] = i + 1;
        }
		else
		{
			ex[i] = 1;
		}
    }
    /* signal index in obs data */
    for (i = nex = 0; i < n; i++)
    {
		if (ex[i] == 0)
		{
			ind[i] = freq[i] - 1;
		}
		else if (freq[i] <= NFREQ + NEXOBS)
		{
			ind[i] = freq[i] - 1;
		}
        else
        { /* no space in obs data */
            ind[i] = -1;
        }
    }

	return;
}

/* adjust carrier-phase rollover ---------------------------------------------*/
RTK_RAM_CODE static double adjcp(rtcm_t *rtcm, int sat, int freq, double cp)
{
    //if (rtcm->cp[sat-1][freq]==0.0) ;
    //else if (cp<rtcm->cp[sat-1][freq]-750.0) cp+=1500.0;
    //else if (cp>rtcm->cp[sat-1][freq]+750.0) cp-=1500.0;
    //rtcm->cp[sat-1][freq]=cp;
    return cp;
}

/* loss-of-lock indicator ----------------------------------------------------*/
RTK_RAM_CODE static int lossoflock(rtcm_t *rtcm, int sat, int freq, int lock)
{
	int lli = (!lock && !rtcm->lock[sat - 1][freq]) || lock < rtcm->lock[sat - 1][freq];

	rtcm->lock[sat - 1][freq] = (unsigned short)lock;

	return lli;
}

/* s/n ratio -----------------------------------------------------------------*/
RTK_RAM_CODE static unsigned char snratio(double snr)
{
    return (unsigned char)(snr <= 0.0 || 255.5 <= snr ? 0.0 : snr * 4.0 + 0.5);
}

/* Receiver Status and Safety (RSS) */
RTK_RAM_CODE static void decode_type999_id1(rtcm_t *rtcm, obs_t *obs)
{
	int i = 24, gnss_id, week, toe;

	gnss_id = rtcm_getbitu(rtcm->buff, i, 12);
	i += 12;
	//rtcm_999_receiver_PVT.reserved_ITRF = rtcm_getbitu(rtcm->buff, i, 6);
	i += 8;
	
	i += 30;
	week = rtcm_getbitu(rtcm->buff, i, 16);
	i += 16;

	i += 8;

	i += 1;

	i += 7;

	i += 24;

	i += 24;

	rtcm->teseo.PPS_Status = rtcm_getbitu(rtcm->buff, i, 8);
	//printf("PPS:%d\n", rtcm->teseo.PPS_Status);

}

/* receiver PVT */
RTK_RAM_CODE static void decode_type999_id4(rtcm_t *rtcm, obs_t *obs)
{
	int i = 44, gnss_id, week;

    rtcm->teseo.ref_id = rtcm_getbitu(rtcm->buff, i, 12);
    i += 12;
    //rtcm_999_receiver_PVT.reserved_ITRF = rtcm_getbitu(rtcm->buff, i, 6);
    i += 6;
    rtcm->teseo.fix_status = rtcm_getbitu(rtcm->buff, i, 4);
    i += 4;
	rtcm->teseo.nsat_use = rtcm_getbitu(rtcm->buff, i, 8);
    i += 8;
	rtcm->teseo.nsat_view = rtcm_getbitu(rtcm->buff, i, 8);
    i += 8;
	rtcm->teseo.hdop = rtcm_getbitu(rtcm->buff, i, 8);
    i += 8;
	rtcm->teseo.vdop = rtcm_getbitu(rtcm->buff, i, 8);
    i += 8;
	rtcm->teseo.pdop = rtcm_getbitu(rtcm->buff, i, 8);
    i += 8;
	rtcm->teseo.geo_sep = (float)rtcm_getbitu(rtcm->buff, i, 15) * 0.01f;
    i += 15;
	rtcm->teseo.age = (float)rtcm_getbitu(rtcm->buff, i, 24) * 0.001f;
    i += 24;
	//rtcm->teseo.ref_id = rtcm_getbitu(rtcm->buff, i, 12);
    i += 12;
	gnss_id = rtcm_getbitu(rtcm->buff, i, 4);
	rtcm->teseo.sys = GNSS_ID[gnss_id];
    i += 4;
	rtcm->teseo.time = (float)rtcm_getbitu(rtcm->buff, i, 30) * 0.001f;
    i += 30;
	week = rtcm_getbitu(rtcm->buff, i, 16);

	if (week > 1024 && week < 4096 && rtcm->teseo.time < SECONDS_IN_WEEK + 0.1)
	{
		rtcm->teseo.time += week * SECONDS_IN_WEEK;
	}
	else
	{
		rtcm->teseo.time = 0.0;
	}
    i += 16;
	rtcm->teseo.leap_sec = rtcm_getbitu(rtcm->buff, i, 8);
    i += 8;
	rtcm->teseo.pos[0] = (double)rtcm_getbits_38(rtcm->buff, i) *0.0001f;
    i += 38;
	rtcm->teseo.pos[1] = (double)rtcm_getbits_38(rtcm->buff, i) *0.0001f;
    i += 38;
	rtcm->teseo.pos[2] = (double)rtcm_getbits_38(rtcm->buff, i) *0.0001f;
    i += 38;
	rtcm->teseo.vel[0] = (float)rtcm_getbits(rtcm->buff, i, 32) * 0.000001f;
    i += 32;
	rtcm->teseo.vel[1] = (float)rtcm_getbits(rtcm->buff, i, 32) * 0.000001f;
    i += 32;
	rtcm->teseo.vel[2] = (float)rtcm_getbits(rtcm->buff, i, 32) * 0.000001f;
    i += 32;

	if (rtcm->teseo.pdop > 25)
	{
		rtcm->teseo.fix_status = 0;
	}

	obs->rtcmtype = 99;
}

/* decode Observable Quality Metric */
RTK_RAM_CODE static void decode_type999_id6(rtcm_t *rtcm, obs_t *obs)
{
	double tow, metValue[MAXOBS*5];
	int i = 24, j, nsat = 0, nmet = 0, temps, mesnum, subtypeID;
	unsigned int gnssID, gnssSatM, metMask, mmind;
	int prn[40] = { 0 }, metrics[32] = {0}, sys = _SYS_NONE_;

	mesnum = (int)rtcm_getbitu(rtcm->buff, i, 12);
	i += 12;
	subtypeID = (int)rtcm_getbitu(rtcm->buff, i, 8);
	i += 8;
	tow = (double)rtcm_getbitu(rtcm->buff, i, 30);
	i += 30;
	gnssID = (int)rtcm_getbitu(rtcm->buff, i, 4);
	i += 4;

	if (gnssID > 11)
	{
		return;
	}

	sys = GNSS_ID[gnssID];

	for (j = 0; j < 40; j++)
	{
		gnssSatM = (unsigned int)rtcm_getbitu(rtcm->buff, i, 1);
		i += 1;
		if (gnssSatM)
		{
			prn[nsat++] = j + 1;
		}
	}

	for (j = 0; j < 32; j++)
	{
		metMask = (unsigned int)rtcm_getbitu(rtcm->buff, i, 1);
		if (metMask)
		{
			metrics[nmet++] = 31 - j;
		}
		i += 1;
	}
	for (j = 0; j < (int)nmet*0.5; j++)
	{
		temps = metrics[j];
		metrics[j] = metrics[nmet - j - 1];
		metrics[nmet - j - 1] = temps;
	}
	mmind = (unsigned int)rtcm_getbitu(rtcm->buff, i, 1);
	i += 1;
	for (j = 0; j < nsat * nmet && j < MAXOBS * 5; j++)
	{
		metValue[j] = (double)0.001*rtcm_getbitu_64(rtcm->buff, i, 32);
		i += 32;
	}

	return;
}

/* decode Inter Frequency Biases */
RTK_RAM_CODE static void decode_type999_id8(rtcm_t *rtcm, obs_t *obs)
{
	double tow;
	int i = 24, mesnum, subtypeID, sys = _SYS_NONE_;
	unsigned int gnssID;

	mesnum = (int)rtcm_getbitu(rtcm->buff, i, 12);
	i += 12;
	subtypeID = (int)rtcm_getbitu(rtcm->buff, i, 8);
	i += 8;
	tow = (double)rtcm_getbitu(rtcm->buff, i, 30);
	i += 30;
	gnssID = (int)rtcm_getbitu(rtcm->buff, i, 4);
	i += 4;
	if (gnssID > 11)
	{
		return;
	}
	sys = GNSS_ID[gnssID];

	return;
}

/* decode Ionospheric Model Parameters */
RTK_RAM_CODE static void decode_type999_id9(rtcm_t *rtcm, obs_t *obs)
{
	double tow;
	int i = 24, j, mesnum, subtypeID, sys = _SYS_NONE_;
	unsigned int gnssID, ai0, ai1, ai2, regions;
	unsigned int ioncof[8] = { 0 };

	mesnum = (int)rtcm_getbitu(rtcm->buff, i, 12);
	i += 12;
	subtypeID = (int)rtcm_getbitu(rtcm->buff, i, 8);
	i += 8;
	tow = (double)rtcm_getbitu(rtcm->buff, i, 30);
	i += 30;
	gnssID = (int)rtcm_getbitu(rtcm->buff, i, 4);
	i += 4;

	if (gnssID > 11)
	{
		return;
	}
	sys = GNSS_ID[gnssID];

	if (sys == _SYS_GAL_)
	{
		ai0 = (unsigned int)rtcm_getbitu(rtcm->buff, i, 11);
		i += 11;
		ai1 = (unsigned int)rtcm_getbitu(rtcm->buff, i, 11);
		i += 11;
		ai2 = (unsigned int)rtcm_getbitu(rtcm->buff, i, 14);
		i += 14;
		regions = (unsigned int)rtcm_getbitu(rtcm->buff, i, 5);
		i += 5;
	}
	else
	{
		for (j = 0; j < 8; j++)
		{
			ioncof[j] = (unsigned int)rtcm_getbitu(rtcm->buff, i, 8);
			i += 8;
		}

	}

	return;
}

/* Extended PVT (EPVT) */
RTK_RAM_CODE static void decode_type999_id21(rtcm_t *rtcm, obs_t *obs)
{
	int dat_sta = 0, gnss_idx = 0, fix_frq = 0, raim = 0, rfu = 0, time_vail = 0;
	int fw_ver = 811, week = 0;
	int i = 44;
	double blh[3], vel_enu[3], pl_enu[3];
	double vel_horz = 0.0, vel_ver = 0.0, cour_anl = 0.0, tow = 0.0, plh = 0.0, plv = 0.0, pla = 0.0;

	if (rtcm->len >= 60)
	{
		fw_ver = 812;
	}

	rtcm->teseo.ref_id = rtcm_getbitu(rtcm->buff, i, 12);
	i += 12;
	/* ITRF Realization Year */
	i += 6;
	rtcm->teseo.fix_status = rtcm_getbitu(rtcm->buff, i, 4);
	i += 4;

	/* decode raim message for version 5.8.12 ... */
	if (fw_ver == 812)
	{
		dat_sta = rtcm_getbitu(rtcm->buff, i, 1);
		i += 1;
		fix_frq = rtcm_getbitu(rtcm->buff, i, 1);
		i += 1;
		raim = rtcm_getbitu(rtcm->buff, i, 1);
		i += 1;
		rfu = rtcm_getbitu(rtcm->buff, i, 1);
		i += 1;
		if ((dat_sta == 1) || (raim == 0))
		{
			rtcm->teseo.fix_status = 0;
		}
	}

	rtcm->teseo.nsat_use = rtcm_getbitu(rtcm->buff, i, 8);
	i += 8;
	rtcm->teseo.nsat_view = rtcm_getbitu(rtcm->buff, i, 8);
	i += 8;
	if ((rtcm->teseo.nsat_use == 255) || (rtcm->teseo.nsat_view == 255))
	{
		rtcm->teseo.fix_status = 0;
	}
	rtcm->teseo.hdop = rtcm_getbitu(rtcm->buff, i, 8);
	i += 8;
	rtcm->teseo.vdop = rtcm_getbitu(rtcm->buff, i, 8);
	i += 8;
	rtcm->teseo.pdop = rtcm_getbitu(rtcm->buff, i, 8);
	i += 8;
	if ((rtcm->teseo.hdop == 255) || (rtcm->teseo.vdop == 255)
		|| (rtcm->teseo.pdop == 255))
	{
		rtcm->teseo.fix_status = 0;
	}
	rtcm->teseo.geo_sep = rtcm_getbits(rtcm->buff, i, 15) * 0.01;
	i += 15;
	if (fabs(rtcm->teseo.geo_sep) > 100.0)
	{
		rtcm->teseo.geo_sep = 0.0;
	}
	rtcm->teseo.age = rtcm_getbits(rtcm->buff, i, 24) * 0.001;
	i += 24;
	/* Differential Reference Station ID */
	i += 12;
	gnss_idx = rtcm_getbitu(rtcm->buff, i, 4);
	i += 4;
	if (gnss_idx > 11)
	{
		rtcm->teseo.sys = SYS_NONE;
	}
	else
	{
		rtcm->teseo.sys = GNSS_ID[gnss_idx];
	}

	/* decode time validity for version 5.8.12 ... */
	if (fw_ver == 812)
	{
		time_vail = rtcm_getbitu(rtcm->buff, i, 4);
		i += 4;
	}

	tow = rtcm_getbitu(rtcm->buff, i, 30) * 0.001;
	i += 30;
	week = rtcm_getbitu(rtcm->buff, i, 16);
	i += 16;
	if ((week > 1024) && (week < 4096) && (tow < (SECONDS_IN_WEEK + 0.1)))
	{
		rtcm->teseo.time = tow + (week * SECONDS_IN_WEEK);
		rtcm->time = gpst2time(week, tow);
	}
	else
	{
		rtcm->teseo.time = 0.0;
	}

	rtcm->teseo.leap_sec = rtcm_getbitu(rtcm->buff, i, 8);
	i += 8;
	blh[0] = ((rtcm_getbits(rtcm->buff, i, 32) * 0.001) / 3600) * D2R;
	i += 32;
	blh[1] = ((rtcm_getbits(rtcm->buff, i, 32) * 0.001) / 3600) * D2R;
	i += 32;

	/* decode position and velocity for version 5.8.11 */
	if (fw_ver == 811)
	{
		blh[2] = rtcm_getbits(rtcm->buff, i, 16) * 0.1;
		i += 16;
		if (fabs(blh[2]) > 3276.6)
		{
			rtcm->teseo.fix_status = 0;
		}
		vel_horz = (rtcm_getbits(rtcm->buff, i, 16) * 0.01 * 1000.0) / 3600.0;
		i += 16;
		vel_ver = (rtcm_getbits(rtcm->buff, i, 16) * 0.01 * 1000.0) / 3600.0;
		i += 16;
	}
	else
	{
		blh[2] = rtcm_getbits(rtcm->buff, i, 20) * 0.1;
		i += 20;
		if (fabs(blh[2]) > 18000.0)
		{
			rtcm->teseo.fix_status = 0;
		}
		vel_horz = rtcm_getbits(rtcm->buff, i, 20) * 0.01;
		i += 20;
		vel_ver = rtcm_getbits(rtcm->buff, i, 20) * 0.01;
		i += 20;
	}

	cour_anl = rtcm_getbits(rtcm->buff, i, 16);
	i += 16;
	if (fabs(cour_anl) > 360.0)
	{
		cour_anl = 0.0;
	}
	if (cour_anl < 0.0)
	{
		cour_anl += 360.0;
	}

	//blh[2] += rtcm->teseo.geo_sep;
	pos2ecef(blh, rtcm->teseo.pos);
	vel_enu[0] = vel_horz * sin(cour_anl*D2R);
	vel_enu[1] = vel_horz * cos(cour_anl*D2R);
	vel_enu[2] = vel_ver;
	enu2ecef(blh, vel_enu, rtcm->teseo.vel);

	plh = rtcm_getbitu(rtcm->buff, i, 16) * 0.01;
	i += 16;
	plv = rtcm_getbitu(rtcm->buff, i, 16) * 0.01;
	i += 16;
	pla = rtcm_getbits(rtcm->buff, i, 16) * 0.01;
	if (pla < 0.0)
	{
		pla += 360.0;
	}
	i += 16;

	pl_enu[0] = plh * sin(pla*D2R);
	pl_enu[1] = plh * cos(pla*D2R);
	pl_enu[2] = plv;
	enu2ecef(blh, pl_enu, rtcm->teseo.lev_pos);

	rtcm->teseo.cbias = rtcm_getbits(rtcm->buff, i, 32) * 0.001;
	i += 32;
	rtcm->teseo.cdt = rtcm_getbits(rtcm->buff, i, 32) * 0.01;
	i += 32;

	obs->rtcmtype = 99;
	obs->fwver = fw_ver;

	return;
}

/* decode RF Status */
RTK_RAM_CODE static void decode_type999_id24(rtcm_t *rtcm, obs_t *obs)
{
	double tow;
	int i = 24, mesnum, subtypeID, sys = _SYS_NONE_;
	unsigned int gnssID;

	mesnum = (int)rtcm_getbitu(rtcm->buff, i, 12);
	i += 12;
	subtypeID = (int)rtcm_getbitu(rtcm->buff, i, 8);
	i += 8;
	tow = (double)rtcm_getbitu(rtcm->buff, i, 30);
	i += 30;
	gnssID = (int)rtcm_getbitu(rtcm->buff, i, 4);
	i += 4;
	if (gnssID > 11)
		return;
	sys = GNSS_ID[gnssID];

	return;
}

/* decode Signal Quality Metrics 2 */
RTK_RAM_CODE static void decode_type999_id26(rtcm_t *rtcm, obs_t *obs)
{
	double tow, metValue[MAXOBS * 8];
	int i = 24, j, nsat = 0, nmet = 0, temps, mesnum, subtypeID;
	unsigned int gnssID, gnssSatM, metMask, mmind;
	int prn[40] = { 0 }, metrics[32] = { 0 }, sys = _SYS_NONE_;
	int sat, f;
	unsigned char stdest;
	//double stdest;

	mesnum = (int)rtcm_getbitu(rtcm->buff, i, 12);
	i += 12;
	subtypeID = (int)rtcm_getbitu(rtcm->buff, i, 8);
	i += 8;
	tow = (double)rtcm_getbitu(rtcm->buff, i, 30);
	i += 30;
	gnssID = (int)rtcm_getbitu(rtcm->buff, i, 4);
	i += 4;

	if (gnssID > 11)
	{
		return;
	}

	sys = GNSS_ID[gnssID];

	for (j = 0; j < 40; j++)
	{
		gnssSatM = (unsigned int)rtcm_getbitu(rtcm->buff, i, 1);
		i += 1;
		if (gnssSatM)
		{
			prn[nsat++] = j + 1;
		}
	}

	for (j = 0; j < 32; j++)
	{
		metMask = (unsigned int)rtcm_getbitu(rtcm->buff, i, 1);
		if (metMask)
		{
			metrics[nmet++] = 31 - j;
		}
		i += 1;
	}
	for (j = 0; j < (int)nmet*0.5; j++)
	{
		temps = metrics[j];
		metrics[j] = metrics[nmet - j - 1];
		metrics[nmet - j - 1] = temps;
	}

	mmind = (unsigned int)rtcm_getbitu(rtcm->buff, i, 1);
	i += 1;

	for (j = 0; j < nsat && j < (int)MAXOBS*0.5; j++)
	{
		metValue[j * 16 + 0] = rtcm_getbitu(rtcm->buff, i, 8);		        i += 8;
		metValue[j * 16 + 1] = rtcm_getbitu(rtcm->buff, i, 16)*D2R*0.001;	i += 16;
		metValue[j * 16 + 2] = rtcm_getbitu(rtcm->buff, i, 16)*0.01;		i += 16;
		metValue[j * 16 + 3] = rtcm_getbits(rtcm->buff, i, 16)*0.01;		i += 16;
		metValue[j * 16 + 4] = rtcm_getbits(rtcm->buff, i, 16);				i += 16;
		metValue[j * 16 + 5] = rtcm_getbitu(rtcm->buff, i, 32);				i += 32;
		//metValue[j * 16 + 6] = rtcm_getbitu(rtcm->buff, i, 8);	i += 8;
		//metValue[j * 16 + 7] = rtcm_getbitu(rtcm->buff, i, 8);	i += 8;
		//metValue[j * 16 + 8] = rtcm_getbitu(rtcm->buff, i, 8);	i += 8;
		//metValue[j * 16 + 9] = rtcm_getbitu(rtcm->buff, i, 8);	i += 8;
		metValue[j * 16 + 10] = rtcm_getbitu(rtcm->buff, i, 8);		i += 8;
		metValue[j * 16 + 11] = rtcm_getbitu(rtcm->buff, i, 16)*D2R*0.001;	i += 16;
		metValue[j * 16 + 12] = rtcm_getbitu(rtcm->buff, i, 16)*0.01;		i += 16;
		metValue[j * 16 + 13] = rtcm_getbits(rtcm->buff, i, 16)*0.01;		i += 16;
		metValue[j * 16 + 14] = rtcm_getbits(rtcm->buff, i, 16);			i += 16;
		metValue[j * 16 + 15] = rtcm_getbitu(rtcm->buff, i, 32);			i += 32;
	}

	for (j = 0; j < nsat; j++)
	{
		sat = satno(sys, prn[j]);
		if (sat <= 0 || sat > MAXSAT)
		{
			continue;
		}

		for (f = 0; f < NFREQ; f++)
		{
			if (fabs(metValue[j * 16 + f * 10 + 1]) < 0.15)
			{
				stdest = 1;
			}
			else
			{
				stdest = 4;
			}
			//stdest = metValue[j * 16 + f * 10 + 1];
			rtcm->teseo.sigqm2[f * MAXSAT + sat - 1][1] = stdest;

			if ((metValue[j * 16 + f * 10 + 2] < 100.0 && metValue[j * 16 + f * 10 + 2] > 10.0) ||
				metValue[j * 16 + f * 10 + 2] > 655.34)
			{
				stdest = 4;
			}
			else
			{
				stdest = 1;
			}
			//stdest = metValue[j * 16 + f * 10 + 2];
			rtcm->teseo.sigqm2[f * MAXSAT + sat - 1][0] = stdest;
		}

	}

	return;
}


RTK_RAM_CODE static void decode_type999_id30(rtcm_t *rtcm)
{
	int i = 74;

	rtcm->teseo.rms = (double)rtcm_getbitu(rtcm->buff, i, 20) * 0.01f;
	i += 20;
	rtcm->teseo.std_ellipse[0] = (double)rtcm_getbitu(rtcm->buff, i, 20) * 0.01f;
	i += 20;
	rtcm->teseo.std_ellipse[1] = (double)rtcm_getbitu(rtcm->buff, i, 20) * 0.01f;
	i += 20;
	rtcm->teseo.std_ellipse[2] = (double)rtcm_getbitu(rtcm->buff, i, 16) * 0.1f;
	i += 16;

	rtcm->teseo.std_pos[0] = (double)rtcm_getbitu(rtcm->buff, i, 20) * 0.01f;
	i += 20;
	rtcm->teseo.std_pos[1] = (double)rtcm_getbitu(rtcm->buff, i, 20) * 0.01f;
	i += 20;
	rtcm->teseo.std_pos[2] = (double)rtcm_getbitu(rtcm->buff, i, 20) * 0.01f;
	i += 20;

	rtcm->teseo.std_vel[0] = (double)rtcm_getbitu(rtcm->buff, i, 20) * 0.001f;
	i += 20;
	rtcm->teseo.std_vel[1] = (double)rtcm_getbitu(rtcm->buff, i, 20) * 0.001f;
	i += 20;
	rtcm->teseo.std_vel[2] = (double)rtcm_getbitu(rtcm->buff, i, 20) * 0.001f;
}


RTK_RAM_CODE static int decode_type999(rtcm_t *rtcm, obs_t *obs)
{

    int i = 24, sbu_type_ID = 0;

    i += 12;
    sbu_type_ID = rtcm_getbitu(rtcm->buff, i, 8);
	double tow = time2gpst(rtcm->time, NULL);
	//printf("ID:%2d, Time:%10.2lf\n", sbu_type_ID, tow);

    i += 8;

	switch (sbu_type_ID)
	{
		case 1:  decode_type999_id1(rtcm, obs); break;
		case 4:  decode_type999_id4(rtcm, obs); break;
		case 6:  decode_type999_id6(rtcm, obs); break;
		//case 8:  decode_type999_id8 (rtcm, obs); break;
		//case 9:  decode_type999_id9 (rtcm, obs); break;
		case 21: decode_type999_id21(rtcm, obs); break;
		//case 24: decode_type999_id24(rtcm, obs); break;
		case 26: decode_type999_id26(rtcm, obs); break;
		case 30: decode_type999_id30(rtcm);       break;
		default: break;
	}

    return 0;
}
/* decode type 1001-1004 message header --------------------------------------*/
RTK_RAM_CODE static int decode_head1001(rtcm_t *rtcm, obs_t *obs, int *sync)
{
    double tow;
    char tstr[64];
    int i = 24;
    int staid, nsat, type;

    type = rtcm_getbitu(rtcm->buff, i, 12);
    i += 12;

    if (i + 52 <= rtcm->len * 8)
    {
        staid = rtcm_getbitu(rtcm->buff, i, 12);
        i += 12;
        tow = rtcm_getbitu(rtcm->buff, i, 30) * 0.001;
        i += 30;
        *sync = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
        nsat = rtcm_getbitu(rtcm->buff, i, 5);
    }
    else
    {
        return -1;
    }
    /* test station id */
    if (!test_staid(obs, staid))
        return -1;

    adjweek(&rtcm->time, tow);

    time2str(rtcm->time, tstr, 2);

    return nsat;
}

/* decode type 1001: L1-only gps rtk observation -----------------------------*/
RTK_RAM_CODE static int decode_type1001(rtcm_t *rtcm, obs_t *obs)
{
    int sync;
    if (decode_head1001(rtcm, obs, &sync) < 0)
        return -1;
    obs->obsflag = !sync;
    return sync ? 0 : 1;
}

/* decode type 1002: extended L1-only gps rtk observables --------------------*/
RTK_RAM_CODE static int decode_type1002(rtcm_t *rtcm, obs_t *obs)
{
    double pr1, cnr1, /*tt,*/ cp1;
    int i = 24 + 64, j = 0;
    int index, nsat, sync, prn, code, sat, ppr1, lock1, amb, sys;

    if ((nsat = decode_head1001(rtcm, obs, &sync)) < 0)
        return -1;

    for (j = 0; j < nsat && obs->n < MAXOBS && i + 74 <= rtcm->len * 8; j++)
    {
        prn = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;
        code = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
        pr1 = rtcm_getbitu(rtcm->buff, i, 24);
        i += 24;
        ppr1 = rtcm_getbits(rtcm->buff, i, 20);
        i += 20;
        lock1 = rtcm_getbitu(rtcm->buff, i, 7);
        i += 7;
        amb = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        cnr1 = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        if (prn < 40)
        {
            sys = _SYS_GPS_;
        }
        else
        {
            sys = _SYS_SBS_;
            prn += 80;
        }
        if (!(sat = satno(sys, prn)))
        {
            continue;
        }
        if ((index = obsindex(obs, rtcm->time, sat)) < 0)
            continue;
        pr1 = pr1 * 0.02 + amb * PRUNIT_GPS;
        if (ppr1 != (int)0xFFF80000)
        {
            obs->data[index].P[0] = pr1;
            cp1 = adjcp(rtcm, sat, 0, ppr1 * 0.0005 / lam_carr[0]);
            obs->data[index].L[0] = pr1 / lam_carr[0] + cp1;
        }
        obs->data[index].LLI[0] = (unsigned char)lossoflock(rtcm, sat, 0, lock1);
        obs->data[index].SNR[0] = snratio(cnr1 * 0.25);
        obs->data[index].code[0] = code ? CODE_L1P : CODE_L1C;
    }
    return sync ? 0 : 1;
}

/* decode type 1003: L1&L2 gps rtk observables -------------------------------*/
RTK_RAM_CODE static int decode_type1003(rtcm_t *rtcm, obs_t *obs)
{
    int sync;
    if (decode_head1001(rtcm, obs, &sync) < 0)
        return -1;
    obs->obsflag = !sync;
    return sync ? 0 : 1;
}

/* decode type 1004: extended L1&L2 gps rtk observables ----------------------*/
RTK_RAM_CODE static int decode_type1004(rtcm_t *rtcm, obs_t *obs)
{
    const int L2codes[] = {CODE_L2X, CODE_L2P, CODE_L2D, CODE_L2W};
    double pr1, cnr1, cnr2, /*tt,*/ cp1, cp2;
    int i = 24 + 64, j, index, nsat, sync, prn, sat, code1, code2, pr21, ppr1, ppr2;
    int lock1, lock2, amb, sys;

    if ((nsat = decode_head1001(rtcm, obs, &sync)) < 0)
        return -1;

    for (j = 0; j < nsat && obs->n < MAXOBS && (i + 125) <= rtcm->len * 8; j++)
    {
        prn = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;
        code1 = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
        pr1 = rtcm_getbitu(rtcm->buff, i, 24);
        i += 24;
        ppr1 = rtcm_getbits(rtcm->buff, i, 20);
        i += 20;
        lock1 = rtcm_getbitu(rtcm->buff, i, 7);
        i += 7;
        amb = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        cnr1 = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        code2 = rtcm_getbitu(rtcm->buff, i, 2);
        i += 2;
        pr21 = rtcm_getbits(rtcm->buff, i, 14);
        i += 14;
        ppr2 = rtcm_getbits(rtcm->buff, i, 20);
        i += 20;
        lock2 = rtcm_getbitu(rtcm->buff, i, 7);
        i += 7;
        cnr2 = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        if (prn < 40)
        {
            sys = _SYS_GPS_;
        }
        else
        {
            sys = _SYS_SBS_;
            prn += 80;
        }
        if (!(sat = satno(sys, prn)))
        {
            continue;
        }
        if ((index = obsindex(obs, rtcm->time, sat)) < 0)
            continue;
        pr1 = pr1 * 0.02 + amb * PRUNIT_GPS;
        if (ppr1 != (int)0xFFF80000)
        {
            obs->data[index].P[0] = pr1;
            cp1 = adjcp(rtcm, sat, 0, ppr1 * 0.0005 / lam_carr[0]);
            obs->data[index].L[0] = pr1 / lam_carr[0] + cp1;
        }
        obs->data[index].LLI[0] = (unsigned char)lossoflock(rtcm, sat, 0, lock1);
        obs->data[index].SNR[0] = snratio(cnr1 * 0.25);
        obs->data[index].code[0] = code1 ? CODE_L1P : CODE_L1C;

        if (pr21 != (int)0xFFFFE000)
        {
            obs->data[index].P[1] = pr1 + pr21 * 0.02;
        }
        if (ppr2 != (int)0xFFF80000)
        {
            cp2 = adjcp(rtcm, sat, 1, ppr2 * 0.0005 / lam_carr[1]);
            obs->data[index].L[1] = pr1 / lam_carr[1] + cp2;
        }
        obs->data[index].LLI[1] = (unsigned char)lossoflock(rtcm, sat, 1, lock2);
        obs->data[index].SNR[1] = snratio(cnr2 * 0.25);
        obs->data[index].code[1] = (unsigned char)L2codes[code2];
    }
    obs->obsflag = !sync;
    return sync ? 0 : 1;
}

/* decode type 1005: stationary rtk reference station arp --------------------*/
RTK_RAM_CODE static int decode_type1005(rtcm_t *rtcm, obs_t *obs)
{
    double rr[3];
    int i = 24 + 12, j, staid /*,itrf*/;

    if (i + 140 == rtcm->len * 8)
    {
        staid = rtcm_getbitu(rtcm->buff, i, 12);
        i += 12;
        /*itrf =rtcm_getbitu(rtcm->buff,i, 6);*/ i += 6 + 4;
        rr[0] = rtcm_getbits_38(rtcm->buff, i);
        i += 38 + 2;
        rr[1] = rtcm_getbits_38(rtcm->buff, i);
        i += 38 + 2;
        rr[2] = rtcm_getbits_38(rtcm->buff, i);
    }
    else
    {
        return -1;
    }
    /* test station id */
    if (!test_staid(obs, staid))
        return -1;

    for (j = 0; j < 3; j++)
    {
        obs->pos[j] = rr[j] * 0.0001;
    }
    return 5;
}

/* decode type 1006: stationary rtk reference station arp with height --------*/
RTK_RAM_CODE static int decode_type1006(rtcm_t *rtcm, obs_t *obs)
{
    double rr[3] /*,anth*/;
    int i = 24 + 12, j, staid, itrf;

    if (i + 156 <= (int)rtcm->len * 8)
    {
        staid = rtcm_getbitu(rtcm->buff, i, 12);
        i += 12;
        itrf = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6 + 4;
        rr[0] = rtcm_getbits_38(rtcm->buff, i);
        i += 38 + 2;
        rr[1] = rtcm_getbits_38(rtcm->buff, i);
        i += 38 + 2;
        rr[2] = rtcm_getbits_38(rtcm->buff, i);
        i += 38;
        /*anth =rtcm_getbitu(rtcm->buff,i,16);*/
    }
    else
    {
        return -1;
    }
    /* test station id */
    if (!test_staid(obs, staid))
        return -1;

    for (j = 0; j < 3; j++)
    {
        obs->pos[j] = rr[j] * 0.0001;
    }
    return 5;
}

/* decode type 1007: antenna descriptor --------------------------------------*/
RTK_RAM_CODE static int decode_type1007(rtcm_t *rtcm, obs_t *obs)
{
    char des[32] = "";
    int i = 24 + 12, j, staid, n, setup;

    n = rtcm_getbitu(rtcm->buff, i + 12, 8);

    if (i + 28 + 8 * n <= (int)rtcm->len * 8)
    {
        staid = rtcm_getbitu(rtcm->buff, i, 12);
        i += 12 + 8;
        for (j = 0; j < n && j < 31; j++)
        {
            des[j] = (char)rtcm_getbitu(rtcm->buff, i, 8);
            i += 8;
        }
        setup = rtcm_getbitu(rtcm->buff, i, 8);
    }
    else
    {
        return -1;
    }
    /* test station id */
    if (!test_staid(obs, staid))
        return -1;

    return 5;
}

/* decode type 1008: antenna descriptor & serial number ----------------------*/
RTK_RAM_CODE static int decode_type1008(rtcm_t *rtcm, obs_t *obs)
{
    /*char des[32]="",sno[32]="";*/
    int i = 24 + 12, j, staid, n, m;

    n = rtcm_getbitu(rtcm->buff, i + 12, 8);
    m = rtcm_getbitu(rtcm->buff, i + 28 + 8 * n, 8);

    if (i + 36 + 8 * (n + m) <= (int)rtcm->len * 8)
    {
        staid = rtcm_getbitu(rtcm->buff, i, 12);
        i += 12 + 8;
        for (j = 0; j < n && j < 31; j++)
        {
            /*des[j]=(char)rtcm_getbitu(rtcm->buff,i,8);*/ i += 8;
        }
        /*setup=rtcm_getbitu(rtcm->buff,i, 8);*/ i += 8 + 8;
        for (j = 0; j < m && j < 31; j++)
        {
            /*sno[j]=(char)rtcm_getbitu(rtcm->buff,i,8);*/ i += 8;
        }
    }
    else
    {
        return -1;
    }
    /* test station id */
    if (!test_staid(obs, staid))
        return -1;

    return 5;
}

/* decode type 1009-1012 message header --------------------------------------*/
RTK_RAM_CODE static int decode_head1009(rtcm_t *rtcm, obs_t *obs, int *sync)
{
    double tod;
    char tstr[64];
    int i = 24, staid, nsat, type;

    type = rtcm_getbitu(rtcm->buff, i, 12);
    i += 12;

    if (i + 49 <= (int)rtcm->len * 8)
    {
        staid = rtcm_getbitu(rtcm->buff, i, 12);
        i += 12;
        tod = rtcm_getbitu(rtcm->buff, i, 27) * 0.001;
        i += 27; /* sec in a day */
        *sync = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
        nsat = rtcm_getbitu(rtcm->buff, i, 5);
    }
    else
    {
        return -1;
    }
    /* test station id */
    if (!test_staid(obs, staid))
        return -1;

    adjday_glot(&rtcm->time, tod);

    time2str(rtcm->time, tstr, 2);

    return nsat;
}

/* decode type 1009: L1-only glonass rtk observables -------------------------*/
RTK_RAM_CODE static int decode_type1009(rtcm_t *rtcm, obs_t *obs)
{
    int sync;
    if (decode_head1009(rtcm, obs, &sync) < 0)
        return -1;
    obs->obsflag = !sync;
    return sync ? 0 : 1;
}

/* decode type 1010: extended L1-only glonass rtk observables ----------------*/
RTK_RAM_CODE static int decode_type1010(rtcm_t *rtcm, obs_t *obs)
{
    double pr1, cnr1, cp1, lam1;
    int i = 24 + 61, j, index, nsat, sync, prn, sat, code, freq, ppr1, lock1, amb, sys = _SYS_GLO_;

    if ((nsat = decode_head1009(rtcm, obs, &sync)) < 0)
        return -1;

    for (j = 0; j < nsat && obs->n < MAXOBS && i + 79 <= (int)rtcm->len * 8; j++)
    {
        prn = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;
        code = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
        freq = rtcm_getbitu(rtcm->buff, i, 5);
        i += 5;
        pr1 = rtcm_getbitu(rtcm->buff, i, 25);
        i += 25;
        ppr1 = rtcm_getbits(rtcm->buff, i, 20);
        i += 20;
        lock1 = rtcm_getbitu(rtcm->buff, i, 7);
        i += 7;
        amb = rtcm_getbitu(rtcm->buff, i, 7);
        i += 7;
        cnr1 = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        if (!(sat = satno(sys, prn)))
        {
            continue;
        }
        if ((index = obsindex(obs, rtcm->time, sat)) < 0)
            continue;
        pr1 = pr1 * 0.02 + amb * PRUNIT_GLO;
        if (ppr1 != (int)0xFFF80000)
        {
            obs->data[index].P[0] = pr1;
            lam1 = CLIGHT / (FREQ1_GLO + DFRQ1_GLO * (freq - 7));
            cp1 = adjcp(rtcm, sat, 0, ppr1 * 0.0005 / lam1);
            obs->data[index].L[0] = pr1 / lam1 + cp1;
        }
        obs->data[index].LLI[0] = (unsigned char)lossoflock(rtcm, sat, 0, lock1);
        obs->data[index].SNR[0] = snratio(cnr1 * 0.25);
        obs->data[index].code[0] = code ? CODE_L1P : CODE_L1C;
    }
    obs->obsflag = !sync;
    return sync ? 0 : 1;
}

/* decode type 1011: L1&L2 glonass rtk observables ---------------------------*/
RTK_RAM_CODE static int decode_type1011(rtcm_t *rtcm, obs_t *obs)
{
    int sync;
    if (decode_head1009(rtcm, obs, &sync) < 0)
        return -1;
    obs->obsflag = !sync;
    return sync ? 0 : 1;
}

/* decode type 1012: extended L1&L2 glonass rtk observables ------------------*/
RTK_RAM_CODE static int decode_type1012(rtcm_t *rtcm, obs_t *obs)
{
    double pr1, cnr1, cnr2, cp1, cp2, lam1, lam2;
    int i = 24 + 61, j, index, nsat, sync, prn, sat, freq, code1, code2, pr21, ppr1, ppr2;
    int lock1, lock2, amb, sys = _SYS_GLO_;

    if ((nsat = decode_head1009(rtcm, obs, &sync)) < 0)
        return -1;

    for (j = 0; j < nsat && obs->n < MAXOBS && i + 130 <= (int)rtcm->len * 8; j++)
    {
        prn = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;
        code1 = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
        freq = rtcm_getbitu(rtcm->buff, i, 5);
        i += 5;
        pr1 = rtcm_getbitu(rtcm->buff, i, 25);
        i += 25;
        ppr1 = rtcm_getbits(rtcm->buff, i, 20);
        i += 20;
        lock1 = rtcm_getbitu(rtcm->buff, i, 7);
        i += 7;
        amb = rtcm_getbitu(rtcm->buff, i, 7);
        i += 7;
        cnr1 = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        code2 = rtcm_getbitu(rtcm->buff, i, 2);
        i += 2;
        pr21 = rtcm_getbits(rtcm->buff, i, 14);
        i += 14;
        ppr2 = rtcm_getbits(rtcm->buff, i, 20);
        i += 20;
        lock2 = rtcm_getbitu(rtcm->buff, i, 7);
        i += 7;
        cnr2 = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        if (!(sat = satno(sys, prn)))
        {
            continue;
        }
        if ((index = obsindex(obs, rtcm->time, sat)) < 0)
            continue;
        pr1 = pr1 * 0.02 + amb * PRUNIT_GLO;
        if (ppr1 != (int)0xFFF80000)
        {
            lam1 = CLIGHT / (FREQ1_GLO + DFRQ1_GLO * (freq - 7));
            obs->data[index].P[0] = pr1;
            cp1 = adjcp(rtcm, sat, 0, ppr1 * 0.0005 / lam1);
            obs->data[index].L[0] = pr1 / lam1 + cp1;
        }
        obs->data[index].LLI[0] = (unsigned char)lossoflock(rtcm, sat, 0, lock1);
        obs->data[index].SNR[0] = snratio(cnr1 * 0.25);
        obs->data[index].code[0] = code1 ? CODE_L1P : CODE_L1C;

        if (pr21 != (int)0xFFFFE000)
        {
            obs->data[index].P[1] = pr1 + pr21 * 0.02;
        }
        if (ppr2 != (int)0xFFF80000)
        {
            lam2 = CLIGHT / (FREQ2_GLO + DFRQ2_GLO * (freq - 7));
            cp2 = adjcp(rtcm, sat, 1, ppr2 * 0.0005 / lam2);
            obs->data[index].L[1] = pr1 / lam2 + cp2;
        }
        obs->data[index].LLI[1] = (unsigned char)lossoflock(rtcm, sat, 1, lock2);
        obs->data[index].SNR[1] = snratio(cnr2 * 0.25);
        obs->data[index].code[1] = code2 ? CODE_L2P : CODE_L2C;
    }
    obs->obsflag = !sync;
    return sync ? 0 : 1;
}

/* decode type 1013: system parameters ---------------------------------------*/
RTK_RAM_CODE static int decode_type1013(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1019: gps ephemerides -----------------------------------------*/
RTK_RAM_CODE static int decode_type1019(rtcm_t *rtcm, nav_t *nav)
{
    eph_t eph = {0};
    double toc, sqrtA;
    int i = 24 + 12, prn, sat, week, sys = _SYS_GPS_;

    if (i + 476 <= (int)rtcm->len * 8)
    {
        prn = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;
        week = rtcm_getbitu(rtcm->buff, i, 10);
        i += 10;
        eph.sva = rtcm_getbitu(rtcm->buff, i, 4);
        i += 4;
        eph.code = rtcm_getbitu(rtcm->buff, i, 2);
        i += 2;
        eph.idot = rtcm_getbits(rtcm->buff, i, 14) * P2_43 * SC2RAD;
        i += 14;
        eph.iode = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        toc = rtcm_getbitu(rtcm->buff, i, 16) * 16.0;
        i += 16;
        eph.f2 = rtcm_getbits(rtcm->buff, i, 8) * P2_55;
        i += 8;
        eph.f1 = rtcm_getbits(rtcm->buff, i, 16) * P2_43;
        i += 16;
        eph.f0 = rtcm_getbits(rtcm->buff, i, 22) * P2_31;
        i += 22;
        eph.iodc = rtcm_getbitu(rtcm->buff, i, 10);
        i += 10;
        eph.crs = rtcm_getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.deln = rtcm_getbits(rtcm->buff, i, 16) * P2_43 * SC2RAD;
        i += 16;
        eph.M0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cuc = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.e = rtcm_getbitu(rtcm->buff, i, 32) * P2_33;
        i += 32;
        eph.cus = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        sqrtA = rtcm_getbitu(rtcm->buff, i, 32) * P2_19;
        i += 32;
        eph.toes = rtcm_getbitu(rtcm->buff, i, 16) * 16.0;
        i += 16;
        eph.cic = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.OMG0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cis = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.i0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.crc = rtcm_getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.omg = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.OMGd = rtcm_getbits(rtcm->buff, i, 24) * P2_43 * SC2RAD;
        i += 24;
        eph.tgd[0] = rtcm_getbits(rtcm->buff, i, 8) * P2_31;
        i += 8;
        eph.svh = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;
        eph.flag = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
        eph.fit = rtcm_getbitu(rtcm->buff, i, 1) ? 0.0 : 4.0; /* 0:4hr,1:>4hr */
    }
    else
    {
        return -1;
    }
    if (prn >= 40)
    {
        sys = _SYS_SBS_;
        prn += 80;
    }

    if (!(sat = satno(sys, prn)))
    {
        return -1;
    }
    eph.sat = sat;
    eph.week = adjgpsweek(&rtcm->time, week);
    eph.toe = gpst2time(eph.week, eph.toes);
    eph.toc = gpst2time(eph.week, toc);

#ifdef _POST_RTK_
#ifdef ARM_MCU
    eph.ttr = timeget();
#else
    eph.ttr = rtcm->time;
#endif
#endif
    eph.A = sqrtA * sqrtA;

   	if (add_eph(&eph, nav)==1)
		++nav->n_gps;

    return 2;
}
/* decode type 1020: glonass ephemerides -------------------------------------*/
static int decode_type1020(rtcm_t *rtcm, nav_t *nav)
{
    geph_t geph = {0};
    double tk_h, tk_m, tk_s, toe, tow, tod, tof;
    int i = 24 + 12, prn, sat, week, tb, bn, sys = _SYS_GLO_;

    if (i + 348 <= (int)rtcm->len * 8)
    {
        prn = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;
        geph.frq = rtcm_getbitu(rtcm->buff, i, 5) - 7;
        i += 5 + 2 + 2;
        tk_h = rtcm_getbitu(rtcm->buff, i, 5);
        i += 5;
        tk_m = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;
        tk_s = rtcm_getbitu(rtcm->buff, i, 1) * 30.0;
        i += 1;
        bn = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1 + 1;
        tb = rtcm_getbitu(rtcm->buff, i, 7);
        i += 7;
        geph.vel[0] = getbitg(rtcm->buff, i, 24) * P2_20 * 1E3;
        i += 24;
        geph.pos[0] = getbitg(rtcm->buff, i, 27) * P2_11 * 1E3;
        i += 27;
        geph.acc[0] = getbitg(rtcm->buff, i, 5) * P2_30 * 1E3;
        i += 5;
        geph.vel[1] = getbitg(rtcm->buff, i, 24) * P2_20 * 1E3;
        i += 24;
        geph.pos[1] = getbitg(rtcm->buff, i, 27) * P2_11 * 1E3;
        i += 27;
        geph.acc[1] = getbitg(rtcm->buff, i, 5) * P2_30 * 1E3;
        i += 5;
        geph.vel[2] = getbitg(rtcm->buff, i, 24) * P2_20 * 1E3;
        i += 24;
        geph.pos[2] = getbitg(rtcm->buff, i, 27) * P2_11 * 1E3;
        i += 27;
        geph.acc[2] = getbitg(rtcm->buff, i, 5) * P2_30 * 1E3;
        i += 5 + 1;
        geph.gamn = getbitg(rtcm->buff, i, 11) * P2_40;
        i += 11 + 3;
        geph.taun = getbitg(rtcm->buff, i, 22) * P2_30;

        set_glo_frq(prn, geph.frq);
    }
    else
    {
        return -1;
    }
    if (!(sat = satno(sys, prn)))
    {
        return -1;
    }

    geph.sat = sat;
    geph.svh = bn;
    geph.iode = tb & 0x7F;
    if (rtcm->time.time == 0)
        rtcm->time = utc2gpst(timeget());
    tow = time2gpst(gpst2utc(rtcm->time), &week);
    tod = fmod(tow, 86400.0);
    tow -= tod;
    tof = tk_h * 3600.0 + tk_m * 60.0 + tk_s - 10800.0; /* lt->utc */
    if (tof < tod - 43200.0)
        tof += 86400.0;
    else if (tof > tod + 43200.0)
        tof -= 86400.0;
#ifdef _POST_RTK_
	geph.tof = utc2gpst(gpst2time(week, tow + tof));
#endif // _POST_RTK_    
    toe = tb * 900.0 - 10800.0; /* lt->utc */
    if (toe < tod - 43200.0)
        toe += 86400.0;
    else if (toe > tod + 43200.0)
        toe -= 86400.0;
    geph.toe = utc2gpst(gpst2time(week, tow + toe)); /* utc->gpst */

    add_geph(&geph, nav);
    
    return 2;
}
/* decode type 1021: helmert/abridged molodenski -----------------------------*/
RTK_RAM_CODE static int decode_type1021(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1022: moledenski-badekas transfromation -----------------------*/
RTK_RAM_CODE static int decode_type1022(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1023: residual, ellipoidal grid representation ----------------*/
RTK_RAM_CODE static int decode_type1023(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1024: residual, plane grid representation ---------------------*/
RTK_RAM_CODE static int decode_type1024(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1025: projection (types except LCC2SP,OM) ---------------------*/
RTK_RAM_CODE static int decode_type1025(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1026: projection (LCC2SP - lambert conic conformal (2sp)) -----*/
RTK_RAM_CODE static int decode_type1026(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1027: projection (type OM - oblique mercator) -----------------*/
RTK_RAM_CODE static int decode_type1027(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1029: unicode text string -------------------------------------*/
RTK_RAM_CODE static int decode_type1029(rtcm_t *rtcm)
{
    int i = 24 + 12, staid, mjd, tod, nchar, cunit;

    if (i + 60 <= (int)rtcm->len * 8)
    {
        staid = rtcm_getbitu(rtcm->buff, i, 12);
        i += 12;
        mjd = rtcm_getbitu(rtcm->buff, i, 16);
        i += 16;
        tod = rtcm_getbitu(rtcm->buff, i, 17);
        i += 17;
        nchar = rtcm_getbitu(rtcm->buff, i, 7);
        i += 7;
        cunit = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
    }
    else
    {
        return -1;
    }
    if (i + nchar * 8 > (int)rtcm->len * 8)
    {
        return -1;
    }
    return 0;
}
/* decode type 1030: network rtk residual ------------------------------------*/
RTK_RAM_CODE static int decode_type1030(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1031: glonass network rtk residual ----------------------------*/
RTK_RAM_CODE static int decode_type1031(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1032: physical reference station position information ---------*/
RTK_RAM_CODE static int decode_type1032(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1033: receiver and antenna descriptor -------------------------*/
RTK_RAM_CODE static int decode_type1033(rtcm_t *rtcm, obs_t *obs)
{
    char des[32] = "", sno[32] = "", rec[32] = "", ver[32] = "", rsn[32] = "";
    int i = 24 + 12, j, staid, n, m, n1, n2, n3, setup;

    n = rtcm_getbitu(rtcm->buff, i + 12, 8);
    m = rtcm_getbitu(rtcm->buff, i + 28 + 8 * n, 8);
    n1 = rtcm_getbitu(rtcm->buff, i + 36 + 8 * (n + m), 8);
    n2 = rtcm_getbitu(rtcm->buff, i + 44 + 8 * (n + m + n1), 8);
    n3 = rtcm_getbitu(rtcm->buff, i + 52 + 8 * (n + m + n1 + n2), 8);

    if (i + 60 + 8 * (n + m + n1 + n2 + n3) <= (int)rtcm->len * 8)
    {
        staid = rtcm_getbitu(rtcm->buff, i, 12);
        i += 12 + 8;
        for (j = 0; j < n && j < 31; j++)
        {
            des[j] = (char)rtcm_getbitu(rtcm->buff, i, 8);
            i += 8;
        }
        setup = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8 + 8;
        for (j = 0; j < m && j < 31; j++)
        {
            sno[j] = (char)rtcm_getbitu(rtcm->buff, i, 8);
            i += 8;
        }
        i += 8;
        for (j = 0; j < n1 && j < 31; j++)
        {
            rec[j] = (char)rtcm_getbitu(rtcm->buff, i, 8);
            i += 8;
        }
        i += 8;
        for (j = 0; j < n2 && j < 31; j++)
        {
            ver[j] = (char)rtcm_getbitu(rtcm->buff, i, 8);
            i += 8;
        }
        i += 8;
        for (j = 0; j < n3 && j < 31; j++)
        {
            rsn[j] = (char)rtcm_getbitu(rtcm->buff, i, 8);
            i += 8;
        }
    }
    else
    {
        return -1;
    }
    /* test station id */
    if (!test_staid(obs, staid))
        return -1;

    return 5;
}
/* decode type 1034: gps network fkp gradient --------------------------------*/
RTK_RAM_CODE static int decode_type1034(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1035: glonass network fkp gradient ----------------------------*/
RTK_RAM_CODE static int decode_type1035(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1037: glonass network rtk ionospheric correction difference ---*/
RTK_RAM_CODE static int decode_type1037(rtcm_t *rtcm)
{
    int i = 0;
    return 0;
}
/* decode type 1038: glonass network rtk geometic correction difference ------*/
RTK_RAM_CODE static int decode_type1038(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1039: glonass network rtk combined correction difference ------*/
RTK_RAM_CODE static int decode_type1039(rtcm_t *rtcm)
{
    return 0;
}
/* decode type 1044: qzss ephemerides (ref [15]) -----------------------------*/
RTK_RAM_CODE static int decode_type1044(rtcm_t *rtcm, nav_t *nav)
{
    eph_t eph = {0};
    double toc, sqrtA;
    int i = 24 + 12, prn, sat, week, sys = _SYS_QZS_;

    if (i + 473 <= (int)rtcm->len * 8)
    {
        prn = rtcm_getbitu(rtcm->buff, i, 4);
        i += 4;
        toc = rtcm_getbitu(rtcm->buff, i, 16) * 16.0;
        i += 16;
        eph.f2 = rtcm_getbits(rtcm->buff, i, 8) * P2_55;
        i += 8;
        eph.f1 = rtcm_getbits(rtcm->buff, i, 16) * P2_43;
        i += 16;
        eph.f0 = rtcm_getbits(rtcm->buff, i, 22) * P2_31;
        i += 22;
        eph.iode = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        eph.crs = rtcm_getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.deln = rtcm_getbits(rtcm->buff, i, 16) * P2_43 * SC2RAD;
        i += 16;
        eph.M0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cuc = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.e = rtcm_getbitu(rtcm->buff, i, 32) * P2_33;
        i += 32;
        eph.cus = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        sqrtA = rtcm_getbitu(rtcm->buff, i, 32) * P2_19;
        i += 32;
        eph.toes = rtcm_getbitu(rtcm->buff, i, 16) * 16.0;
        i += 16;
        eph.cic = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.OMG0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cis = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.i0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.crc = rtcm_getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.omg = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.OMGd = rtcm_getbits(rtcm->buff, i, 24) * P2_43 * SC2RAD;
        i += 24;
        eph.idot = rtcm_getbits(rtcm->buff, i, 14) * P2_43 * SC2RAD;
        i += 14;
        eph.code = rtcm_getbitu(rtcm->buff, i, 2);
        i += 2;
        week = rtcm_getbitu(rtcm->buff, i, 10);
        i += 10;
        eph.sva = rtcm_getbitu(rtcm->buff, i, 4);
        i += 4;
        eph.svh = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;
        eph.tgd[0] = rtcm_getbits(rtcm->buff, i, 8) * P2_31;
        i += 8;
        eph.iodc = rtcm_getbitu(rtcm->buff, i, 10);
        i += 10;
        eph.fit = rtcm_getbitu(rtcm->buff, i, 1) ? 0.0 : 2.0; /* 0:2hr,1:>2hr */
    }
    else
    {
        return -1;
    }

    if (!(sat = satno(sys, prn)))
    {
        return -1;
    }
    eph.sat = sat;
    eph.week = adjgpsweek(&rtcm->time, week);
    eph.toe = gpst2time(eph.week, eph.toes);
    eph.toc = gpst2time(eph.week, toc);
#ifdef _POST_RTK_
	eph.ttr = rtcm->time;
#endif // _POST_RTK_
    eph.A = sqrtA * sqrtA;
    
    /* do not use QZSS now */
#ifdef ENAQZS
	if (add_eph(&eph, nav) == 1)
		++nav->n_gps;
#endif

    return 2;
}
/* decode type 1045: galileo F/NAV satellite ephemerides (ref [15]) ----------*/
RTK_RAM_CODE static int decode_type1045(rtcm_t *rtcm, nav_t *nav)
{
    eph_t eph = {0};
    double toc, sqrtA, ws;
    int i = 24 + 12, prn, sat, week, e5a_hs, e5a_dvs, rsv, sys = _SYS_GAL_, wk;

    if (i + 484 <= (int)rtcm->len * 8)
    {
        prn = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;
        week = rtcm_getbitu(rtcm->buff, i, 12);
        i += 12; /* gst-week */
        eph.iode = rtcm_getbitu(rtcm->buff, i, 10);
        i += 10;
        eph.sva = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        eph.idot = rtcm_getbits(rtcm->buff, i, 14) * P2_43 * SC2RAD;
        i += 14;
        toc = rtcm_getbitu(rtcm->buff, i, 14) * 60.0;
        i += 14;
        eph.f2 = rtcm_getbits(rtcm->buff, i, 6) * P2_59;
        i += 6;
        eph.f1 = rtcm_getbits(rtcm->buff, i, 21) * P2_46;
        i += 21;
        eph.f0 = rtcm_getbits(rtcm->buff, i, 31) * P2_34;
        i += 31;
        eph.crs = rtcm_getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.deln = rtcm_getbits(rtcm->buff, i, 16) * P2_43 * SC2RAD;
        i += 16;
        eph.M0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cuc = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.e = rtcm_getbitu(rtcm->buff, i, 32) * P2_33;
        i += 32;
        eph.cus = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        sqrtA = rtcm_getbitu(rtcm->buff, i, 32) * P2_19;
        i += 32;
        eph.toes = rtcm_getbitu(rtcm->buff, i, 14) * 60.0;
        i += 14;
        eph.cic = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.OMG0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cis = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.i0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.crc = rtcm_getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.omg = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.OMGd = rtcm_getbits(rtcm->buff, i, 24) * P2_43 * SC2RAD;
        i += 24;
        eph.tgd[0] = rtcm_getbits(rtcm->buff, i, 10) * P2_32;
        i += 10; /* E5a/E1 */
        e5a_hs = rtcm_getbitu(rtcm->buff, i, 2);
        i += 2; /* OSHS */
        e5a_dvs = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1; /* OSDVS */
        rsv = rtcm_getbitu(rtcm->buff, i, 7);
    }
    else
    {
        return -1;
    }

    if (!(sat = satno(sys, prn)))
    {
        return -1;
    }
    eph.sat = sat;
    eph.week = week + 1024; /* gal-week = gst-week + 1024 */
    set_week_number(eph.week);
    ws = time2gpst(rtcm->time, &wk);
    if (wk != eph.week)
        rtcm->time = gpst2time(eph.week, ws);
    eph.toe = gpst2time(eph.week, eph.toes);
    eph.toc = gpst2time(eph.week, toc);
#ifdef _POST_RTK_
	eph.ttr = rtcm->time;
#endif // _POST_RTK_
    eph.A = sqrtA * sqrtA;
    eph.svh = (e5a_hs << 4) + (e5a_dvs << 3);
    eph.code = (1 << 1) | (1 << 8); /* data source = f/nav e5a + af0-2,toc,sisa for e5a-e1 */
    
    if (add_eph(&eph, nav) == 1)
		++nav->n_gal;

    return 2;
}
/* decode type 1046: galileo I/NAV satellite ephemerides (ref [17]) ----------*/
RTK_RAM_CODE static int decode_type1046(rtcm_t *rtcm, nav_t *nav)
{
    eph_t eph = {0};
    double toc, sqrtA, ws;
    int i = 24 + 12, prn, sat, week, e5b_hs, e5b_dvs, e1_hs, e1_dvs, sys = _SYS_GAL_, wk;

    if (i + 492 <= (int)rtcm->len * 8)
    {
        prn = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;
        week = rtcm_getbitu(rtcm->buff, i, 12);
        i += 12;
        eph.iode = rtcm_getbitu(rtcm->buff, i, 10);
        i += 10;
        eph.sva = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        eph.idot = rtcm_getbits(rtcm->buff, i, 14) * P2_43 * SC2RAD;
        i += 14;
        toc = rtcm_getbitu(rtcm->buff, i, 14) * 60.0;
        i += 14;
        eph.f2 = rtcm_getbits(rtcm->buff, i, 6) * P2_59;
        i += 6;
        eph.f1 = rtcm_getbits(rtcm->buff, i, 21) * P2_46;
        i += 21;
        eph.f0 = rtcm_getbits(rtcm->buff, i, 31) * P2_34;
        i += 31;
        eph.crs = rtcm_getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.deln = rtcm_getbits(rtcm->buff, i, 16) * P2_43 * SC2RAD;
        i += 16;
        eph.M0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cuc = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.e = rtcm_getbitu(rtcm->buff, i, 32) * P2_33;
        i += 32;
        eph.cus = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        sqrtA = rtcm_getbitu(rtcm->buff, i, 32) * P2_19;
        i += 32;
        eph.toes = rtcm_getbitu(rtcm->buff, i, 14) * 60.0;
        i += 14;
        eph.cic = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.OMG0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cis = rtcm_getbits(rtcm->buff, i, 16) * P2_29;
        i += 16;
        eph.i0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.crc = rtcm_getbits(rtcm->buff, i, 16) * P2_5;
        i += 16;
        eph.omg = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.OMGd = rtcm_getbits(rtcm->buff, i, 24) * P2_43 * SC2RAD;
        i += 24;
        eph.tgd[0] = rtcm_getbits(rtcm->buff, i, 10) * P2_32;
        i += 10; /* E5a/E1 */
        eph.tgd[1] = rtcm_getbits(rtcm->buff, i, 10) * P2_32;
        i += 10; /* E5b/E1 */
        e5b_hs = rtcm_getbitu(rtcm->buff, i, 2);
        i += 2; /* E5b OSHS */
        e5b_dvs = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1; /* E5b OSDVS */
        e1_hs = rtcm_getbitu(rtcm->buff, i, 2);
        i += 2; /* E1 OSHS */
        e1_dvs = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1; /* E1 OSDVS */
    }
    else
    {
        return -1;
    }

    if (!(sat = satno(sys, prn)))
    {
        return -1;
    }

    eph.sat = sat;
    eph.week = week + 1024; /* gal-week = gst-week + 1024 */
    set_week_number(eph.week);
    ws = time2gpst(rtcm->time, &wk);
    if (wk != eph.week)
        rtcm->time = gpst2time(eph.week, ws);
    eph.toe = gpst2time(eph.week, eph.toes);
    eph.toc = gpst2time(eph.week, toc);
#ifdef _POST_RTK_
	eph.ttr = rtcm->time;
#endif // _POST_RTK_
    eph.A = sqrtA * sqrtA;
    eph.svh = (e5b_hs << 7) + (e5b_dvs << 6) + (e1_hs << 1) + (e1_dvs << 0);
    eph.code = (1 << 0) | (1 << 9); /* data source = i/nav e1b + af0-2,toc,sisa for e5b-e1 */

    if (add_eph(&eph, nav)==1)
		++nav->n_gal;
    
    return 2;
}
/* decode type 1042/63: beidou ephemerides -----------------------------------*/
RTK_RAM_CODE static int decode_type1042(rtcm_t *rtcm, nav_t *nav)
{
    eph_t eph = {0};
	double toc, sqrtA;
    int i = 24 + 12, prn, sat, week, sys = _SYS_BDS_;

    if (i + 499 <= (int)rtcm->len * 8)
    {
        prn = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;
        week = rtcm_getbitu(rtcm->buff, i, 13);
        i += 13;
        eph.sva = rtcm_getbitu(rtcm->buff, i, 4);
        i += 4;
        eph.idot = rtcm_getbits(rtcm->buff, i, 14) * P2_43 * SC2RAD;
        i += 14;
        eph.iode = rtcm_getbitu(rtcm->buff, i, 5);
        i += 5; /* AODE */
        toc = rtcm_getbitu(rtcm->buff, i, 17) * 8.0;
        i += 17;
        eph.f2 = rtcm_getbits(rtcm->buff, i, 11) * P2_66;
        i += 11;
        eph.f1 = rtcm_getbits(rtcm->buff, i, 22) * P2_50;
        i += 22;
        eph.f0 = rtcm_getbits(rtcm->buff, i, 24) * P2_33;
        i += 24;
        eph.iodc = rtcm_getbitu(rtcm->buff, i, 5);
        i += 5; /* AODC */
        eph.crs = rtcm_getbits(rtcm->buff, i, 18) * P2_6;
        i += 18;
        eph.deln = rtcm_getbits(rtcm->buff, i, 16) * P2_43 * SC2RAD;
        i += 16;
        eph.M0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cuc = rtcm_getbits(rtcm->buff, i, 18) * P2_31;
        i += 18;
        eph.e = rtcm_getbitu(rtcm->buff, i, 32) * P2_33;
        i += 32;
        eph.cus = rtcm_getbits(rtcm->buff, i, 18) * P2_31;
        i += 18;
        sqrtA = rtcm_getbitu(rtcm->buff, i, 32) * P2_19;
        i += 32;
        eph.toes = rtcm_getbitu(rtcm->buff, i, 17) * 8.0;
        i += 17;
        eph.cic = rtcm_getbits(rtcm->buff, i, 18) * P2_31;
        i += 18;
        eph.OMG0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.cis = rtcm_getbits(rtcm->buff, i, 18) * P2_31;
        i += 18;
        eph.i0 = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.crc = rtcm_getbits(rtcm->buff, i, 18) * P2_6;
        i += 18;
        eph.omg = rtcm_getbits(rtcm->buff, i, 32) * P2_31 * SC2RAD;
        i += 32;
        eph.OMGd = rtcm_getbits(rtcm->buff, i, 24) * P2_43 * SC2RAD;
        i += 24;
        eph.tgd[0] = rtcm_getbits(rtcm->buff, i, 10) * 1E-10;
        i += 10;
        eph.tgd[1] = rtcm_getbits(rtcm->buff, i, 10) * 1E-10;
        i += 10;
        eph.svh = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
    }
    else
    {
        return -1;
    }

    if (!(sat = satno(sys, prn)))
    {
        return -1;
    }
    eph.sat = sat;
    eph.week = adjbdtweek(&rtcm->time, week); // 1356 + week; //
    set_week_number(eph.week + 1356);
    // ws = time2gpst(rtcm->time, &wk);
    // if (wk != eph.week)
    //     rtcm->time = gpst2time(eph.week, ws);
    eph.toe = bdt2gpst(bdt2time(eph.week, eph.toes)); /* bdt -> gpst */
    eph.toc = bdt2gpst(bdt2time(eph.week, toc));      /* bdt -> gpst */
#ifdef _POST_RTK_
	eph.ttr = rtcm->time;
#endif // _POST_RTK_
    eph.A = sqrtA * sqrtA;

    if (add_eph(&eph, nav) == 1)
		++nav->n_bds;

    return 2;
}

/* save obs data in msm message ----------------------------------------------*/
RTK_RAM_CODE static void save_msm_obs(rtcm_t *rtcm, obs_t *obs, int sys, msm_h_t *h, const double *r,
                         const double *pr, const double *cp, const double *rr,
                         const double *rrf, const double *cnr, const int *lock,
                         const int *ex, const int *half)
{
    const char *sig[32];
    double wl;
    unsigned char code[32];
    char *msm_type = "", *q = NULL;
    int i, j, k, type, prn, sat, fn, index = 0, sysid, freq[32], ind[32];
    char opt[256] = {0};
	obsd_t obsd = { 0 };
	int frq_code1, frq_code2, msmobs;

    type = rtcm_getbitu(rtcm->buff, 24, 12);
	msmobs = type - (int)(floor(type*0.1) * 10);

    switch (sys)
    {
    case _SYS_GPS_:
        msm_type = q = rtcm->msmtype[0];
        break;
    case _SYS_GLO_:
        msm_type = q = rtcm->msmtype[1];
        break;
    case _SYS_GAL_:
        msm_type = q = rtcm->msmtype[2];
        break;
    case _SYS_QZS_:
        msm_type = q = rtcm->msmtype[3];
        break;
    case _SYS_SBS_:
        msm_type = q = rtcm->msmtype[4];
        break;
    case _SYS_BDS_:
        msm_type = q = rtcm->msmtype[5];
        break;
    }
    /* id to signal */
    for (i = 0; i < h->nsig; i++)
    {
        switch (sys)
        {
        case _SYS_GPS_:
			sig[i] = rtcm_msm_sig_gps[h->sigs[i] - 1];  sysid = 0;
            break;
        case _SYS_GLO_:
            sig[i] = rtcm_msm_sig_glo[h->sigs[i] - 1];  sysid = 3;
            break;
        case _SYS_GAL_:
            sig[i] = msm_sig_gal[h->sigs[i] - 1];       sysid = 1;
            break;
        case _SYS_QZS_:
            sig[i] = msm_sig_qzs[h->sigs[i] - 1];       sysid = 0;
            break;
        case _SYS_SBS_:
            sig[i] = msm_sig_sbs[h->sigs[i] - 1];       sysid = 5;
            break;
        case _SYS_BDS_:
            sig[i] = msm_sig_cmp[h->sigs[i] - 1];       sysid = 2;
            break;
        default:
            sig[i] = "";
            break;
        }
        /* signal to rinex obs type */
        code[i] = obs2code(sys, sig[i], freq + i);
		for (j = 0; j < NFREQ; j++)
		{
			if (sysid + 1 > NSYS)	continue;
			if (rtcm->icode[sysid*NFREQ + j] == 0)
			{
				if(sys != _SYS_QZS_)
					rtcm->icode[sysid*NFREQ + j] = code[i];
				freq[i] = j + 1;
				break;
			}
			else
			{
				frq_code1 = code2frq(sys, rtcm->icode[sysid*NFREQ + j]);
				frq_code2 = code2frq(sys, code[i]);
				if (frq_code1 >=0 && frq_code1 == frq_code2)
				{
					freq[i] = j + 1;
					break;
				}				
			}
		}

        if (code[i] != CODE_NONE)
        {
            if (q)
                q += sprintf(q, "L%s%s", sig[i], i < h->nsig - 1 ? "," : "");
        }
        else
        {
            if (q)
                q += sprintf(q, "(%d)%s", h->sigs[i], i < h->nsig - 1 ? "," : "");

        }
    }

    /* get signal index */
    sigindex(sys, code, freq, h->nsig, opt, ind);

    for (i = j = 0; i < h->nsat; i++)
    {
        prn = h->sats[i];
        if (sys == _SYS_QZS_)
            prn += MINPRNQZS - 1;
        if (sys == _SYS_SBS_)
            return ;

		memset(&obsd, 0, sizeof(obsd_t));
        if ((sat = satno(sys, prn)))
        {
            //index = obsindex(obs, rtcm->time, sat);
			obsd.sat = sat;
			obsd.time = rtcm->time;
        }
        else
        {
        }
        for (k = 0; k < h->nsig; k++)
        {
            if (!h->cellmask[k + i * h->nsig])
                continue;

			if (sys == _SYS_QZS_ && k == 1)
				continue;

            if (sat && ind[k] >= 0 && code[k])
            {
                /* satellite carrier wave length */
                wl = satwavelen(sat, code[k]);
				if (wl == 0.0)
				{
					j++;
					continue;
				}
                /* glonass wave length by extended info */
                if (sys == _SYS_GLO_ && ex && ex[i] <= 13)
                {
                    fn = ex[i] - 7;
                    wl = CLIGHT / ((freq[k] == 2 ? FREQ2_GLO : FREQ1_GLO) +
                                   (freq[k] == 2 ? DFRQ2_GLO : DFRQ1_GLO) * fn);
                }
                /* pseudorange (m) */
                if (r[i] != 0.0 && pr[j] > -1E12)
                {
                    //obs->data[index].P[ind[k]] = r[i] + pr[j];
					obsd.P[ind[k]] = r[i] + pr[j];
                }
                /* carrier-phase (cycle) */
                if (r[i] != 0.0 && cp[j] > -1E12 && wl > 0.0)
                {
                    //obs->data[index].L[ind[k]] = (r[i] + cp[j]) / wl;
					obsd.L[ind[k]] = (r[i] + cp[j]) / wl;
                }
                /* doppler (hz) */
                if (rr && rrf && rrf[j] > -1E12 && wl > 0.0)
                {
                    //obs->data[index].D[ind[k]] = (float)(-(rr[i] + rrf[j]) / wl);
					obsd.D[ind[k]] = (float)(-(rr[i] + rrf[j]) / wl);
                }
				if (msmobs == 7)
				{
					obsd.qualP[ind[k]] = rtcm->teseo.sigqm2[MAXSAT * k + sat - 1][0];
					obsd.qualL[ind[k]] = rtcm->teseo.sigqm2[MAXSAT * k + sat - 1][1];
				}

				obsd.LLI[ind[k]] = (unsigned char)lossoflock(rtcm, sat, ind[k], lock[j]) +
					               (half[j] ? 2 : 0);
				obsd.SNR[ind[k]] = (unsigned char)(cnr[j] * 4.0);
				obsd.code[ind[k]] = code[k];
				//if (obsd.qualP[ind[k]] == 4)
				//{
				//	obsd.P[ind[k]] = 0.0;
				//}
				//if (obsd.qualL[ind[k]] == 4)
				//{
				//	obsd.L[ind[k]] = 0.0;
				//}
				j++;
            }
			else
			{
				j++;
				continue;
			}            
        }
		add_obs(&obsd, obs);
    }
}
/* decode type msm message header --------------------------------------------*/
RTK_RAM_CODE static int decode_msm_head(rtcm_t *rtcm, obs_t *obs, int sys, int *sync, int *iod,
                           msm_h_t *h, int *hsize)
{
    msm_h_t h0 = {0};
    double tow, tod;
    char tstr[64];
    int i = 24, j, dow, mask, staid, type, ncell = 0;

    type = rtcm_getbitu(rtcm->buff, i, 12);
    i += 12;

    *h = h0;
    if (i + 157 <= (int)rtcm->len * 8)
    {
        staid = rtcm_getbitu(rtcm->buff, i, 12);
        obs->staid_obs = staid;
        i += 12;

        if (sys == _SYS_GLO_)
        {
            dow = rtcm_getbitu(rtcm->buff, i, 3);
            i += 3;
            tod = rtcm_getbitu(rtcm->buff, i, 27) * 0.001;
            i += 27;
            adjday_glot(&rtcm->time, tod);
        }
        else if (sys == _SYS_BDS_)
        {
            tow = rtcm_getbitu(rtcm->buff, i, 30) * 0.001;
            i += 30;
            tow += 14.0; /* BDT -> GPST */
            adjweek(&rtcm->time, tow);
        }
        else
        {
            tow = rtcm_getbitu(rtcm->buff, i, 30) * 0.001;
            i += 30;
            adjweek(&rtcm->time, tow);
        }
        *sync = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
        *iod = rtcm_getbitu(rtcm->buff, i, 3);
        i += 3;
        h->time_s = (unsigned char)rtcm_getbitu(rtcm->buff, i, 7);
        i += 7;
        h->clk_str = (unsigned char)rtcm_getbitu(rtcm->buff, i, 2);
        i += 2;
        h->clk_ext = (unsigned char)rtcm_getbitu(rtcm->buff, i, 2);
        i += 2;
        h->smooth = (unsigned char)rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
        h->tint_s = (unsigned char)rtcm_getbitu(rtcm->buff, i, 3);
        i += 3;
        for (j = 1; j <= 64; j++)
        {
            mask = rtcm_getbitu(rtcm->buff, i, 1);
            i += 1;
			if (mask)
			{
				h->sats[h->nsat++] = (unsigned char)j;
			}
        }
        for (j = 1; j <= 32; j++)
        {
            mask = rtcm_getbitu(rtcm->buff, i, 1);
            i += 1;
			if (mask)
			{
				h->sigs[h->nsig++] = (unsigned char)j;
			}
        }
    }
    else
    {
        return -1;
    }
    /* test station id */
	//if (!test_staid(obs, staid))
	//{
	//	return -1;
	//}

    if (h->nsat * h->nsig > 64)
    {
        return -1;
    }
    if (i + h->nsat * h->nsig > (int)rtcm->len * 8)
    {
        return -1;
    }
    for (j = 0; j < h->nsat * h->nsig; j++)
    {
        h->cellmask[j] = (unsigned char)rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
		if (h->cellmask[j])
		{
			ncell++;
		}
    }
    *hsize = i;

    time2str(rtcm->time, tstr, 2);

    return ncell;
}
/* decode unsupported msm message --------------------------------------------*/
RTK_RAM_CODE static int decode_msm0(rtcm_t *rtcm, obs_t *obs, int sys)
{
    msm_h_t h = {0};
    int i, sync, iod;
    if (decode_msm_head(rtcm, obs, sys, &sync, &iod, &h, &i) < 0)
        return -1;
    obs->obsflag = !sync;
    return sync ? 0 : 1;
}
/* decode msm 4: full pseudorange and phaserange plus cnr --------------------*/
RTK_RAM_CODE static int decode_msm4(rtcm_t *rtcm, obs_t *obs, int sys)
{
    msm_h_t h = {0};
    double r[64], pr[64], cp[64], cnr[64];
    int i, j, type, sync, iod, ncell, rng, rng_m, prv, cpv, lock[64], half[64];

    type = rtcm_getbitu(rtcm->buff, 24, 12);

    /* decode msm header */
	if ((ncell = decode_msm_head(rtcm, obs, sys, &sync, &iod, &h, &i)) < 0)
	{
		return -1;
	}

    if (i + h.nsat * 18 + ncell * 48 > (int)rtcm->len * 8)
    {
        return -1;
    }
	for (j = 0; j < h.nsat; j++)
	{
		r[j] = 0.0;
	}
	for (j = 0; j < ncell; j++)
	{
		pr[j] = cp[j] = -1E16;
	}

    /* decode satellite data */
    for (j = 0; j < h.nsat; j++)
    { /* range */
        rng = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
		if (rng != 255)
		{
			r[j] = rng * RANGE_MS;
		}
    }
    for (j = 0; j < h.nsat; j++)
    {
        rng_m = rtcm_getbitu(rtcm->buff, i, 10);
        i += 10;
		if (r[j] != 0.0)
		{
			r[j] += rng_m * P2_10 * RANGE_MS;
		}
    }
    /* decode signal data */
    for (j = 0; j < ncell; j++)
    { /* pseudorange */
        prv = rtcm_getbits(rtcm->buff, i, 15);
        i += 15;
		if (prv != -16384)
		{
			pr[j] = prv * P2_24 * RANGE_MS;
		}
    }
    for (j = 0; j < ncell; j++)
    { /* phaserange */
        cpv = rtcm_getbits(rtcm->buff, i, 22);
        i += 22;
		if (cpv != -2097152)
		{
			cp[j] = cpv * P2_29 * RANGE_MS;
		}
    }
    for (j = 0; j < ncell; j++)
    { /* lock time */
        lock[j] = rtcm_getbitu(rtcm->buff, i, 4);
        i += 4;
    }
    for (j = 0; j < ncell; j++)
    { /* half-cycle ambiguity */
        half[j] = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
    }
    for (j = 0; j < ncell; j++)
    { /* cnr */
        cnr[j] = rtcm_getbitu(rtcm->buff, i, 6) * 1.0;
        i += 6;
    }
    /* save obs data in msm message */
    save_msm_obs(rtcm, obs, sys, &h, r, pr, cp, NULL, NULL, cnr, lock, NULL, half);

    obs->obsflag = !sync;
    return sync ? 0 : 1;
}
/* decode msm 5: full pseudorange, phaserange, phaserangerate and cnr --------*/
RTK_RAM_CODE static int decode_msm5(rtcm_t *rtcm, obs_t *obs, int sys)
{
    msm_h_t h = {0};
    double r[64], rr[64], pr[64], cp[64], rrf[64], cnr[64];
    int i, j, type, sync, iod, ncell, rng, rng_m, rate, prv, cpv, rrv, lock[64];
    int ex[64], half[64];

    type = rtcm_getbitu(rtcm->buff, 24, 12);

    /* decode msm header */
    if ((ncell = decode_msm_head(rtcm, obs, sys, &sync, &iod, &h, &i)) < 0)
        return -1;

    if (i + h.nsat * 36 + ncell * 63 > (int)rtcm->len * 8)
    {
        return -1;
    }
    for (j = 0; j < h.nsat; j++)
    {
        r[j] = rr[j] = 0.0;
        ex[j] = 15;
    }
    for (j = 0; j < ncell; j++)
        pr[j] = cp[j] = rrf[j] = -1E16;

    /* decode satellite data */
    for (j = 0; j < h.nsat; j++)
    { /* range */
        rng = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        if (rng != 255)
            r[j] = rng * RANGE_MS;
    }
    for (j = 0; j < h.nsat; j++)
    { /* extended info */
        ex[j] = rtcm_getbitu(rtcm->buff, i, 4);
        i += 4;
    }
    for (j = 0; j < h.nsat; j++)
    {
        rng_m = rtcm_getbitu(rtcm->buff, i, 10);
        i += 10;
        if (r[j] != 0.0)
            r[j] += rng_m * P2_10 * RANGE_MS;
    }
    for (j = 0; j < h.nsat; j++)
    { /* phaserangerate */
        rate = rtcm_getbits(rtcm->buff, i, 14);
        i += 14;
        if (rate != -8192)
            rr[j] = rate * 1.0;
    }
    /* decode signal data */
    for (j = 0; j < ncell; j++)
    { /* pseudorange */
        prv = rtcm_getbits(rtcm->buff, i, 15);
        i += 15;
        if (prv != -16384)
            pr[j] = prv * P2_24 * RANGE_MS;
    }
    for (j = 0; j < ncell; j++)
    { /* phaserange */
        cpv = rtcm_getbits(rtcm->buff, i, 22);
        i += 22;
        if (cpv != -2097152)
            cp[j] = cpv * P2_29 * RANGE_MS;
    }
    for (j = 0; j < ncell; j++)
    { /* lock time */
        lock[j] = rtcm_getbitu(rtcm->buff, i, 4);
        i += 4;
    }
    for (j = 0; j < ncell; j++)
    { /* half-cycle ambiguity */
        half[j] = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
    }
    for (j = 0; j < ncell; j++)
    { /* cnr */
        cnr[j] = rtcm_getbitu(rtcm->buff, i, 6) * 1.0;
        i += 6;
    }
    for (j = 0; j < ncell; j++)
    { /* phaserangerate */
        rrv = rtcm_getbits(rtcm->buff, i, 15);
        i += 15;
        if (rrv != -16384)
            rrf[j] = rrv * 0.0001;
    }
    /* save obs data in msm message */
    save_msm_obs(rtcm, obs, sys, &h, r, pr, cp, rr, rrf, cnr, lock, ex, half);

    obs->obsflag = !sync;
    return sync ? 0 : 1;
}
/* decode msm 6: full pseudorange and phaserange plus cnr (high-res) ---------*/
RTK_RAM_CODE static int decode_msm6(rtcm_t *rtcm, obs_t *obs, int sys)
{
    msm_h_t h = {0};
    double r[64], pr[64], cp[64], cnr[64];
    int i, j, type, sync, iod, ncell, rng, rng_m, prv, cpv, lock[64], half[64];

    type = rtcm_getbitu(rtcm->buff, 24, 12);

    /* decode msm header */
    if ((ncell = decode_msm_head(rtcm, obs, sys, &sync, &iod, &h, &i)) < 0)
        return -1;

    if (i + h.nsat * 18 + ncell * 65 > (int)rtcm->len * 8)
    {
        return -1;
    }
    for (j = 0; j < h.nsat; j++)
        r[j] = 0.0;
    for (j = 0; j < ncell; j++)
        pr[j] = cp[j] = -1E16;

    /* decode satellite data */
    for (j = 0; j < h.nsat; j++)
    { /* range */
        rng = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
        if (rng != 255)
            r[j] = rng * RANGE_MS;
    }
    for (j = 0; j < h.nsat; j++)
    {
        rng_m = rtcm_getbitu(rtcm->buff, i, 10);
        i += 10;
        if (r[j] != 0.0)
            r[j] += rng_m * P2_10 * RANGE_MS;
    }
    /* decode signal data */
    for (j = 0; j < ncell; j++)
    { /* pseudorange */
        prv = rtcm_getbits(rtcm->buff, i, 20);
        i += 20;
        if (prv != -524288)
            pr[j] = prv * P2_29 * RANGE_MS;
    }
    for (j = 0; j < ncell; j++)
    { /* phaserange */
        cpv = rtcm_getbits(rtcm->buff, i, 24);
        i += 24;
        if (cpv != -8388608)
            cp[j] = cpv * P2_31 * RANGE_MS;
    }
    for (j = 0; j < ncell; j++)
    { /* lock time */
        lock[j] = rtcm_getbitu(rtcm->buff, i, 10);
        i += 10;
    }
    for (j = 0; j < ncell; j++)
    { /* half-cycle ambiguity */
        half[j] = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
    }
    for (j = 0; j < ncell; j++)
    { /* cnr */
        cnr[j] = rtcm_getbitu(rtcm->buff, i, 10) * 0.0625;
        i += 10;
    }
    /* save obs data in msm message */
    save_msm_obs(rtcm, obs, sys, &h, r, pr, cp, NULL, NULL, cnr, lock, NULL, half);

    obs->obsflag = !sync;
    return sync ? 0 : 1;
}
/* decode msm 7: full pseudorange, phaserange, phaserangerate and cnr (h-res) */
RTK_RAM_CODE static int decode_msm7(rtcm_t *rtcm, obs_t *obs, int sys)
{
    msm_h_t h = {0};
    double r[64] = {0}, rr[64] = {0}, pr[64] = {0}, cp[64] = {0}, rrf[64] = {0}, cnr[64] = {0};
    int i, j, type, sync, iod, ncell, rng, rng_m, rate, prv, cpv, rrv, lock[64];
    int ex[64] = {0}, half[64] = {0};

    type = rtcm_getbitu(rtcm->buff, 24, 12);

    /* decode msm header */
	if ((ncell = decode_msm_head(rtcm, obs, sys, &sync, &iod, &h, &i)) < 0)
	{
		return -1;
	}

    if (i + h.nsat * 36 + ncell * 80 > (int)rtcm->len * 8)
    {
        return -1;
    }
    for (j = 0; j < h.nsat; j++)
    {
        r[j] = rr[j] = 0.0;
        ex[j] = 15;
    }
    for (j = 0; j < ncell; j++)
        pr[j] = cp[j] = rrf[j] = -1E16;

    /* decode satellite data */
    for (j = 0; j < h.nsat; j++)
    { /* range */
        rng = rtcm_getbitu(rtcm->buff, i, 8);
        i += 8;
		if (rng != 255)
		{
			r[j] = rng * RANGE_MS;
		}
    }

    for (j = 0; j < h.nsat; j++)
    { /* extended info */
        ex[j] = rtcm_getbitu(rtcm->buff, i, 4);
        i += 4;
    }

    for (j = 0; j < h.nsat; j++)
    {
        rng_m = rtcm_getbitu(rtcm->buff, i, 10);
        i += 10;
		if (r[j] != 0.0)
		{
			r[j] += rng_m * P2_10 * RANGE_MS;
		}
    }

    for (j = 0; j < h.nsat; j++)
    { /* phaserangerate */
        rate = rtcm_getbits(rtcm->buff, i, 14);
        i += 14;
		if (rate != -8192)
		{
			rr[j] = rate * 1.0;
		}
    }

    /* decode signal data */
    for (j = 0; j < ncell; j++)
    { /* pseudorange */
        prv = rtcm_getbits(rtcm->buff, i, 20);
        i += 20;
        if (prv != -524288)
            pr[j] = prv * P2_29 * RANGE_MS;
    }

    for (j = 0; j < ncell; j++)
    { /* phaserange */
        cpv = rtcm_getbits(rtcm->buff, i, 24);
        i += 24;
		if (cpv != -8388608)
		{
			cp[j] = cpv * P2_31 * RANGE_MS;
		}
    }

    for (j = 0; j < ncell; j++)
    { /* lock time */
        lock[j] = rtcm_getbitu(rtcm->buff, i, 10);
        i += 10;
    }

    for (j = 0; j < ncell; j++)
    { /* half-cycle amiguity */
        half[j] = rtcm_getbitu(rtcm->buff, i, 1);
        i += 1;
    }

    for (j = 0; j < ncell; j++)
    { /* cnr */
        cnr[j] = rtcm_getbitu(rtcm->buff, i, 10) * 0.0625;
        i += 10;
    }

    for (j = 0; j < ncell; j++)
    { /* phaserangerate */
        rrv = rtcm_getbits(rtcm->buff, i, 15);
        i += 15;
		if (rrv != -16384)
		{
			rrf[j] = rrv * 0.0001;
		}
    }

    /* save obs data in msm message */
    save_msm_obs(rtcm, obs, sys, &h, r, pr, cp, rr, rrf, cnr, lock, ex, half);

    obs->obsflag = !sync;

    return sync ? 0 : 1;
}

/* decode type 1230: glonass L1 and L2 code-phase biases ---------------------*/
RTK_RAM_CODE static int decode_type1230(rtcm_t *rtcm)
{
    return 0;
}
#ifdef _USE_PPP_
/* decode ssr 1,4 message header ---------------------------------------------*/
static int decode_ssr1_head(rtcm_t *rtcm, int sys, int *sync, int *iod,
                            double *udint, int *refd, int *hsize)
{
    double tod, tow;
    char tstr[64];
    int i = 24 + 12, nsat, udi, provid = 0, solid = 0, ns;

    ns = sys == _SYS_QZS_ ? 4 : 6;

    if (i + (sys == _SYS_GLO_ ? 53 : 50 + ns) > rtcm->len * 8)
        return -1;

    if (sys == _SYS_GLO_)
    {
        tod = rtcm_getbitu(rtcm->buff, i, 17);
        i += 17;
        adjday_glot(&rtcm->time, tod);
    }
    else
    {
        tow = rtcm_getbitu(rtcm->buff, i, 20);
        i += 20;
        adjweek(&rtcm->time, tow);
    }
    udi = rtcm_getbitu(rtcm->buff, i, 4);
    i += 4;
    *sync = rtcm_getbitu(rtcm->buff, i, 1);
    i += 1;
    *refd = rtcm_getbitu(rtcm->buff, i, 1);
    i += 1; /* satellite ref datum */
    *iod = rtcm_getbitu(rtcm->buff, i, 4);
    i += 4; /* iod */
    provid = rtcm_getbitu(rtcm->buff, i, 16);
    i += 16; /* provider id */
    solid = rtcm_getbitu(rtcm->buff, i, 4);
    i += 4; /* solution id */
    nsat = rtcm_getbitu(rtcm->buff, i, ns);
    i += ns;
    *udint = ssrudint[udi];

    time2str(rtcm->time, tstr, 2);

    *hsize = i;
    return nsat;
}
/* decode ssr 2,3,5,6 message header -----------------------------------------*/
static int decode_ssr2_head(rtcm_t *rtcm, int sys, int *sync, int *iod,
                            double *udint, int *hsize)
{
    double tod, tow;
    char tstr[64];
    int i = 24 + 12, nsat, udi, provid = 0, solid = 0, ns;

    ns = sys == _SYS_QZS_ ? 4 : 6;

    if (i + (sys == _SYS_GLO_ ? 52 : 49 + ns) > rtcm->len * 8)
        return -1;

    if (sys == _SYS_GLO_)
    {
        tod = rtcm_getbitu(rtcm->buff, i, 17);
        i += 17;
        adjday_glot(&rtcm->time, tod);
    }
    else
    {
        tow = rtcm_getbitu(rtcm->buff, i, 20);
        i += 20;
        adjweek(&rtcm->time, tow);
    }
    udi = rtcm_getbitu(rtcm->buff, i, 4);
    i += 4;
    *sync = rtcm_getbitu(rtcm->buff, i, 1);
    i += 1;
    *iod = rtcm_getbitu(rtcm->buff, i, 4);
    i += 4;
    provid = rtcm_getbitu(rtcm->buff, i, 16);
    i += 16; /* provider id */
    solid = rtcm_getbitu(rtcm->buff, i, 4);
    i += 4; /* solution id */
    nsat = rtcm_getbitu(rtcm->buff, i, ns);
    i += ns;
    *udint = ssrudint[udi];

    time2str(rtcm->time, tstr, 2);

    *hsize = i;
    return nsat;
}

/* decode ssr 1: orbit corrections -------------------------------------------*/
static int decode_ssr1(rtcm_t *rtcm, int sys, nav_t *nav, obs_t *obs)
{
    double udint, deph[3], ddeph[3];
    int i, j, k, type, sync, iod, nsat, prn, sat, iode, iodcrc, refd = 0, np, ni, nj, offp, loc;
    ssr_t ssr = {0};

    type = rtcm_getbitu(rtcm->buff, 24, 12);

    if ((nsat = decode_ssr1_head(rtcm, sys, &sync, &iod, &udint, &refd, &i)) < 0)
    {
        return -1;
    }
    switch (sys)
    {
    case _SYS_GPS_:
        np = 6;
        ni = 8;
        nj = 0;
        offp = 0;
        break;
    case _SYS_GLO_:
        np = 5;
        ni = 8;
        nj = 0;
        offp = 0;
        break;
    case _SYS_GAL_:
        np = 6;
        ni = 10;
        nj = 0;
        offp = 0;
        break;
    case _SYS_QZS_:
        np = 4;
        ni = 8;
        nj = 0;
        offp = 192;
        break;
    case _SYS_BDS_:
        np = 6;
        ni = 10;
        nj = 24;
        offp = 1;
        break;
    case _SYS_SBS_:
        np = 6;
        ni = 9;
        nj = 24;
        offp = 120;
        break;
    default:
        return sync ? 0 : 10;
    }
    for (j = 0; j < nsat && i + 121 + np + ni + nj <= rtcm->len * 8; j++)
    {
        prn = rtcm_getbitu(rtcm->buff, i, np) + offp;
        i += np;
        iode = rtcm_getbitu(rtcm->buff, i, ni);
        i += ni;
        iodcrc = rtcm_getbitu(rtcm->buff, i, nj);
        i += nj;
        deph[0] = rtcm_getbits(rtcm->buff, i, 22) * 1E-4;
        i += 22;
        deph[1] = rtcm_getbits(rtcm->buff, i, 20) * 4E-4;
        i += 20;
        deph[2] = rtcm_getbits(rtcm->buff, i, 20) * 4E-4;
        i += 20;
        ddeph[0] = rtcm_getbits(rtcm->buff, i, 21) * 1E-6;
        i += 21;
        ddeph[1] = rtcm_getbits(rtcm->buff, i, 19) * 4E-6;
        i += 19;
        ddeph[2] = rtcm_getbits(rtcm->buff, i, 19) * 4E-6;
        i += 19;

        if (!(sat = satno(sys, prn)))
        {
            /*         trace(2,"rtcm3 %d satellite number error: prn=%d\n",type,prn);   */
            continue;
        }
        memset(&ssr, 0, sizeof(ssr_t));
        ssr.sat = sat;
        ssr.t0[0] = rtcm->time;
        ssr.udi[0] = udint;
        ssr.iod[0] = iod;
        ssr.iode = iode;     /* sbas/bds: toe/t0 modulo */
        ssr.iodcrc = iodcrc; /* sbas/bds: iod crc */
        ssr.refd = refd;

        for (k = 0; k < 3; k++)
        {
            ssr.deph[k] = deph[k];
            ssr.ddeph[k] = ddeph[k];
        }
        ssr.update = 1;

        for (loc = 0; loc < nav->ns; ++loc)
        {
            if (nav->ssr[loc].sat == sat)
            {
                break;
            }
        }
        if (loc < nav->ns)
        {
            nav->ssr[loc] = ssr;
        }
        else if (loc == nav->ns && loc < MAXSSR)
        {
            nav->ssr[nav->ns] = ssr;
            ++nav->ns;
        }
    }
    return sync ? 0 : 10;
}
/* decode ssr 2: clock corrections -------------------------------------------*/
static int decode_ssr2(rtcm_t *rtcm, int sys, nav_t *nav, obs_t *obs)
{
    double udint, dclk[3];
    int i, j, k, type, sync, iod, nsat, prn, sat, np, offp, loc;
    ssr_t ssr = {0};

    type = rtcm_getbitu(rtcm->buff, 24, 12);

    if ((nsat = decode_ssr2_head(rtcm, sys, &sync, &iod, &udint, &i)) < 0)
    {
        return -1;
    }
    switch (sys)
    {
    case _SYS_GPS_:
        np = 6;
        offp = 0;
        break;
    case _SYS_GLO_:
        np = 5;
        offp = 0;
        break;
    case _SYS_GAL_:
        np = 6;
        offp = 0;
        break;
    case _SYS_QZS_:
        np = 4;
        offp = 192;
        break;
    case _SYS_BDS_:
        np = 6;
        offp = 1;
        break;
    case _SYS_SBS_:
        np = 6;
        offp = 120;
        break;
    default:
        return sync ? 0 : 10;
    }
    for (j = 0; j < nsat && i + 70 + np <= rtcm->len * 8; j++)
    {
        prn = rtcm_getbitu(rtcm->buff, i, np) + offp;
        i += np;
        dclk[0] = rtcm_getbits(rtcm->buff, i, 22) * 1E-4;
        i += 22;
        dclk[1] = rtcm_getbits(rtcm->buff, i, 21) * 1E-6;
        i += 21;
        dclk[2] = rtcm_getbits(rtcm->buff, i, 27) * 2E-8;
        i += 27;

        if (!(sat = satno(sys, prn)))
        {
            /*         trace(2,"rtcm3 %d satellite number error: prn=%d\n",type,prn);    */
            continue;
        }
        memset(&ssr, 0, sizeof(ssr_t));
        ssr.sat = sat;
        ssr.t0[1] = rtcm->time;
        ssr.udi[1] = udint;
        ssr.iod[1] = iod;

        for (k = 0; k < 3; k++)
        {
            ssr.dclk[k] = dclk[k];
        }
        ssr.update = 1;

        for (loc = 0; loc < nav->ns; ++loc)
        {
            if (nav->ssr[loc].sat == sat)
            {
                break;
            }
        }
        if (loc < nav->ns)
        {
            nav->ssr[loc] = ssr;
        }
        else if (loc == nav->ns && loc < MAXSSR)
        {
            nav->ssr[nav->ns] = ssr;
            ++nav->ns;
        }
    }
    return sync ? 0 : 10;
}
/* decode ssr 3: satellite code biases ---------------------------------------*/
static int decode_ssr3(rtcm_t *rtcm, int sys, nav_t *nav, obs_t *obs)
{
    const int *codes;
    const unsigned char *freqs;
    double udint, bias; // cbias[NFREQ];
    int i, j, k, type, mode, sync, iod, nsat, prn, sat, nbias, np, offp, ncode, loc;
    ssr_t ssr = {0};
    memset(&ssr, 0, sizeof(ssr_t));
    type = rtcm_getbitu(rtcm->buff, 24, 12);

    if ((nsat = decode_ssr2_head(rtcm, sys, &sync, &iod, &udint, &i)) < 0)
    {
        return -1;
    }
    switch (sys)
    {
    case _SYS_GPS_:
        np = 6;
        offp = 0;
        codes = codes_gps;
        ncode = 17;
        freqs = obsfreqs_gps;
        break;
    case _SYS_GLO_:
        np = 5;
        offp = 0;
        codes = codes_glo;
        ncode = 4;
        freqs = obsfreqs_glo;
        break;
    case _SYS_GAL_:
        np = 6;
        offp = 0;
        codes = codes_gal;
        ncode = 19;
        freqs = obsfreqs_gal;
        break;
    case _SYS_QZS_:
        np = 4;
        offp = 192;
        codes = codes_qzs;
        ncode = 13;
        freqs = obsfreqs_qzs;
        break;
    case _SYS_BDS_:
        np = 6;
        offp = 1;
        codes = codes_bds;
        ncode = 9;
        freqs = obsfreqs_cmp;
        break;
    case _SYS_SBS_:
        np = 6;
        offp = 120;
        codes = codes_sbs;
        ncode = 4;
        freqs = obsfreqs_sbs;
        break;
    default:
        return sync ? 0 : 10;
    }
    for (j = 0; j < nsat && i + 5 + np <= rtcm->len * 8; j++)
    {
        prn = rtcm_getbitu(rtcm->buff, i, np) + offp;
        i += np;
        nbias = rtcm_getbitu(rtcm->buff, i, 5);
        i += 5;

        for (k = 0; k < nbias && i + 19 <= rtcm->len * 8; k++)
        {
            mode = rtcm_getbitu(rtcm->buff, i, 5);
            i += 5;
            bias = rtcm_getbits(rtcm->buff, i, 14) * 0.01;
            i += 14;
            if (mode <= ncode && bias != 0.0 && freqs[codes[mode] + 1] <= NFREQ)
            {
                //  ssr.cbias[freqs[codes[mode] + 1] - 1] = (float)bias;
            }
        }
        if (!(sat = satno(sys, prn)))
        {
            /*      trace(2,"rtcm3 %d satellite number error: prn=%d\n",type,prn);   */
            continue;
        }

        ssr.sat = sat;
        ssr.t0[4] = rtcm->time;
        ssr.udi[4] = udint;
        ssr.iod[4] = iod;
        ssr.update = 1;

        for (loc = 0; loc < nav->ns; ++loc)
        {
            if (nav->ssr[loc].sat == sat)
            {
                nav->ssr[loc].t0[4] = ssr.t0[4];
                nav->ssr[loc].udi[4] = ssr.udi[4];
                nav->ssr[loc].iod[4] = ssr.iod[4];
                //   for (k = 0; k < NFREQ; k++)   nav->ssr[loc].cbias[k] = ssr.cbias[k];
                break;
            }
        }

        if (loc < nav->ns)
        {
            for (k = 0; k < obs->n; k++)
            {
                if (obs->data[k].sat == sat)
                {
                    nav->ssr[loc].t0[4] = ssr.t0[4];
                    nav->ssr[loc].udi[4] = ssr.udi[4];
                    nav->ssr[loc].iod[4] = ssr.iod[4];
                    //       for (k = 0; k < NFREQ; k++)   nav->ssr[loc].cbias[k] = ssr.cbias[k];
                    break;
                }
            }
        }
        if (loc == nav->ns)
        {
            if (loc < MAXSSR)
            {
                for (k = 0; k < obs->n; k++)
                {
                    if (obs->data[k].sat == sat)
                    {
                        nav->ssr[nav->ns] = ssr;
                        ++nav->ns;
                        break;
                    }
                }
            }
            else
            {
                for (j = 0; j < nav->ns; ++j)
                {
                    int idx = -1;
                    for (k = 0; k < obs->n; k++)
                    {
                        if (obs->data[k].sat == nav->ssr[j].sat)
                        {
                            idx = k;
                            break;
                        }
                    }
                    if (idx == -1)
                    {
                        for (k = 0; k < obs->n; k++)
                        {
                            if (obs->data[k].sat == sat)
                            {
                                nav->ssr[j] = ssr;
                                return sync ? 0 : 10;
                            }
                        }
                    }
                }
            }
        }
    }
    return sync ? 0 : 10;
}
/* decode ssr 4: combined orbit and clock corrections ------------------------*/
static int decode_ssr4(rtcm_t *rtcm, int sys, nav_t *nav, obs_t *obs)
{
    double udint, deph[3], ddeph[3], dclk[3];
    int i, j, k, type, nsat, sync, iod, prn, sat, iode, iodcrc, refd = 0, np, ni, nj, offp, loc;
    ssr_t ssr = {0};

    type = rtcm_getbitu(rtcm->buff, 24, 12);

    if ((nsat = decode_ssr1_head(rtcm, sys, &sync, &iod, &udint, &refd, &i)) < 0)
    {
        return -1;
    }
    switch (sys)
    {
    case _SYS_GPS_:
        np = 6;
        ni = 8;
        nj = 0;
        offp = 0;
        break;
    case _SYS_GLO_:
        np = 5;
        ni = 8;
        nj = 0;
        offp = 0;
        break;
    case _SYS_GAL_:
        np = 6;
        ni = 10;
        nj = 0;
        offp = 0;
        break;
    case _SYS_QZS_:
        np = 4;
        ni = 8;
        nj = 0;
        offp = 192;
        break;
    case _SYS_BDS_:
        np = 6;
        ni = 10;
        nj = 24;
        offp = 1;
        break;
    case _SYS_SBS_:
        np = 6;
        ni = 9;
        nj = 24;
        offp = 120;
        break;
    default:
        return sync ? 0 : 10;
    }
    for (j = 0; j < nsat && i + 191 + np + ni + nj <= rtcm->len * 8; j++)
    {
        prn = rtcm_getbitu(rtcm->buff, i, np) + offp;
        i += np;
        iode = rtcm_getbitu(rtcm->buff, i, ni);
        i += ni;
        iodcrc = rtcm_getbitu(rtcm->buff, i, nj);
        i += nj;
        deph[0] = rtcm_getbits(rtcm->buff, i, 22) * 1E-4;
        i += 22;
        deph[1] = rtcm_getbits(rtcm->buff, i, 20) * 4E-4;
        i += 20;
        deph[2] = rtcm_getbits(rtcm->buff, i, 20) * 4E-4;
        i += 20;
        ddeph[0] = rtcm_getbits(rtcm->buff, i, 21) * 1E-6;
        i += 21;
        ddeph[1] = rtcm_getbits(rtcm->buff, i, 19) * 4E-6;
        i += 19;
        ddeph[2] = rtcm_getbits(rtcm->buff, i, 19) * 4E-6;
        i += 19;

        dclk[0] = rtcm_getbits(rtcm->buff, i, 22) * 1E-4;
        i += 22;
        dclk[1] = rtcm_getbits(rtcm->buff, i, 21) * 1E-6;
        i += 21;
        dclk[2] = rtcm_getbits(rtcm->buff, i, 27) * 2E-8;
        i += 27;

        if (!(sat = satno(sys, prn)))
        {
            /*        trace(2,"rtcm3 %d satellite number error: prn=%d\n",type,prn);  */
            continue;
        }
        memset(&ssr, 0, sizeof(ssr_t));
        ssr.sat = sat;
        ssr.t0[0] = ssr.t0[1] = rtcm->time;
        ssr.udi[0] = ssr.udi[1] = udint;
        ssr.iod[0] = ssr.iod[1] = iod;
        ssr.iode = iode;
        ssr.iodcrc = iodcrc;
        ssr.refd = refd;

        for (k = 0; k < 3; k++)
        {
            ssr.deph[k] = deph[k];
            ssr.ddeph[k] = ddeph[k];
            ssr.dclk[k] = dclk[k];
        }
        ssr.update = 1;

        for (loc = 0; loc < nav->ns; ++loc)
        {
            if (nav->ssr[loc].sat == sat)
            {
                break;
            }
        }

        if (loc < nav->ns)
        {
            for (k = 0; k < obs->n; k++)
            {
                if (obs->data[k].sat == sat)
                {
                    nav->ssr[loc] = ssr;
                    break;
                }
            }
        }
        else if (loc == nav->ns)
        {
            if (loc < MAXSSR)
            {
                for (k = 0; k < obs->n; k++)
                {
                    if (obs->data[k].sat == sat)
                    {
                        nav->ssr[nav->ns] = ssr;
                        ++nav->ns;
                        break;
                    }
                }
            }
            else
            {
                for (j = 0; j < nav->ns; ++j)
                {
                    int idx = -1;
                    for (k = 0; k < obs->n; k++)
                    {
                        if (obs->data[k].sat == nav->ssr[j].sat)
                        {
                            idx = k;
                            break;
                        }
                    }
                    if (idx == -1)
                    {
                        for (k = 0; k < obs->n; k++)
                        {
                            if (obs->data[k].sat == sat)
                            {
                                nav->ssr[j] = ssr;
                                return sync ? 0 : 10;
                            }
                        }
                    }
                }
            }
        }
    }
    return sync ? 0 : 10;
}
/* decode ssr 5: ura ---------------------------------------------------------*/
static int decode_ssr5(rtcm_t *rtcm, int sys, nav_t *nav)
{
    double udint;
    int i, j, type, nsat, sync, iod, prn, sat, ura, np, offp, loc;
    ssr_t ssr = {0};
    ;

    type = rtcm_getbitu(rtcm->buff, 24, 12);

    if ((nsat = decode_ssr2_head(rtcm, sys, &sync, &iod, &udint, &i)) < 0)
    {
        return -1;
    }
    switch (sys)
    {
    case _SYS_GPS_:
        np = 6;
        offp = 0;
        break;
    case _SYS_GLO_:
        np = 5;
        offp = 0;
        break;
    case _SYS_GAL_:
        np = 6;
        offp = 0;
        break;
    case _SYS_QZS_:
        np = 4;
        offp = 192;
        break;
    case _SYS_BDS_:
        np = 6;
        offp = 1;
        break;
    case _SYS_SBS_:
        np = 6;
        offp = 120;
        break;
    default:
        return sync ? 0 : 10;
    }
    for (j = 0; j < nsat && i + 6 + np <= rtcm->len * 8; j++)
    {
        prn = rtcm_getbitu(rtcm->buff, i, np) + offp;
        i += np;
        ura = rtcm_getbitu(rtcm->buff, i, 6);
        i += 6;

        if (!(sat = satno(sys, prn)))
        {
            continue;
        }
        memset(&ssr, 0, sizeof(ssr_t));
        ssr.sat = sat;
        ssr.t0[3] = rtcm->time;
        ssr.udi[3] = udint;
        ssr.iod[3] = iod;
        ssr.ura = ura;
        ssr.update = 1;

        for (loc = 0; loc < nav->ns; ++loc)
        {
            if (nav->ssr[loc].sat == sat)
            {
                break;
            }
        }
        if (loc < nav->ns)
        {
            nav->ssr[loc] = ssr;
        }
        else if (loc == nav->ns && loc < MAXEPH)
        {
            nav->ssr[nav->ns] = ssr;
            ++nav->ns;
        }
    }
    return sync ? 0 : 10;
}
/* decode ssr 6: high rate clock correction ----------------------------------*/
static int decode_ssr6(rtcm_t *rtcm, int sys, nav_t *nav)
{
    double udint, hrclk;
    int i, j, type, nsat, sync, iod, prn, sat, np, offp, loc;
    ssr_t ssr = {0};

    type = rtcm_getbitu(rtcm->buff, 24, 12);

    if ((nsat = decode_ssr2_head(rtcm, sys, &sync, &iod, &udint, &i)) < 0)
    {
        return -1;
    }
    switch (sys)
    {
    case _SYS_GPS_:
        np = 6;
        offp = 0;
        break;
    case _SYS_GLO_:
        np = 5;
        offp = 0;
        break;
    case _SYS_GAL_:
        np = 6;
        offp = 0;
        break;
    case _SYS_QZS_:
        np = 4;
        offp = 192;
        break;
    case _SYS_BDS_:
        np = 6;
        offp = 1;
        break;
    case _SYS_SBS_:
        np = 6;
        offp = 120;
        break;
    default:
        return sync ? 0 : 10;
    }
    for (j = 0; j < nsat && i + 22 + np <= rtcm->len * 8; j++)
    {
        prn = rtcm_getbitu(rtcm->buff, i, np) + offp;
        i += np;
        hrclk = rtcm_getbits(rtcm->buff, i, 22) * 1E-4;
        i += 22;

        if (!(sat = satno(sys, prn)))
        {
            continue;
        }
        memset(&ssr, 0, sizeof(ssr_t));
        ssr.sat = sat;
        ssr.t0[2] = rtcm->time;
        ssr.udi[2] = udint;
        ssr.iod[2] = iod;
        ssr.hrclk = hrclk;
        ssr.update = 1;

        for (loc = 0; loc < nav->ns; ++loc)
        {
            if (nav->ssr[loc].sat == sat)
            {
                break;
            }
        }
        if (loc < nav->ns)
        {
            nav->ssr[loc] = ssr;
        }
        else if (loc == nav->ns && loc < MAXEPH)
        {
            nav->ssr[nav->ns] = ssr;
            ++nav->ns;
        }
    }
    return sync ? 0 : 10;
}
/* decode ssr 7 message header -----------------------------------------------*/
static int decode_ssr7_head(rtcm_t *rtcm, int sys, int *sync, int *iod,
                            double *udint, int *dispe, int *mw, int *hsize)
{
    double tod, tow;
    char tstr[64];
    int i = 24 + 12, nsat, udi, provid = 0, solid = 0, ns;

    ns = sys == _SYS_QZS_ ? 4 : 6;

    if (i + (sys == _SYS_GLO_ ? 54 : 51 + ns) > rtcm->len * 8)
        return -1;

    if (sys == _SYS_GLO_)
    {
        tod = rtcm_getbitu(rtcm->buff, i, 17);
        i += 17;
        adjday_glot(&rtcm->time, tod);
    }
    else
    {
        tow = rtcm_getbitu(rtcm->buff, i, 20);
        i += 20;
        adjweek(&rtcm->time, tow);
    }
    udi = rtcm_getbitu(rtcm->buff, i, 4);
    i += 4;
    *sync = rtcm_getbitu(rtcm->buff, i, 1);
    i += 1;
    *iod = rtcm_getbitu(rtcm->buff, i, 4);
    i += 4;
    provid = rtcm_getbitu(rtcm->buff, i, 16);
    i += 16; /* provider id */
    solid = rtcm_getbitu(rtcm->buff, i, 4);
    i += 4; /* solution id */
    *dispe = rtcm_getbitu(rtcm->buff, i, 1);
    i += 1; /* dispersive bias consistency ind */
    *mw = rtcm_getbitu(rtcm->buff, i, 1);
    i += 1; /* MW consistency indicator */
    nsat = rtcm_getbitu(rtcm->buff, i, ns);
    i += ns;
    *udint = ssrudint[udi];

    time2str(rtcm->time, tstr, 2);

    *hsize = i;
    return nsat;
}
/* decode ssr 7: phase bias --------------------------------------------------*/
static int decode_ssr7(rtcm_t *rtcm, int sys, nav_t *nav)
{
    const int *codes;
    double udint, bias, std, pbias[MAXCODE], stdpb[MAXCODE];
    int i, j, k, type, mode, sync, iod, nsat, prn, sat, nbias, ncode, np, mw, offp, sii, swl;
    int dispe, sdc, yaw_ang, yaw_rate;
    int loc;
    ssr_t ssr = {0};

    type = rtcm_getbitu(rtcm->buff, 24, 12);

    if ((nsat = decode_ssr7_head(rtcm, sys, &sync, &iod, &udint, &dispe, &mw, &i)) < 0)
    {
        return -1;
    }
    switch (sys)
    {
    case _SYS_GPS_:
        np = 6;
        offp = 0;
        codes = codes_gps;
        ncode = 17;
        break;
    case _SYS_GLO_:
        np = 5;
        offp = 0;
        codes = codes_glo;
        ncode = 4;
        break;
    case _SYS_GAL_:
        np = 6;
        offp = 0;
        codes = codes_gal;
        ncode = 19;
        break;
    case _SYS_QZS_:
        np = 4;
        offp = 192;
        codes = codes_qzs;
        ncode = 13;
        break;
    case _SYS_BDS_:
        np = 6;
        offp = 1;
        codes = codes_bds;
        ncode = 9;
        break;
    default:
        return sync ? 0 : 10;
    }
    for (j = 0; j < nsat && i + 5 + 17 + np <= rtcm->len * 8; j++)
    {
        prn = rtcm_getbitu(rtcm->buff, i, np) + offp;
        i += np;
        nbias = rtcm_getbitu(rtcm->buff, i, 5);
        i += 5;
        yaw_ang = rtcm_getbitu(rtcm->buff, i, 9);
        i += 9;
        yaw_rate = rtcm_getbits(rtcm->buff, i, 8);
        i += 8;

        for (k = 0; k < MAXCODE; k++)
            pbias[k] = stdpb[k] = 0.0;
        for (k = 0; k < nbias && i + 49 <= rtcm->len * 8; k++)
        {
            mode = rtcm_getbitu(rtcm->buff, i, 5);
            i += 5;
            sii = rtcm_getbitu(rtcm->buff, i, 1);
            i += 1; /* integer-indicator */
            swl = rtcm_getbitu(rtcm->buff, i, 2);
            i += 2; /* WL integer-indicator */
            sdc = rtcm_getbitu(rtcm->buff, i, 4);
            i += 4; /* discontinuity counter */
            bias = rtcm_getbits(rtcm->buff, i, 20);
            i += 20; /* phase bias (m) */
            std = rtcm_getbitu(rtcm->buff, i, 17);
            i += 17; /* phase bias std-dev (m) */
            if (mode <= ncode)
            {
                pbias[codes[mode] - 1] = bias * 0.0001; /* (m) */
                stdpb[codes[mode] - 1] = std * 0.0001;  /* (m) */
            }
            else
            {
            }
        }
        if (!(sat = satno(sys, prn)))
        {
            continue;
        }
        memset(&ssr, 0, sizeof(ssr_t));
        ssr.sat = sat;
        ssr.t0[5] = rtcm->time;
        ssr.udi[5] = udint;
        ssr.iod[5] = iod;
        ssr.yaw_ang = yaw_ang / 256.0 * 180.0;    /* (deg) */
        ssr.yaw_rate = yaw_rate / 8192.0 * 180.0; /* (deg/s) */

        for (k = 0; k < MAXCODE; k++)
        {
            //ssr.pbias[k]=pbias[k];
            //ssr.stdpb[k]=(float)stdpb[k];
        }

        for (loc = 0; loc < nav->ns; ++loc)
        {
            if (nav->ssr[loc].sat == sat)
            {
                break;
            }
        }
        if (loc < nav->ns)
        {
            nav->ssr[loc] = ssr;
        }
        else if (loc == nav->ns && loc < MAXEPH)
        {
            nav->ssr[nav->ns] = ssr;
            ++nav->ns;
        }
    }
    return 20;
}
#endif

RTK_RAM_CODE extern void set_approximate_time(int year, int doy, rtcm_t *rtcm)
{
    int totalDay = (year < 1981) ? (0) : (360);
    int weekNum = 0, i = 0;
    double weekSec = 0.0;
    for (int yearIndex = 1981; yearIndex < year; ++yearIndex)
    {
        totalDay += 365;
        if ((yearIndex % 4 == 0 && yearIndex % 100 != 0) || yearIndex % 400 == 0)
            ++totalDay;
    }
    totalDay += doy;
    weekNum = totalDay / 7;
    weekSec = (totalDay - weekNum * 7) * 24 * 3600.0;
    rtcm->time = gpst2time(weekNum, weekSec);
    return;
}

/* decode rtcm ver.3 message -------------------------------------------------*/
RTK_RAM_CODE int decode_rtcm3(rtcm_t *rtcm, obs_t *obs, nav_t *nav)
{
    int ret = 0, type = rtcm->type = rtcm_getbitu(rtcm->buff, 24, 12);

    switch (type)
    {
	case 999:
		ret = decode_type999(rtcm, obs);
        break;
    case 1001:
        ret = decode_type1001(rtcm, obs);
        break; /* not supported */
    case 1002:
        ret = decode_type1002(rtcm, obs);
        break;
    case 1003:
        ret = decode_type1003(rtcm, obs);
        break; /* not supported */
    case 1004:
        ret = decode_type1004(rtcm, obs);
        break;
    case 1005:
        ret = decode_type1005(rtcm, obs);
        break;
    case 1006:
        ret = decode_type1006(rtcm, obs);
        break;
    case 1007:
        ret = decode_type1007(rtcm, obs);
        break;
    case 1008:
        ret = decode_type1008(rtcm, obs);
        break;
    case 1009:
        ret = decode_type1009(rtcm, obs);
        break; /* not supported */
    case 1010:
        ret = decode_type1010(rtcm, obs);
        break;
    case 1011:
        ret = decode_type1011(rtcm, obs);
        break; /* not supported */
    case 1012:
        ret = decode_type1012(rtcm, obs);
        break;
    case 1013:
        ret = decode_type1013(rtcm);
        break; /* not supported */
    case 1019:
        ret = decode_type1019(rtcm, nav);
        break;
    case 1020:
        ret = decode_type1020(rtcm, nav);
        break;
    case 1021:
        ret = decode_type1021(rtcm);
        break; /* not supported */
    case 1022:
        ret = decode_type1022(rtcm);
        break; /* not supported */
    case 1023:
        ret = decode_type1023(rtcm);
        break; /* not supported */
    case 1024:
        ret = decode_type1024(rtcm);
        break; /* not supported */
    case 1025:
        ret = decode_type1025(rtcm);
        break; /* not supported */
    case 1026:
        ret = decode_type1026(rtcm);
        break; /* not supported */
    case 1027:
        ret = decode_type1027(rtcm);
        break; /* not supported */
    case 1029:
        ret = decode_type1029(rtcm);
        break;
    case 1030:
        ret = decode_type1030(rtcm);
        break; /* not supported */
    case 1031:
        ret = decode_type1031(rtcm);
        break; /* not supported */
    case 1032:
        ret = decode_type1032(rtcm);
        break; /* not supported */
    case 1033:
        ret = decode_type1033(rtcm, obs);
        break;
    case 1034:
        ret = decode_type1034(rtcm);
        break; /* not supported */
    case 1035:
        ret = decode_type1035(rtcm);
        break; /* not supported */
    case 1037:
        ret = decode_type1037(rtcm);
        break; /* not supported */
    case 1038:
        ret = decode_type1038(rtcm);
        break; /* not supported */
    case 1039:
        ret = decode_type1039(rtcm);
        break; /* not supported */
    case 1044:
        ret = decode_type1044(rtcm, nav);
        break;
    case 1045:
        ret = decode_type1045(rtcm, nav);
        break;
    case 1046:
        ret = decode_type1046(rtcm, nav);
        break;
    case 63:
        ret = decode_type1042(rtcm, nav);
        break; /* RTCM draft */
    case 1042:
        ret = decode_type1042(rtcm, nav);
        break;
#ifdef _USE_PPP_
    case 1057:
        ret = decode_ssr1(rtcm, _SYS_GPS_, nav, obs);
        break;
    case 1058:
        ret = decode_ssr2(rtcm, _SYS_GPS_, nav, obs);
        break;
    case 1059:
        ret = decode_ssr3(rtcm, _SYS_GPS_, nav, obs);
        break;
    case 1060:
        ret = decode_ssr4(rtcm, _SYS_GPS_, nav, obs);
        break;
    case 1061:
        ret = decode_ssr5(rtcm, _SYS_GPS_, nav);
        break;
    case 1062:
        ret = decode_ssr6(rtcm, _SYS_GPS_, nav);
        break;
    case 1063:
        ret = decode_ssr1(rtcm, _SYS_GLO_, nav, obs);
        break;
    case 1064:
        ret = decode_ssr2(rtcm, _SYS_GLO_, nav, obs);
        break;
    case 1065:
        ret = decode_ssr3(rtcm, _SYS_GLO_, nav, obs);
        break;
    case 1066:
        ret = decode_ssr4(rtcm, _SYS_GLO_, nav, obs);
        break;
    case 1067:
        ret = decode_ssr5(rtcm, _SYS_GLO_, nav);
        break;
    case 1068:
        ret = decode_ssr6(rtcm, _SYS_GLO_, nav);
        break;
#endif
    case 1071:
        ret = decode_msm0(rtcm, obs, _SYS_GPS_);
        break; /* not supported */
    case 1072:
        ret = decode_msm0(rtcm, obs, _SYS_GPS_);
        break; /* not supported */
    case 1073:
        ret = decode_msm0(rtcm, obs, _SYS_GPS_);
        break; /* not supported */
    case 1074:
        ret = decode_msm4(rtcm, obs, _SYS_GPS_);
        break;
    case 1075:
        ret = decode_msm5(rtcm, obs, _SYS_GPS_);
        break;
    case 1076:
        ret = decode_msm6(rtcm, obs, _SYS_GPS_);
        break;
    case 1077:
        ret = decode_msm7(rtcm, obs, _SYS_GPS_);
        break;
    case 1081:
        ret = decode_msm0(rtcm, obs, _SYS_GLO_);
        break; /* not supported */
    case 1082:
        ret = decode_msm0(rtcm, obs, _SYS_GLO_);
        break; /* not supported */
    case 1083:
        ret = decode_msm0(rtcm, obs, _SYS_GLO_);
        break; /* not supported */
    case 1084:
        ret = decode_msm4(rtcm, obs, _SYS_GLO_);
        break;
    case 1085:
        ret = decode_msm5(rtcm, obs, _SYS_GLO_);
        break;
    case 1086:
        ret = decode_msm6(rtcm, obs, _SYS_GLO_);
        break;
    case 1087:
        ret = decode_msm7(rtcm, obs, _SYS_GLO_);
        break;
    case 1091:
        ret = decode_msm0(rtcm, obs, _SYS_GAL_);
        break; /* not supported */
    case 1092:
        ret = decode_msm0(rtcm, obs, _SYS_GAL_);
        break; /* not supported */
    case 1093:
        ret = decode_msm0(rtcm, obs, _SYS_GAL_);
        break; /* not supported */
    case 1094:
        ret = decode_msm4(rtcm, obs, _SYS_GAL_);
        break;
    case 1095:
        ret = decode_msm5(rtcm, obs, _SYS_GAL_);
        break;
    case 1096:
        ret = decode_msm6(rtcm, obs, _SYS_GAL_);
        break;
    case 1097:
        ret = decode_msm7(rtcm, obs, _SYS_GAL_);
        break;
    case 1101:
        ret = decode_msm0(rtcm, obs, _SYS_SBS_);
        break; /* not supported */
    case 1102:
        ret = decode_msm0(rtcm, obs, _SYS_SBS_);
        break; /* not supported */
    case 1103:
        ret = decode_msm0(rtcm, obs, _SYS_SBS_);
        break; /* not supported */
    case 1104:
        ret = decode_msm4(rtcm, obs, _SYS_SBS_);
        break;
    case 1105:
        ret = decode_msm5(rtcm, obs, _SYS_SBS_);
        break;
    case 1106:
        ret = decode_msm6(rtcm, obs, _SYS_SBS_);
        break;
    case 1107:
        ret = decode_msm7(rtcm, obs, _SYS_SBS_);
        break;
    case 1111:
        ret = decode_msm0(rtcm, obs, _SYS_QZS_);
        break; /* not supported */
    case 1112:
        ret = decode_msm0(rtcm, obs, _SYS_QZS_);
        break; /* not supported */
    case 1113:
        ret = decode_msm0(rtcm, obs, _SYS_QZS_);
        break; /* not supported */
    case 1114:
        ret = decode_msm4(rtcm, obs, _SYS_QZS_);
        break;
    case 1115:
        ret = decode_msm5(rtcm, obs, _SYS_QZS_);
        break;
    case 1116:
        ret = decode_msm6(rtcm, obs, _SYS_QZS_);
        break;
    case 1117:
        ret = decode_msm7(rtcm, obs, _SYS_QZS_);
        break;
    case 1121:
        ret = decode_msm0(rtcm, obs, _SYS_BDS_);
        break; /* not supported */
    case 1122:
        ret = decode_msm0(rtcm, obs, _SYS_BDS_);
        break; /* not supported */
    case 1123:
        ret = decode_msm0(rtcm, obs, _SYS_BDS_);
        break; /* not supported */
    case 1124:
        ret = decode_msm4(rtcm, obs, _SYS_BDS_);
        break;
    case 1125:
        ret = decode_msm5(rtcm, obs, _SYS_BDS_);
        break;
    case 1126:
        ret = decode_msm6(rtcm, obs, _SYS_BDS_);
        break;
    case 1127:
        ret = decode_msm7(rtcm, obs, _SYS_BDS_);
        break;
	case 1134:
		ret = decode_msm4(rtcm, obs, _SYS_NONE_);
		break;
	case 1137:
		ret = decode_msm7(rtcm, obs, _SYS_NONE_);
		break;
    case 1230:
        ret = decode_type1230(rtcm);
        break; /* not supported */
#ifdef _USE_PPP_
    case 1240:
        ret = decode_ssr1(rtcm, _SYS_GAL_, nav, obs);
        break;
    case 1241:
        ret = decode_ssr2(rtcm, _SYS_GAL_, nav, obs);
        break;
    case 1242:
        ret = decode_ssr3(rtcm, _SYS_GAL_, nav, obs);
        break;
    case 1243:
        ret = decode_ssr4(rtcm, _SYS_GAL_, nav, obs);
        break;
    case 1244:
        ret = decode_ssr5(rtcm, _SYS_GAL_, nav);
        break;
    case 1245:
        ret = decode_ssr6(rtcm, _SYS_GAL_, nav);
        break;
    case 1246:
        ret = decode_ssr1(rtcm, _SYS_QZS_, nav, obs);
        break;
    case 1247:
        ret = decode_ssr2(rtcm, _SYS_QZS_, nav, obs);
        break;
    case 1248:
        ret = decode_ssr3(rtcm, _SYS_QZS_, nav, obs);
        break;
    case 1249:
        ret = decode_ssr4(rtcm, _SYS_QZS_, nav, obs);
        break;
    case 1250:
        ret = decode_ssr5(rtcm, _SYS_QZS_, nav);
        break;
    case 1251:
        ret = decode_ssr6(rtcm, _SYS_QZS_, nav);
        break;
    case 1252:
        ret = decode_ssr1(rtcm, _SYS_SBS_, nav, obs);
        break;
    case 1253:
        ret = decode_ssr2(rtcm, _SYS_SBS_, nav, obs);
        break;
    case 1254:
        ret = decode_ssr3(rtcm, _SYS_SBS_, nav, obs);
        break;
    case 1255:
        ret = decode_ssr4(rtcm, _SYS_SBS_, nav, obs);
        break;
    case 1256:
        ret = decode_ssr5(rtcm, _SYS_SBS_, nav);
        break;
    case 1257:
        ret = decode_ssr6(rtcm, _SYS_SBS_, nav);
        break;
    case 1258:
        ret = decode_ssr1(rtcm, _SYS_BDS_, nav, obs);
        break;
    case 1259:
        ret = decode_ssr2(rtcm, _SYS_BDS_, nav, obs);
        break;
    case 1260:
        ret = decode_ssr3(rtcm, _SYS_BDS_, nav, obs);
        break;
    case 1261:
        ret = decode_ssr4(rtcm, _SYS_BDS_, nav, obs);
        break;
    case 1262:
        ret = decode_ssr5(rtcm, _SYS_BDS_, nav);
        break;
    case 1263:
        ret = decode_ssr6(rtcm, _SYS_BDS_, nav);
        break;
    case 11:
        ret = decode_ssr7(rtcm, _SYS_GLO_, nav);
        break; /* tentative */
    case 12:
        ret = decode_ssr7(rtcm, _SYS_GAL_, nav);
        break; /* tentative */
    case 13:
        ret = decode_ssr7(rtcm, _SYS_QZS_, nav);
        break; /* tentative */
    case 14:
        ret = decode_ssr7(rtcm, _SYS_BDS_, nav);
        break; /* tentative */
#endif
    default:
        ret = 0;
    }

    return ret;
}

/* input rtcm 3 message from stream --------------------------------------------
* fetch next rtcm 3 message and input a message from byte stream
* args   : rtcm_t *rtcm IO   rtcm control struct
*          unsigned char data I stream data (1 byte)
* return : status (-1: error message, 0: no message, 1: input observation data,
*                  2: input ephemeris, 5: input station pos/ant parameters,
*                  10: input ssr messages)
* notes  : before firstly calling the function, time in rtcm control struct has
*          to be set to the approximate time within 1/2 week in order to resolve
*          ambiguity of time in rtcm messages.
*          
*          to specify input options, set rtcm->opt to the following option
*          strings separated by spaces.
*
*          -EPHALL  : input all ephemerides
*          -STA=nnn : input only message with STAID=nnn
*          -GLss    : select signal ss for GPS MSM (ss=1C,1P,...)
*          -RLss    : select signal ss for GLO MSM (ss=1C,1P,...)
*          -ELss    : select signal ss for GAL MSM (ss=1C,1B,...)
*          -JLss    : select signal ss for QZS MSM (ss=1C,2C,...)
*          -CLss    : select signal ss for BDS MSM (ss=2I,7I,...)
*
*          supported RTCM 3 messages
*             (ref [2][3][4][5][6][7][8][9][10][11][12][13][14][15][16][17])
*
*            TYPE       GPS     GLOASS    GALILEO    QZSS     BEIDOU     SBAS
*         ----------------------------------------------------------------------
*          OBS C-L1  : 1001~     1009~       -         -         -         -
*              F-L1  : 1002      1010        -         -         -         -
*              C-L12 : 1003~     1011~       -         -         -         -
*              F-L12 : 1004      1012        -         -         -         -
*
*          NAV       : 1019      1020      1045      1044      1042        -
*                        -         -       1046        -         63*       -
*
*          MSM 1     : 1071~     1081~     1091~     1111~     1121~     1101~
*              2     : 1072~     1082~     1092~     1112~     1122~     1102~
*              3     : 1073~     1083~     1093~     1113~     1123~     1103~
*              4     : 1074      1084      1094      1114      1124      1104
*              5     : 1075      1085      1095      1115      1125      1105
*              6     : 1076      1086      1096      1116      1126      1106
*              7     : 1077      1087      1097      1117      1127      1107
*
*          SSR OBT   : 1057      1063      1240*     1246*     1258*       -
*              CLK   : 1058      1064      1241*     1247*     1259*       -
*              BIAS  : 1059      1065      1242*     1248*     1260*       -
*              OBTCLK: 1060      1066      1243*     1249*     1261*       -
*              URA   : 1061      1067      1244*     1250*     1262*       -
*              HRCLK : 1062      1068      1245*     1251*     1263*       -
*
*          ANT INFO  : 1005 1006 1007 1008 1033
*         ----------------------------------------------------------------------
*                                                    (* draft, ~ only encode)
*
*          for MSM observation data with multiple signals for a frequency,
*          a signal is selected according to internal priority. to select
*          a specified signal, use the input options.
*
*          rtcm3 message format:
*            +----------+--------+-----------+--------------------+----------+
*            | preamble | 000000 |  length   |    data message    |  parity  |
*            +----------+--------+-----------+--------------------+----------+
*            |<-- 8 --->|<- 6 -->|<-- 10 --->|<--- length x 8 --->|<-- 24 -->|
*            
*-----------------------------------------------------------------------------*/

extern uint8_t stnID;
RTK_RAM_CODE extern int input_rtcm3_data(rtcm_t *rtcm, unsigned char data, obs_t *obs, nav_t *nav)
{
    /* synchronize frame */
    int ret = 0;
    int maxSize = sizeof(rtcm->buff);

	if ((int)rtcm->nbyte >= maxSize)
	{
		rtcm->nbyte = 0;
	}
    rtcm->type = 0;
    if (rtcm->nbyte == 0)
    {
        /* key = 0 => RTCM, key = 1 => NMEA */
        if (data == RTCM3PREAMB)
        {
            rtcm->key = data;
            rtcm->buff[rtcm->nbyte++] = data;
        }
        return 0;
    }

    /* RTCM decorder */
    rtcm->buff[rtcm->nbyte++] = data;

    if (rtcm->nbyte == 3)
    {
        rtcm->len = rtcm_getbitu(rtcm->buff, 14, 10) + 3; /* length without parity */
    }
	if (rtcm->nbyte < 3 || rtcm->nbyte < rtcm->len + 3)
	{
		return 0;
	}
#ifdef ROVER_OUT_ENABLE
	if ((s32)stnID == ROVER)
	{
		// s32 i = 36, sbu_type_ID = 0;
		// sbu_type_ID = (s32)rtcm_getbitu(rtcm->buff, i, 8);

		//if (rtcm->type != 999 || sbu_type_ID == 21)
		{
			u8 rover_to_debug[1300] = { "rover" };
			gpOS_clock_t time_out = 10;
			memcpy(rover_to_debug + strlen("rover"), &rtcm->nbyte, sizeof(s16));
			memcpy(rover_to_debug + strlen("rover") + sizeof(s16), rtcm->buff, rtcm->nbyte);
			svc_uart_write_bsp(2, (tU8*)rover_to_debug, strlen("rover") + sizeof(s16) + rtcm->nbyte, &time_out);
		}
	}
#endif
    rtcm->nbyte = 0;
    rtcm->type = rtcm_getbitu(rtcm->buff, 24, 12);

    /* check parity */
    if (rtk_crc24q(rtcm->buff, rtcm->len) != rtcm_getbitu(rtcm->buff, rtcm->len * 8, 24))
    {
        return 0;
    }
    /* decode rtcm3 message */
    return decode_rtcm3(rtcm, obs, nav);
}

RTK_RAM_CODE int is_complete_rtcm(rtcm_t *rtcm, unsigned char data) 
{
	/* synchronize frame */
	int ret = 0;
	int maxSize = sizeof(rtcm->buff);
	if ((int)rtcm->nbyte >= maxSize)
	{
		rtcm->nbyte = 0;
	}
	rtcm->type = 0;
	if (rtcm->nbyte == 0)
	{
		/* key = 0 => RTCM, key = 1 => NMEA */
		if (data == RTCM3PREAMB)
		{
			rtcm->key = data;
			rtcm->buff[rtcm->nbyte++] = data;
		}
		return 0;
	}

	/* RTCM decorder */
	rtcm->buff[rtcm->nbyte++] = data;

	if (rtcm->nbyte == 3)
	{
		rtcm->len = rtcm_getbitu(rtcm->buff, 14, 10) + 3; /* length without parity */
	}
	if (rtcm->nbyte < 3 || rtcm->nbyte < rtcm->len + 3)
	{
		return 0;
	}
	rtcm->nbyte = 0;
	rtcm->type = rtcm_getbitu(rtcm->buff, 24, 12);

	/* check parity */
	if (rtk_crc24q(rtcm->buff, rtcm->len) != rtcm_getbitu(rtcm->buff, rtcm->len * 8, 24))
	{
		return 0;
	}
	else 
	{
		return 1;
	}
}

RTK_RAM_CODE extern int input_rtcm3(unsigned char data, unsigned int stnID, gnss_rtcm_t *gnss)
{
    rtcm_t *rtcm = NULL;
    obs_t *obs = NULL;
    nav_t *nav = NULL;
    int i, week, ret = 0;
	double tt = 0.0, tow, time;
    //static int obs_flag = 0;

    if (stnID < MAXSTN)
    {
        rtcm = gnss->rcv + stnID;
        nav = &gnss->nav;
        obs = gnss->obs + stnID;
        ret = input_rtcm3_data(rtcm, data, obs, nav);

		if (ret == 1)
		{
			for (i = 0; i < NSYS*NFREQ; i++)
			{
				if (gnss->rcv[0].icode[i] > 0)
					gnss->rcv[1].icode[i] = gnss->rcv[0].icode[i];
			}

			if (obs->rtcmtype == 99)
			{
				if (rtcm->teseo.fix_status == 0)
				{
					rtcm->teseo.time = 0.0;
					rtcm->teseo.pos[0] = rtcm->teseo.pos[1] = rtcm->teseo.pos[2] = 0.0;
				}
				tow = time2gpst(obs->time, &week);
				time = tow + week * SECONDS_IN_WEEK;
				tt = time - rtcm->teseo.time;
				tt = tt < 5.0 ? tt: 0.0;
				if (fabs(tt) > SECONDS_IN_WEEK *0.5)
				{
					week = floor(rtcm->teseo.time / SECONDS_IN_WEEK);
					tow = rtcm->teseo.time - week * SECONDS_IN_WEEK;
					rtcm->time = obs->time = gpst2time(week, tow);
				}
				for (i = 0; i < 3; i++)
				{
					obs->pos[i] = rtcm->teseo.pos[i] + rtcm->teseo.vel[i] * tt;
                    obs->pos[i + 3] = rtcm->teseo.vel[i];
				}
			}

			if (fabs(timediff(gnss->rcv[0].time, gnss->rcv[1].time)) > 1800.0)
			{
				gnss->rcv[1].time = gnss->rcv[0].time;
			}
			if (stnID == 0)
			{
				obs->geo_sep = rtcm->teseo.geo_sep;
			}
		}

    }

    return ret;
}

/* transform ecef to geodetic postion ------------------------------------------
* transform ecef position to geodetic position
* args   : double *r        I   ecef position {x,y,z} (m)
*          double *pos      O   geodetic position {lat,lon,h} (rad,m)
* return : none
* notes  : WGS84, ellipsoidal height
*-----------------------------------------------------------------------------*/
RTK_RAM_CODE extern void ecef2pos(const double *r, double *pos)
{
    double e2 = FE_WGS84 * (2.0 - FE_WGS84), r2 = r[0] * r[0] + r[1] * r[1], z, zk, v = RE_WGS84, sinp;

    for (z = r[2], zk = 0.0; fabs(z - zk) >= 1E-4;)
    {
        zk = z;
        sinp = z / sqrt(r2 + z * z);
        v = RE_WGS84 / sqrt(1.0 - e2 * sinp * sinp);
        z = r[2] + v * e2 * sinp;
    }
    pos[0] = r2 > 1E-12 ? atan(z / sqrt(r2)) : (r[2] > 0.0 ? PI / 2.0 : -PI / 2.0);
    pos[1] = r2 > 1E-12 ? atan2(r[1], r[0]) : 0.0;
    pos[2] = sqrt(r2 + z * z) - v;
}

/* transform geodetic to ecef position -----------------------------------------
* transform geodetic position to ecef position
* args   : double *pos      I   geodetic position {lat,lon,h} (rad,m)
*          double *r        O   ecef position {x,y,z} (m)
* return : none
* notes  : WGS84, ellipsoidal height
*-----------------------------------------------------------------------------*/
RTK_RAM_CODE extern void pos2ecef(const double *pos, double *r)
{
    double sinp = sin(pos[0]), cosp = cos(pos[0]), sinl = sin(pos[1]), cosl = cos(pos[1]);
    double e2 = FE_WGS84 * (2.0 - FE_WGS84), v = RE_WGS84 / sqrt(1.0 - e2 * sinp * sinp);

    r[0] = (v + pos[2]) * cosp * cosl;
    r[1] = (v + pos[2]) * cosp * sinl;
    r[2] = (v * (1.0 - e2) + pos[2]) * sinp;
}


// void set_ms(uint32_t ms)
// {
// #ifdef INT_SEC_SEND
//     sensor_time_s.ms += ms; 
// #endif
// }

// #ifdef INT_SEC_SEND
// TIME_S *get_time()
// {
//     return &sensor_time_s;
// }
// #endif

RTK_RAM_CODE extern int add_obs(obsd_t* obsd, obs_t* obs)
{
	int i;
	double tt = timediff(obs->time, obsd->time);

    if (obsd->sat <= 0)	return 0;
    if (fabs(tt) > 0.01)
    {
        /* new epoch, reset the n and obsflag */
        obs->n = 0;
        obs->obsflag = 0;
        memset(obs->data, 0, sizeof(obsd_t)*MAXOBS);
    }

	if (obs->n >= MAXOBS)	obs->n = 0;

	if (obs->n == 0)
	{
		/* first obs, set the time tag */
		obs->time = obsd->time;
	}
	for (i = 0; i < (int)obs->n; i++)
	{
		if (obs->data[i].sat == obsd->sat)
			break; /* field already exists */
	}
	if (i == obs->n)
	{
		/* add new field */
		if (obs->n < MAXOBS)
		{
			obs->data[i] = *obsd;
			obs->n++;
		}
		else
		{
			i = -1;
		}
	}
	else
	{
		/* duplicated satellites */
		obs->data[i] = *obsd;
	}

	return obs->n;
}

RTK_RAM_CODE extern int add_eph(eph_t* eph, nav_t* nav)
{
	int i = 0, ret = 0;
	int sat = eph->sat;
	int bestL = -1;
	double bestT = 0.0;
	if (sat <= 0) return 0;
	for (i = 0; i < (int)nav->n; ++i)
	{
		if (nav->eph[i].sat == sat)
		{
			break;
		}
	}
	if (i < (int)nav->n)
	{
		/* replace old */
		nav->eph[i] = *eph;
		nav->ephsat = sat;
	}
	else if (i == nav->n)
	{
		if (i < MAXEPH)
		{
			nav->eph[nav->n] = *eph;
			nav->ephsat = sat;
			++nav->n;
			ret = 1;
		}
		else
		{
			/* remove the oldest one */
			for (i = 0; i < (int)nav->n; ++i)
			{
				double diffT = fabs(timediff(nav->eph[i].toe, eph->toe));
				if (bestL < 0 || bestT < diffT)
				{
					bestL = i;
					bestT = diffT;
				}
			}
			if (bestL >= 0)
			{
				nav->eph[bestL] = *eph;
				nav->ephsat = sat;
				ret = 0;
			}
		}
	}
	return ret;
}

RTK_RAM_CODE extern int add_geph(geph_t* eph, nav_t* nav)
{
	int i = 0, ret = 0;
	int sat = eph->sat;
	int bestL = -1;
	double bestT = 0.0;

	if (sat <= 0) return 0;

	for (i = 0; i < (int)nav->ng; ++i)
	{
		if (nav->geph[i].sat == sat)
		{
			break;
		}
	}
	if (i < (int)nav->ng)
	{
		/* replace old */
		nav->geph[i] = *eph;
		nav->ephsat = sat;
	}
	else if (i == nav->ng)
	{
		if (i < MAXEPH_R)
		{
			nav->geph[nav->ng] = *eph;
			nav->ephsat = sat;
			++nav->ng;
			ret = 1;
		}
		else
		{
			/* remove the oldest one */
			for (i = 0; i < (int)nav->ng; ++i)
			{
				double diffT = fabs(timediff(nav->geph[i].toe, eph->toe));
				if (bestL < 0 || bestT < diffT)
				{
					bestL = i;
					bestT = diffT;
				}
			}
			if (bestL >= 0)
			{
				nav->geph[bestL] = *eph;
				nav->ephsat = sat;
				ret = 0;
			}
		}
	}
	return ret;
}

RTK_RAM_CODE extern void rtk_teseo(gnss_rtcm_t *gnss, unsigned int stnID, rcv_rtk_t *rtk)
{
	rtcm_t *rtcm = NULL;

	if (stnID < MAXSTN)
	{
		rtcm = gnss->rcv + stnID;
		rtk->dop[1] = rtcm->teseo.pdop * 0.1;    /* GDOP/PDOP/HDOP/VDOP */
		rtk->dop[2] = rtcm->teseo.hdop * 0.1;
		rtk->dop[3] = rtcm->teseo.vdop * 0.1;
		rtk->geo_sep = rtcm->teseo.geo_sep;
		memcpy(rtk->lev_pos, rtcm->teseo.lev_pos, sizeof(double) * 3);
	}

	return;
}
