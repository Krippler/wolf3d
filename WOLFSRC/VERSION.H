//
// Which game this build is for. The original chose by editing this file;
// the Makefile passes WOLF_WL1 for the shareware episode instead, and the
// registered six episodes are the default, as they were here.
//
#ifdef WOLF_WL1
#include "WOLF1VER.H"
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
