TOP = ../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#----------------------------------------------------
#  Optimization of db files using dbst (DEFAULT: NO)
#DB_OPT = YES

#----------------------------------------------------
# Create and install (or just install)
# databases, templates, substitutions like this
DB += tpr_master.db

#----------------------------------------------------
# Create and install (or just install)
# Autosave request files, templates, substitutions like this
REQ += autosave_tpr.req

#----------------------------------------------------
# Create and install (or just install)
# Archive files, templates, substitutions like this
ARCHIVE += archive_tpr.archive

# Nothing to build for autosave, just install from $AUTOSAVE/db
#DB_INSTALLS += $(AUTOSAVE)/db/save_restoreStatus.db

include $(TOP)/configure/RULES
