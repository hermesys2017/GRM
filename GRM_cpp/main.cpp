#include "stdafx.h"
#include "gentle.h"
#include "grm.h"
#include "realTime.h"
#include "grmapi.h"

#pragma comment(lib,"version.lib")

using namespace std;
namespace fs = std::filesystem;   

projectfilePathInfo ppi;
fs::path fpnLog;
projectFile prj;
grmOutFiles ofs;

domaininfo di;
int** cvais; // 각셀(x,y)의 cv array idex 
cvpos* cvps;//각 cv의 x, y 위치, ws 위치
cvAtt* cvs; // control volumes
cvAtt* cvsb; // t - dt 시간에 저장된 cvs 에서의 다양한 정보 백업
vector<weatherData> rfs; // 강우량 파일에서 읽은 자료
vector<weatherData> tempMax; // 최대기온 파일에서 읽은 자료
vector<weatherData> tempMin; // 최소기온 파일에서 읽은 자료
vector<weatherData> dayTimeLength; // 일조시간 파일에서 읽은 자료
vector<weatherData> snowpackTemp;// snow pack 온도 파일에서 읽은 자료
double sunDurRatio[12]; // 월별 일조시간 비율 파일에서 읽은 자료
vector<weatherData> solarRad; // 일사량 파일에서 읽은 자료
map<int, double[12]> laiRatio; // <LCvalue, 월별 laiRatio 12개>
double blaneyCriddleKData[12]; // Blaney Criddle K 파일에서 읽은 자료
map<int, int*> cvaisToFA; //fa별 cv array idex 목록
vector<int> fas; // 흐름누적수 종류별 하나씩 저장 (0,1,2,4,6,100 등), fa 값은 일련번호가 아니므로..
map<int, int> faCount; // 흐름누적수(int)별 셀 개수(int)
wpSimData wpSimValue; // wp별로 저장할 다양한 정보.  prj.wps 파일에서 읽은 순서대로 2차 정보 저장
flowControlCellAndData fccds; // gmp 파일에서 읽은 fc 정보를 이용해서 2차 정보 저장
thisSimulation ts; // 이번 simulation 프로세스에 과련된 다양한 변수

extern vector<flowControlinfo> fcinfos;
extern double** ssrAry;
extern double** rfAry;
extern double** rfaccAry;
extern double** QAry;

string msgFileProcess;

