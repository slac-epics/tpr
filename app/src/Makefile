TOP=../..

include $(TOP)/configure/CONFIG

#=============================
# build a library


#INC+=timesyncAPI.h
DBD += tpr.dbd
LIBRARY_IOC = tpr noBSA
tpr_SRCS += devTpr.c drvTpr.c tprProcess.c
tpr_LIBS += $(EPICS_BASE_IOC_LIBS)
noBSA_SRCS += noBSA.c

#===========================

include $(TOP)/configure/RULES

#----------------------------------------
#  ADD RULES AFTER THIS LINE

