#
# Generated Makefile - do not edit!
#
# Edit the Makefile in the project folder instead (../Makefile). Each target
# has a -pre and a -post target defined where you can add customized code.
#
# This makefile implements configuration specific macros and targets.


# Environment
MKDIR=mkdir
CP=cp
GREP=grep
NM=nm
CCADMIN=CCadmin
RANLIB=ranlib
CC=gcc
CCC=g++
CXX=g++
FC=gfortran
AS=as

# Macros
CND_PLATFORM=GNU-Linux
CND_DLIB_EXT=so
CND_CONF=Debug
CND_DISTDIR=dist
CND_BUILDDIR=build

# Include project Makefile
include Makefile

# Object Directory
OBJECTDIR=${CND_BUILDDIR}/${CND_CONF}/${CND_PLATFORM}

# Object Files
OBJECTFILES= \
	${OBJECTDIR}/_ext/5c0/RTP_MIDI.o \
	${OBJECTDIR}/_ext/5c0/RTP_MIDI_AppleProtocol.o \
	${OBJECTDIR}/_ext/5c0/RTP_MIDI_Input.o \
	${OBJECTDIR}/_ext/5c0/XPlatformUtils.o \
	${OBJECTDIR}/_ext/5c0/jackrtpmidid.o \
	${OBJECTDIR}/_ext/5c0/network.o


# C Compiler Flags
CFLAGS=

# CC Compiler Flags
CCFLAGS=
CXXFLAGS=

# Fortran Compiler Flags
FFLAGS=

# Assembler Flags
ASFLAGS=

# Link Libraries and Options
LDLIBSOPTIONS=-ljack

# Build Targets
.build-conf: ${BUILD_SUBPROJECTS}
	"${MAKE}"  -f nbproject/Makefile-${CND_CONF}.mk ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/jackrtpmidid

${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/jackrtpmidid: ${OBJECTFILES}
	${MKDIR} -p ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}
	${LINK.cc} -o ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/jackrtpmidid ${OBJECTFILES} ${LDLIBSOPTIONS}

${OBJECTDIR}/_ext/5c0/RTP_MIDI.o: ../RTP_MIDI.cpp
	${MKDIR} -p ${OBJECTDIR}/_ext/5c0
	${RM} "$@.d"
	$(COMPILE.cc) -g -D__TARGET_LINUX__ -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/5c0/RTP_MIDI.o ../RTP_MIDI.cpp

${OBJECTDIR}/_ext/5c0/RTP_MIDI_AppleProtocol.o: ../RTP_MIDI_AppleProtocol.cpp
	${MKDIR} -p ${OBJECTDIR}/_ext/5c0
	${RM} "$@.d"
	$(COMPILE.cc) -g -D__TARGET_LINUX__ -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/5c0/RTP_MIDI_AppleProtocol.o ../RTP_MIDI_AppleProtocol.cpp

${OBJECTDIR}/_ext/5c0/RTP_MIDI_Input.o: ../RTP_MIDI_Input.cpp
	${MKDIR} -p ${OBJECTDIR}/_ext/5c0
	${RM} "$@.d"
	$(COMPILE.cc) -g -D__TARGET_LINUX__ -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/5c0/RTP_MIDI_Input.o ../RTP_MIDI_Input.cpp

${OBJECTDIR}/_ext/5c0/XPlatformUtils.o: ../XPlatformUtils.cpp
	${MKDIR} -p ${OBJECTDIR}/_ext/5c0
	${RM} "$@.d"
	$(COMPILE.cc) -g -D__TARGET_LINUX__ -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/5c0/XPlatformUtils.o ../XPlatformUtils.cpp

${OBJECTDIR}/_ext/5c0/jackrtpmidid.o: ../jackrtpmidid.cpp
	${MKDIR} -p ${OBJECTDIR}/_ext/5c0
	${RM} "$@.d"
	$(COMPILE.cc) -g -D__TARGET_LINUX__ -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/5c0/jackrtpmidid.o ../jackrtpmidid.cpp

${OBJECTDIR}/_ext/5c0/network.o: ../network.cpp
	${MKDIR} -p ${OBJECTDIR}/_ext/5c0
	${RM} "$@.d"
	$(COMPILE.cc) -g -D__TARGET_LINUX__ -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/_ext/5c0/network.o ../network.cpp

# Subprojects
.build-subprojects:

# Clean Targets
.clean-conf: ${CLEAN_SUBPROJECTS}
	${RM} -r ${CND_BUILDDIR}/${CND_CONF}

# Subprojects
.clean-subprojects:

# Enable dependency checking
.dep.inc: .depcheck-impl

include .dep.inc