int main(int argc, char** args)
{
	string exeName = "GRM";
	version grmVersion = getCurrentFileVersion();
	string outString;
	outString = "GRM v." + to_string(grmVersion.pmajor) + "."
		+ to_string(grmVersion.pminor) + "."
		+ to_string(grmVersion.pbuild) + ". File version : "
		+ to_string(grmVersion.fmajor) + "."
		+ to_string(grmVersion.fminor) + "."
		+ to_string(grmVersion.fbuild) + ".\n";
	    //+"Modified in " + grmVersion.LastWrittenTime + ".\n";
	prj.cpusi = getCPUinfo();
	cout << outString;
	cout << prj.cpusi.infoString;
	outString = outString + prj.cpusi.infoString;
	ts.runByAnalyzer = -1;
	if (argc == 1) {
		printf("ERROR : GRM project file was not entered or invalid arguments.\n");
		grmHelp();
		return 1;
	}
	prj.deleteAllFilesExceptDischargeOut = -1;
	setlocale(LC_ALL, "korean");
	//prj.writeConsole = 1; // exe로 진입하는 것은 1
	prj.forSimulation = 1;// exe로 진입하는 것은 1, dll로 진입하는 것은 -1
	if (argc == 2) {
		string arg1(args[1]);
		string arg1L = lower(trim(arg1));
		if (arg1L == "/?" || arg1L == "/help") {
			grmHelp();
			return 1;
		}
		// 이경우는 grm.exe  fpn_gmp 인 경우
		startSingleRun(arg1, -1, outString);
	}
	else {
		string rtOption1(args[1]);
		string rtOption2(args[2]);
		rtOption1 = lower(trim(rtOption1));
		rtOption2 = lower(trim(rtOption2));
		int isRealTime = -1;
		if (rtOption1 == "/r" || rtOption2 == "/r") {
			// 실시간 수신자료 적용 옵션은 /r 
			// 최대 옵션은 아래와 같다. 
			// args[0] : grm.exe,               args[1] : /r 혹은 /a,   args[2] : /r 혹은 /a,          
			// args[3] : fpnRef,                 args[4] : strGUID,       args[5] : startCommandTime, 
			// args[6] : rtStartDataTime, agrs[7] : strMODEL
			isRealTime = 1;
			string arg1(args[1]); // /r 혹은 /a
			string arg2(args[2]); // /r 혹은 /a
			//string arg3(args[3]); // ref 파일 경로, 이름
			arg1 = lower(trim(arg1));
			arg2 = lower(trim(arg2));
			//arg3 = lower(trim(arg3));
			int isForceAutoROM = -1;
			if (arg1 == "/a" || arg2 == "/a") {
				isForceAutoROM = 1;
			}

			/// 현재는 강우-유출 사상만 실시간 모의가 가능하다. 연속형 모의는 실시간 자료 처리 적용 안되어 있음. 2023.03.06
			printf("WARNNING : The continuous simulation is not available for the real time simulation.\n");
			printf("WARNNING : In real time simulation, just the rainfall-runoff event simulation is possible.\n");

			if (grmRTLauncher(argc, args, isForceAutoROM) == -1) {
				return 1;
			}
		}

		if (isRealTime == -1 && argc == 3) {
			// 이경우는 /a, /f, /fd 중 하나의 옵션이 사용된 경우 
			string arg1(args[1]); // 이건 옵션
			string arg2(args[2]); // 이건 gmp 파일, 혹은 gmps 폴더
			arg1 = lower(trim(arg1));
			arg2 = trim(arg2);
			if (arg1 == "/a") {
				startSingleRun(arg2, 1, outString);
			}
			if (arg1 == "/f" || arg1 == "/fd") {
				struct stat finfo;
				if (stat(arg2.c_str(), &finfo) == 0) { //폴더가 있으면
					vector<string> gmpFiles;
					gmpFiles = getFileListInNaturalOrder(arg2, ".gmp");
					if (gmpFiles.size() == 0) {
						printf("ERROR : There is no GRM project file in this directory.\n");
						waitEnterKey();
						return -1;
					}
					prj.deleteAllFilesExceptDischargeOut = -1;
					if (arg1 == "/fd") {
						prj.deleteAllFilesExceptDischargeOut = 1;
					}
					startGMPsRun(gmpFiles, -1, outString);
					return 1;
				}
				else {
					printf("ERROR : Project folder is invalid!!\n");
					waitEnterKey();
					return -1;
				}
			}
		}

		if (isRealTime == -1 && argc == 4) {
			// 이경우는 /a, /f 혹은 /a, /fd 옵션이 사용된 경우 
			string arg1(args[1]); // /a, /f , /fd 옵션
			string arg2(args[2]);// /a, /f , /fd 옵션
			string arg3(args[3]);// gmps 폴더
			arg1 = lower(trim(arg1));
			arg2 = lower(trim(arg2));
			arg3 = trim(arg3);
			int using_A_switch = -1;
			if (arg1 == "/a" || arg2 == "/a") {
				using_A_switch = 1;
			}
			prj.deleteAllFilesExceptDischargeOut = -1;
			if (arg1 == "/fd" || arg2 == "/fd") {
				prj.deleteAllFilesExceptDischargeOut = 1;
			}
			struct stat finfo;
			if (stat(arg3.c_str(), &finfo) == 0) { //폴더가 있으면
				vector<string> gmpFiles;
				gmpFiles = getFileListInNaturalOrder(arg3, ".gmp");
				if (gmpFiles.size() == 0) {
					printf("ERROR : There is no GRM project file in this directory.\n");
					waitEnterKey();
					return -1;
				}
				startGMPsRun(gmpFiles, using_A_switch, outString);
				return 1;
			}
			else {
				printf("ERROR : Project folder is invalid!!\n");
				waitEnterKey();
				return -1;
			}
		}
	}
	disposeDynamicVars();
	return 1;
}

int startSingleRun(string fpnGMP, int enforceAutoROM, string outString)
{
	fs::path in_arg = fs::path(fpnGMP.c_str());
	string fp = in_arg.parent_path().string();
	if (trim(fp) == "") {
		string fpn_exe = getCurrentExeFilePathName();
		fs::path grmexef = fs::path(fpn_exe.c_str());
		string fp_exe = grmexef.parent_path().string();
		fpnGMP = fp_exe + "\\" + fpnGMP;
	}
	int nResult = _access(fpnGMP.c_str(), 0);
	if (nResult == -1
		|| lower(in_arg.extension().string()) != ".gmp") {
		cout<<"ERROR : GRM project file["+
			fpnGMP+"] is invalid.\n";
		waitEnterKey();
		return -1;
	}
	else if (nResult == 0) {
		ppi = getProjectFileInfo(fpnGMP);
		ts.enforceFCautoROM = enforceAutoROM;
		// 프로젝트 파일 여부 검증 후, fpnLog 파일 설정 후 writeNewLog 사용할 수 있다. 
		writeNewLog(fpnLog, outString, 1, -1); 
		if (setupAndStartSimulation() == -1) {
			waitEnterKey();
			return -1;
		}
	}
	return 1;
}

