TOP=../..

include $(TOP)/configure/CONFIG

#=============================
# build a library


# INC+=evrTime.h
# DBDINC += longSubRecord
DBD += drvTpr.dbd
LIBRARY_IOC = tpr
tpr_SRCS += devTpr.c drvTpr.c
tpr_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES

#----------------------------------------
#  ADD RULES AFTER THIS LINE

