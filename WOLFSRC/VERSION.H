//
// Which game this build is for. The original chose by editing this file;
// the Makefile passes WOLF_WL1 for the shareware episode, WOLF_SOD for
// Spear of Destiny and WOLF_SDM for its two-level demo instead, and the
// registered six episodes are the default, as they were here.
//
#if defined(WOLF_WL1)
#include "WOLF1VER.H"
#elif defined(WOLF_SOD)
#include "SODVER.H"
#elif defined(WOLF_SDM)
#include "SDMVER.H"
#else
//#define SPEAR
//#define JAPAN
#define GOODTIMES
#define ARTSEXTERN
#define DEMOSEXTERN
//#define MYPROFILE
//#define DEBCHECK
#define CARMACIZED
//#define UPLOAD
#endif