int startGMPsRun(vector<string> gmpFiles, int enforceAutoROM, string outString)
{
	clock_t  startT = clock();
	int nFiles = gmpFiles.size();
	ts.enforceFCautoROM = enforceAutoROM;
	for (int n = 0; n < nFiles; n++) {// /f 혹은 /fd 인 경우 여기서 실행
		ppi = getProjectFileInfo(gmpFiles[n]);
		// 프로젝트 파일 여부 검증 후, fpnLog 파일 설정 후 writeNewLog 사용할 수 있다. 
		writeNewLog(fpnLog, outString, 1, -1);
		string progF = to_string(n + 1) + '/' + to_string(gmpFiles.size());
		string progR = dtos(((n + 1) / double(nFiles) * 100.0), 2);
		msgFileProcess = "Total progress: " + progF + "(" + progR + "%). ";
		if (setupAndStartSimulation() == -1) {
			waitEnterKey();
			return -1;
		}
		if ((n + 1) % 100 == 0) {
			system("cls");
		}
		disposeDynamicVars();
	}
	if (prj.deleteAllFilesExceptDischargeOut != 1) {
		clock_t finishT = clock();
		long elapseT_sec = (long)(finishT - startT) / CLOCKS_PER_SEC;
		tm ts_total = secToHHMMSS(elapseT_sec);
		string endingStr = "Total simulation was completed. Run time : "
			+ to_string(ts_total.tm_hour) + "h "
			+ to_string(ts_total.tm_min) + "m "
			+ to_string(ts_total.tm_sec) + "s.\n";
		writeLog(fpnLog, endingStr, 1, 1);
	}
	return 1;
}

void disposeDynamicVars()
{
	if (cvais != NULL) {
		for (int i = 0; i < di.nCols; ++i) {
			if (cvais[i] != NULL) { delete[] cvais[i]; }
		}
		delete[] cvais;
	}
	if (cvps != NULL) { delete[] cvps; }
	if (cvs != NULL) { delete[] cvs; }
	if (cvsb != NULL) { delete[] cvsb; }

	if (cvaisToFA.size() > 0) {
		map<int, int*>::iterator iter;
		//map<int, int*> cvansTofa; //fa별 cvan 목록
		for (iter = cvaisToFA.begin(); iter != cvaisToFA.end(); ++iter) {
			if (cvaisToFA[iter->first] != NULL) {
				delete[] cvaisToFA[iter->first];
			}				
		}
	}

	if (ssrAry != NULL) {
		for (int i = 0; i < di.nCols; ++i) {
			if (ssrAry[i] != NULL) { delete[] ssrAry[i]; }
		}
		delete[] ssrAry;
	}
	if (rfAry != NULL) {
		for (int i = 0; i < di.nCols; ++i) {
			if (rfAry[i] != NULL) { delete[] rfAry[i]; }
		}
		delete[] rfAry;
	}
	if (rfaccAry != NULL) {
		for (int i = 0; i < di.nCols; ++i) {
			if (rfaccAry[i] != NULL) { delete[] rfaccAry[i]; }
		}
		delete[] rfaccAry;
	}
	if (QAry != NULL) {
		for (int i = 0; i < di.nCols; ++i) {
			if (QAry[i] != NULL) { delete[] QAry[i]; }
		}
		delete[] QAry;
	}
	prj.swps.clear();
	prj.css.clear();
	prj.fcs.clear();
	prj.wps.clear();
	prj.sts.clear();
	prj.sds.clear();
	prj.lcs.clear();

	fcinfos.clear();
	rfs.clear();
	tempMax.clear();
	tempMin.clear();
	dayTimeLength.clear();
	snowpackTemp.clear();
	solarRad.clear();
	laiRatio.clear();
	fas.clear();
	faCount.clear();
}

