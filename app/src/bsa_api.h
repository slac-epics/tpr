#define EDEF_MAX 64
typedef uint64_t BsaPulseId;

struct BsaTimingPatternStruct {
	BsaPulseId     pulseId;
	uint64_t       edefInitMask, edefActiveMask, edefAvgDoneMask, edefUpdateMask;
	uint64_t       edefMinorMask, edefMajorMask;
	epicsTimeStamp timeStamp;
};

typedef struct BsaTimingPatternStruct *BsaTimingPattern;

extern void BsaTimingCallback(BsaTimingPattern newPattern);
