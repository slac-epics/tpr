TOP=../..

include $(TOP)/configure/CONFIG

#=============================
# build a library


DBD += tpr.dbd
LIBRARY_IOC = tpr
tpr_SRCS += devTpr.c drvTpr.c tprProcess.c
tpr_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES

#----------------------------------------
#  ADD RULES AFTER THIS LINE