int setupAndStartSimulation()
{
	//===== 여기서 grmWSinfo class  test ===============
	
	//============ 이 부분은 gmp 파일로 인스턴싱 ====================
	//string fpn = "C://GRM//SampleGHG//GHG500.gmp";
	//string fpn = "D://Github//zTestSet_GRM_SampleWC_cpp//SampleProject.gmp";
	//string fpn = "D://Github//zTestSetPyGRM//Prj_v2022_cont_CJDsmall_pyTest//CJD_Prj_cont_10year_small_pyTest.gmp";
	//string fpn = "D://Github//zTestSetPyGRM//SampleWC//SampleWiCheon.gmp";
	//grmWSinfo gws = grmWSinfo(fpn);
	//============ 여기까지 gmp 파일로 인스턴싱 ====================

	//============ 이 부분은 입력 파일 개별 설정 인스턴싱 ====================
	//string fdType = "StartsFromE_TauDEM";
	//string fpn_domain = "D:/Github/zTestSet_GRM_SampleGHG_cpp/watershed/GHG_Watershed_c.asc";
	//string fpn_slope = "D:/Github/zTestSet_GRM_SampleGHG_cpp/watershed/GHG_Slope_ST.asc";
	//string fpn_fd = "D:/Github/zTestSet_GRM_SampleGHG_cpp/watershed/GHG_FDir.asc";
	//string fpn_fa = "D:/Github/zTestSet_GRM_SampleGHG_cpp/watershed/GHG_FAc.asc";
	//string fpn_stream = "D:/Github/zTestSet_GRM_SampleGHG_cpp/watershed/GHG_Stream.asc";
	//string fpn_lc = "D:/Github/zTestSet_GRM_SampleGHG_cpp/watershed/GHG_lc.asc";
	//string fpn_st = "D:/Github/zTestSet_GRM_SampleGHG_cpp/watershed/GHG_SoilTexture.asc";
	//string fpn_sd = "D:/Github/zTestSet_GRM_SampleGHG_cpp/watershed/GHG_SoilDepth.asc";

	////string fpn_domain = "D:/Github/zTestSet_GRM_SampleWC_cpp/Data/WiWatershed.asc";
	////string fpn_slope = "D:/Github/zTestSet_GRM_SampleWC_cpp/Data/Wi_Slope_ST.ASC";
	////string fpn_fd = "D:/Github/zTestSet_GRM_SampleWC_cpp/Data/WiFDir.ASC";
	////string fpn_fa = "D:/Github/zTestSet_GRM_SampleWC_cpp/Data/WiFAc.asc";
	////string fpn_stream = "D:/Github/zTestSet_GRM_SampleWC_cpp/Data/WiStream6.asc";
	////string fpn_lc = "D:/Github/zTestSet_GRM_SampleWC_cpp/Data/wilc200.asc";
	////string fpn_st = "D:/Github/zTestSet_GRM_SampleWC_cpp/Data/wistext200.asc";
	////string fpn_sd = "D:/Github/zTestSet_GRM_SampleWC_cpp/Data/wisdepth200.asc";

	//string fdType = "StartsFromE_TauDEM";
	//string fpn_domain = "D:/GRM_ex/Nakdong/watershed/ND_Watershed.asc";
	//string fpn_slope = "D:/GRM_ex/Nakdong/watershed/ND_Slope.asc";
	//string fpn_fd = "D:/GRM_ex/Nakdong/watershed/ND_Fdr.asc";
	//string fpn_fa = "D:/GRM_ex/Nakdong/watershed/ND_Fac.asc";
	//string fpn_stream = "D:/GRM_ex/Nakdong/watershed/ND_Stream.asc";
	//string fpn_lc = "D:/GRM_ex/Nakdong/watershed/ND_LC.asc";
	//string fpn_st = "D:/GRM_ex/Nakdong/watershed/ND_STexture.asc";
	//string fpn_sd = "D:/GRM_ex/Nakdong/watershed/ND_SDepth.asc";

	//grmWSinfo	gws = grmWSinfo(fdType, fpn_domain,	fpn_slope, fpn_fd,
	//	fpn_fa, fpn_stream, fpn_lc, fpn_st, fpn_sd);
	//============ 여기까지 입력 파일 개별 설정 인스턴싱 ====================

	//============= grmWSinfo에서 정보 얻기..
	//vector <int> mdwsidsv = gws.mostDownStreamWSIDs;
	//swsParameters swsp = gws.subwatershedPars(1);
	//int upWSCount = gws.upStreamWSCount(1);
	//===================================================

	
	if (openPrjAndSetupModel(-1) == -1) {
		writeLog(fpnLog, "ERROR : Model setup failed !!!\n", 1, 1);
		if (prj.forSimulation == 1) {// exe로 진입하는 것은 1, dll로 진입하는 것은 -1
			return -1;
		}
	}
	writeLog(fpnLog, "Simulation was started.\n", 1, 1);
	if (startSimulation() == -1) {
		writeNewLog(fpnLog, "ERROR : An error was occurred while simulation...\n", 1, 1);
		return -1;
	}
	if (prj.deleteAllFilesExceptDischargeOut == 1) {
		if (deleteAllFilesExceptDischarge() == -1) {
			writeNewLog(fpnLog, "ERROR : An error was occurred while deleting all files except discharge.out.\n", 1, 1);
			return -1;
		}
	}
	return 1;
}

