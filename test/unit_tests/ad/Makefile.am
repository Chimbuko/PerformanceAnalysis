AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/3rdparty @PS_FLAGS@
LDADD = $(top_builddir)/src/libchimbuko.la -lgtest -lstdc++fs

testdir = $(prefix)/test/unit_tests/ad
test_PROGRAMS = COPODOutlier COPODOutlierADs HBOSOutlier HBOSOutlierADs HBOSOutlierDistributions HBOSOutlierTestBPFile SSTDOutlierTestBPFile ADEvent ADio ADParser ADOutlier ADNetClient ADLocalFuncStatistics ADMetadataParser ADCounter ADLocalCounterStatistics ADAnomalyProvenance ADglobalFunctionIndexMap ADNormalEventProvenance utils AnomalyData ADcombinedPSdata FuncAnomalyMetrics ADLocalAnomalyMetrics

HBOSOutlier_SOURCES = HBOSOutlier.cpp ../unit_test_main_mpi.cpp
HBOSOutlier_LDADD = $(LDADD)

HBOSOutlierADs_SOURCES = HBOSOutlierADs.cpp ../unit_test_main_mpi.cpp
HBOSOutlierADs_LDADD = $(LDADD)

COPODOutlier_SOURCES = COPODOutlier.cpp ../unit_test_main_mpi.cpp
COPODOutlier_LDADD = $(LDADD)

COPODOutlierADs_SOURCES = COPODOutlierADs.cpp ../unit_test_main_mpi.cpp
COPODOutlierADs_LDADD = $(LDADD)

HBOSOutlierDistributions_SOURCES = HBOSOutlierDistributions.cpp ../unit_test_main_mpi.cpp
HBOSOutlierDistributions_LDADD = $(LDADD)

HBOSOutlierTestBPFile_SOURCES = HBOSOutlierTestBPFile.cpp ../unit_test_main_mpi.cpp
HBOSOutlierTestBPFile_LDADD = $(LDADD)

SSTDOutlierTestBPFile_SOURCES = SSTDOutlierTestBPFile.cpp ../unit_test_main_mpi.cpp
SSTDOutlierTestBPFile_LDADD = $(LDADD)

ADEvent_SOURCES = ADEvent.cpp ../unit_test_main.cpp
ADEvent_LDADD = $(LDADD)

ADio_SOURCES = ADio.cpp ../unit_test_main.cpp
ADio_LDADD = $(LDADD)

ADParser_SOURCES = ADParser.cpp ../unit_test_main_mpi.cpp
ADParser_LDADD = $(LDADD)

ADOutlier_SOURCES = ADOutlier.cpp ../unit_test_main_mpi.cpp
ADOutlier_LDADD = $(LDADD)

ADNetClient_SOURCES = ADNetClient.cpp ../unit_test_main_mpi.cpp
ADNetClient_LDADD = $(LDADD)

ADLocalFuncStatistics_SOURCES = ADLocalFuncStatistics.cpp ../unit_test_main_mpi.cpp
ADLocalFuncStatistics_LDADD = $(LDADD)

ADMetadataParser_SOURCES = ADMetadataParser.cpp ../unit_test_main.cpp
ADMetadataParser_LDADD = $(LDADD)

ADCounter_SOURCES = ADCounter.cpp ../unit_test_main.cpp
ADCounter_LDADD = $(LDADD)

ADLocalCounterStatistics_SOURCES = ADLocalCounterStatistics.cpp ../unit_test_main_mpi.cpp
ADLocalCounterStatistics_LDADD = $(LDADD)

ADAnomalyProvenance_SOURCES = ADAnomalyProvenance.cpp ../unit_test_main.cpp
ADAnomalyProvenance_LDADD = $(LDADD)

ADglobalFunctionIndexMap_SOURCES = ADglobalFunctionIndexMap.cpp ../unit_test_main_mpi.cpp
ADglobalFunctionIndexMap_LDADD = $(LDADD)

ADNormalEventProvenance_SOURCES = ADNormalEventProvenance.cpp ../unit_test_main.cpp
ADNormalEventProvenance_LDADD = $(LDADD)

utils_SOURCES = utils.cpp ../unit_test_main.cpp
utils_LDADD = $(LDADD)

AnomalyData_SOURCES = AnomalyData.cpp ../unit_test_main.cpp
AnomalyData_LDADD = $(LDADD)

ADcombinedPSdata_SOURCES = ADcombinedPSdata.cpp ../unit_test_main.cpp
ADcombinedPSdata_LDADD = $(LDADD)

FuncAnomalyMetrics_SOURCES = FuncAnomalyMetrics.cpp ../unit_test_main.cpp
FuncAnomalyMetrics_LDADD = $(LDADD)

ADLocalAnomalyMetrics_SOURCES = ADLocalAnomalyMetrics.cpp ../unit_test_main.cpp
ADLocalAnomalyMetrics_LDADD = $(LDADD)
