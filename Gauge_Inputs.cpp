#include "Gauge_Inputs.h"

const GiInput GI_TABLE[GI_COUNT] = {
  //  key               path             label            unit   cat          kind       min   max   dp
  { "oil_psi",        "oil_psi",        "OIL",           "PSI", GI_PRESS,    GI_NUM,     0,  120, 0 },
  { "oil_status",     "oil_status",     "OIL STATUS",    "",    GI_NOCAT,    GI_OILST,   1,    3, 0 },
  { "boost_psi",      "boost_psi",      "BOOST",         "PSI", GI_PRESS,    GI_NUM,   -15,   30, 1 },
  { "afr",            "afr",            "AFR",           "",    GI_NOCAT,    GI_NUM,    10,   20, 1 },
  { "engine_running", "engine_running", "ENGINE RUNNING","",    GI_NOCAT,    GI_BOOL,    0,    1, 0 },
};