int openPrjAndSetupModel(int forceRealTime) // 1:true, -1:false
{	
	writeLog(fpnLog, "Checking the input files... \n", 1, 1);
	if (openProjectFile(forceRealTime) < 0)	{
		writeLog(fpnLog, "ERROR : Open "+ ppi.fpn_prj+" was failed.\n", 1, 1);
		if (prj.forSimulation == 1) {// exe로 진입하는 것은 1, dll로 진입하는 것은 -1
			return -1;
		}
	}
	//cout << "completed. \n";
	writeLog(fpnLog, "Checking input files completed.\n", 1, 1);
	writeLog(fpnLog, ppi.fpn_prj+" project was opened.\n", 1, 1);
	//cout << "Setting up input data... ";
	writeLog(fpnLog, "Setting up input data...\n", 1, 1);
	if (setupModelAfterOpenProjectFile() == -1) {		
		if (prj.forSimulation == 1) { return -1; }// exe로 진입하는 것은 1, dll로 진입하는 것은 -1
	}
	//cout << "completed. \n";
	writeLog(fpnLog, "Input data setup was completed.\n", 1, 1);
	string isparallel = "true";
	omp_set_num_threads(prj.mdp);

	//todo : 여기에 셀 개수 조건으로 mdp 설정 추가 가능
	//if (di.cellNnotNull < 12000) {
	//	writeLog(fpnLog, "The number of effective cell [ "
	//		+ to_string(di.cellNnotNull) + "] is smaller than 12,000.\n"
	//		+ "It was converted to serial calculation. \n", 1, 1);
	//	prj.mdp = 1;
	//	isparallel = "false";
	//}
	if (prj.forSimulation == 1) {// exe로 진입하는 것은 1, dll로 진입하는 것은 -1
		if (initOutputFiles() == -1) {
			writeLog(fpnLog, "ERROR : Initializing output files was failed.\n", 1, 1);
			return -1;
		}
	}
	writeLog(fpnLog, ppi.fpn_prj+" -> Model setup was completed.\n", 1, 1);
	writeLog(fpnLog, "The number of effecitve cells : " + to_string(di.cellNnotNull) + "\n", 1, 1);
	if (prj.mdp == 1) { isparallel = "false"; }
	writeLog(fpnLog, "Parallel : " + isparallel + ". Max. degree of parallelism : "
		+ to_string(prj.mdp) + ".\n", 1,1);
	return 1;
}


void grmHelp() // /r, /a 설명 추가
{
	printf("\n");
	printf(" Usage : GRM [Current project file full path and name to simulate]\n");
	printf("\n");
	printf("** 사용법 (How to use in Korean)\n");
	printf("  1. GRM 모형의 project 파일(.gmp)과 입력자료(지형공간자료, 강우, flow control 시계열 자료)를 준비한다. \n");
	printf("  2. 모델링할 프로젝트 이름을 스위치(argument)로 넣고 실행한다.\n");
	printf("      - Console에서 grm.exe [argument] 로 실행한다.\n");
	//printf("      ** 주의사항 : 장기간 모의할 경우, 컴퓨터 업데이트로 인해, 종료될 수 있으니, \n");
	//printf("                       네트워크를 차단하고, 자동업데이트 하지 않음으로 설정한다.\n");
	printf("  3. argument\n");
	printf("      - /?\n");
	printf("          도움말\n");
	printf("      - 프로젝트 파일경로와 이름\n");
	printf("         -- GRM 모델을 파일단위로 실행시킨다.\n");
	printf("         -- 이때 full path, name을 넣어야 하지만, \n");
	printf("             프로젝트 파일이 GRM.exe 파일과 동일한 폴더에 있을 경우에는,\n");
	printf("             file path는 입력하지 않아도 된다.\n");
	printf("         -- 프로젝트 이름과 경로에 공백이 포함될 경우 큰따옴표로 묶어서 입력한다.\n");
	printf("         -- 예문(grm.exe가 d:/GRMrun에 있을 경우)\n");
	printf("           --- Case1. grm.exe와 다른 폴더에 프로젝트 파일이 있을 경우\n");
	printf("               D:/GRMrun>grm D:/GRMTest/TestProject/test.gmp\n");
	printf("               D:/GRMrun>grm \"D:/GRMTest/Test Project/test.gmp\"\n");
	printf("           --- Case 2. grm.exe와 같은 폴더에 프로젝트 파일이 있을 경우\n");
	printf("               D:/GRMrun>grm test.gmp\n");
	printf("      - /f 폴더경로\n");
	printf("         -- grm을 폴더 단위로 실행시킨다.\n");
	printf("         -- 예문 : D:/GRMrun>grm /f d:/GRMrun/TestProject\n");
	printf("      - /fd 폴더경로\n");
	printf("         -- grm을 폴더 단위로 실행시킨다.\n");
	printf("         -- 유량 모의결과인 *discharge.out을 제외한 모든 파일을 지운다(*.gmp, *Depth.out, 등)\n");
	printf("         -- 예문 : D:/GRMrun>grm /fd d:/GRMrun/TestProject\n");
	printf("      - /r REF 파일경로와 이름\n");
	printf("         -- 실시간 유출해석을 시작한다.\n");
	printf("         -- 예문 : D:/GRMrun>grm /r D:/GRMTest/TestProject/test.ref\n");
	printf("      - /a 프로젝트 파일(혹은 REF 파일) 경로와 이름\n");
	printf("         -- 저수지 운영시 입력된 자료가 없거나, 입력된 자료를 모두 사용하면, AutoROM을 적용한다.\n");
	printf("         -- 예문 : D:/GRMrun>grm /a D:/GRMTest/TestProject/test.gmp\n");
	printf("         -- 예문 : D:/GRMrun>grm /r /a D:/GRMTest/TestProject/test.ref\n");
	printf("\n");
	printf("** Usage\n");
	printf("  1. Make a gmp file by using a text editor or the QGIS-GRM.\n");
	printf("  2. In the console window, the gmp file is entered as the argument to run GRM.exe.\n");
	printf("      - In Console : grm.exe [argument]\n");
	printf("  3. argument\n");
	printf("      - /?\n");
	printf("          Help\n");
	printf("      - gmp file path and name\n");
	printf("         -- Run the GRM model for a single gmp file.\n");
	printf("         -- When the GRM.exe and gmp files are in the same folder,\n");
	printf("             the project file path does not have to be entered.\n");
	printf("         -- If there are spaces in the project file name or path, double quotation marks (\" \") have to be used to enclose it for input.\n");
	printf("         -- Example(when grm.exe is in 'd:/GRMrun' folder)\n");
	printf("           --- Case1. gmp file is in different folder with grm.exe\n");
	printf("               D:/GRMrun>grm D:/GRMTest/TestProject/test.gmp\n");
	printf("               D:/GRMrun>grm \"D:/GRMTest/Test Project/test.gmp\"\n");
	printf("           --- Case 2. gmp file is in the same folder with grm.exe\n");
	printf("               D:/GRMrun>grm test.gmp\n");
	printf("      - /f folder path\n");
	printf("         -- The GRM can run at once for all gmp files in the corresponding folder.\n");
	printf("         -- Example : D:/GRMrun>grm /f d:/GRMrun/TestProject\n");
	printf("      - /fd folder path\n");
	printf("         -- The GRM can run at once for all gmp files in the corresponding folder.\n");
	printf("         -- And all files, except for the discharge file (*discharge.out), are deleted (*.gmp, *Depth.out, etc.).\n");
	printf("         -- Example : D:/GRMrun>grm /fd d:/GRMrun/TestProject\n");
	printf("      - /r ref file path and name \n");
	printf("         -- Start real time simulation.\n");
	printf("         -- Example : D:/GRMrun>grm /r D:/GRMTest/TestProject/test.ref\n");
	printf("      - /a gmp file (or ref file) path and name\n");
	printf("         -- Apply AutoROM in case of no reservoir flow data or after using all the data entered for reservoir operation.\n");
	printf("         -- Example : D:/GRMrun>grm /a D:/GRMTest/TestProject/test.gmp\n");
	printf("         -- Example : D:/GRMrun>grm /r /a D:/GRMTest/TestProject/test.ref\n");
}
