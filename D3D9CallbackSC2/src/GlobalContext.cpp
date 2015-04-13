#include "Main.h"
#include "cachemap.h"
#include <stdint.h>
#include <sstream>
#include <boost\filesystem.hpp>
#include <Windows.h>

#include <unordered_set>

#define DEBUG 0
#define ATTACH_DEBUGGER 1
#define GCONTEXT_DEBUG 0
#define CREATE_TEXTURE_DEBUG 0

#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
string gcontext_debug_file = "tonberry\\debug\\global_context.log";
#endif

#if DEBUG
double PCFreq = 0.0;
__int64 CounterStart = 0;
string debug_file = "tonberry\\tests\\timing.csv";
ofstream debug(debug_file, ofstream::out | ofstream::app);

void StartCounter()
{
    LARGE_INTEGER li;
    if(!QueryPerformanceFrequency(&li))
	cout << "QueryPerformanceFrequency failed!\n";

    PCFreq = double(li.QuadPart)/1000000.0;

    QueryPerformanceCounter(&li);
    CounterStart = li.QuadPart;
}

double GetCounter()
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return double(li.QuadPart-CounterStart)/PCFreq;
}
#endif
/*********************************
*								 *
*$(SolutionDir)$(Configuration)\ *
*								 *
*********************************/
//-------------
/*#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>*/
//-------------

#ifndef ULTRA_FAST
bool g_ReportingEvents = false;
#endif

namespace fs = boost::filesystem;

GlobalContext *g_Context;

typedef enum {
	MATCH		= 0,
	NOMATCH		= 1,
	COLLISION	= 2
}Matchtype;

void GraphicsInfo::Init()
{
    _Device = NULL;
    _Overlay = NULL;
}

void GraphicsInfo::SetDevice(LPDIRECT3DDEVICE9 Device)
{
    Assert(Device != NULL, "Device == NULL");
    D3D9Base::IDirect3DSwapChain9* pSwapChain;    
    HRESULT hr = Device->GetSwapChain(0, &pSwapChain);
    Assert(SUCCEEDED(hr), "GetSwapChain failed");    
    hr = pSwapChain->GetPresentParameters(&_PresentParameters);
    Assert(SUCCEEDED(hr), "GetPresentParameters failed");    
    pSwapChain->Release();
    hr = Device->GetCreationParameters(&_CreationParameters);
    Assert(SUCCEEDED(hr), "GetCreationParameters failed");
    _Device = Device;
}

template <typename T>
T ToNumber(const std::string& Str)	//convert string to unsigned long long -> uint64_t
{
    T Number;
    std::stringstream S(Str);
	S >> Number;
    return Number;
}

struct BigInteger_Hash
{
	size_t operator()(const BigInteger& b) const
	{
		return std::hash<string>()(b.getNumber());
	}
};

//Debug control variable
bool debugmode = false;

//Global Variables
//vector<int> pixval;
int pixval[64];
//vector<int> pixval2;
int pixval2[98];
//vector<int> objval;
int objval[64];
unordered_map<uint64_t, string> hashmap;	//Hashmap of unique hashvals 
unordered_map<uint64_t, string> collmap;	//Hashmap of collision duplicates
unordered_map<BigInteger, uint64_t, BigInteger_Hash> hash2map;			//Map of hashval2 -> StringToUint64(hashval2)
unordered_map<string, string> coll2map;
unordered_map<uint64_t, string> objmap;
unordered_map<uint64_t, string>::iterator it;
unordered_map<string, string>::iterator it2;
unordered_set<uint64_t> persistent_hashes;	// holds hashes that should persist in the cache
unordered_set<uint64_t> sysfld_hashes;		// holds sysfld hashes
unordered_set<uint64_t> iconfl_hashes;		// holds iconfl hashes
uint64_t hashval; //current hashval of left half of memory
uint64_t objtop; //object in top left corner of memory
uint64_t objbot; //object in bottom left corner of memory
BigInteger hashval2; //hashval for algo 2

float resize_factor;
string texdir("");

//TextureCache
size_t cache_size = 250;
TextureCache * texcache;

void initCache(){
	texcache = new TextureCache(cache_size);
}

void loadprefs ()
{
	resize_factor = 4.0;
	ifstream prefsfile;
	prefsfile.open ("tonberry\\prefs.txt", ifstream::in);
	if (prefsfile.is_open()){
		string line;
		while ( getline(prefsfile, line) ){
			if (line.empty() || line[0] == '#') continue;
			size_t eq = line.find("=");
			if (eq != string::npos) {
				string param = line.substr(0, eq);
				if (param == "resize_factor")
					resize_factor = (float)atoi(line.substr(eq + 1, line.length()).c_str()); //make sure no spaces in prefs file
				else if (param == "debug_mode")
					debugmode = (line.substr(eq + 1, line.length()) == string("yes"));
				else if (param == "cache_size")
					cache_size = (unsigned)atoi(line.substr(eq + 1, line.length()).c_str());
				else if (param == "texture_dir") {
					texdir = line.substr(eq + 1, line.length());
					if (texdir.back() != '/') texdir += "/";
				}
			}
			//if (line.substr(0) == string("#"));
		}
		prefsfile.close();
	}

	//cache_size = max(cache_size, 100U);
	cache_size = min(cache_size, 800U);

#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
	ofstream debug(gcontext_debug_file, fstream::out | fstream::app);
	debug << endl;
	debug << "Preferences:" << endl;
	debug << "  resize_factor = " << resize_factor << endl;
	debug << "  debugmode = " << ((debugmode) ? "yes" : "no") << endl;
	debug << "  cache_size = " << cache_size << endl;
	debug << "  texdir = " << texdir << endl;
	debug.close();
#endif
}

//Mod Jay
void loadhashfile ()	//Expects hash1map folder to be in ff8/tonberry directory
{
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
	ofstream debug(gcontext_debug_file, fstream::out | fstream::app);
	debug << endl;
	debug << "Hashmap:" << endl;
#endif

	fs::path hashpath("tonberry/hashmap");
	if (!fs::exists(hashpath)) {
		ofstream err;								//Error reporting file
		err.open("tonberry/error.txt", ofstream::out | ofstream::app);
		err << "Error: hashmap folder doesn't exist\n";
		err.close();
	} else {
		fs::directory_iterator end_it;				//get tonberry/hashmap folder iterator


		for (fs::directory_iterator it(hashpath); it != end_it; it++) {
			if (fs::is_regular_file(it->status())) {	//if we got a regular file
				if (it->path().extension() == ".csv") {	//we check its extension, if .csv file:
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
					debug << "  Reading " << it->path().filename().string() << "...";
					debug.flush();
					size_t size_before = hashmap.size();
#endif

					ifstream hashfile;
					hashfile.open(it->path().string(), ifstream::in);	//open it and dump into the map
					string line;
					if (hashfile.is_open()) {
						while (getline(hashfile, line)) //Omzy's original code
						{
							if (line.empty()) continue;
							if (line[0] == '*') continue;						// textures starting with * should be ignored

							bool persist = false;
							if (line[0] == '!') {								// textures starting with ! should be persistent
								persist = true;
								line = line.substr(1);
							}

							int comma = line.find(",");
							string field = line.substr(0, comma);
							string valuestr = line.substr(comma + 1, line.length()).c_str();
							uint64_t value = ToNumber<uint64_t>(valuestr);
							hashmap.insert(pair<uint64_t, string>(value, field)); //key, value for unique names, value, key for unique hashvals

							if (persist) persistent_hashes.insert(value);

							if (field.length() >= 6) {
								string chars = field.substr(0, 6);
								if (chars == "sysfld") { //Exception for sysfld00 and sysfld01
									sysfld_hashes.insert(value);
								} else if (chars == "iconfl") { //Exception for iconfl00, iconfl01, iconfl02, iconfl03, iconflmaster
									iconfl_hashes.insert(value);
								}
							}
						}
						hashfile.close();
					}

#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
					debug << " found " << (hashmap.size() - size_before) << " hashes." << endl;
#endif
				}
			}
		}
	}

#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
	debug << "  hashmap.size() = " << hashmap.size() << endl;
	debug << "  sysfld_hashes.size() = " << sysfld_hashes.size() << ":" << endl;
	for (uint64_t sysfld_hash : sysfld_hashes)
		debug << "    " << sysfld_hash << endl;
	debug << "  iconfl_hashes.size() = " << iconfl_hashes.size() << ":" << endl;
	for (uint64_t iconfl_hash : iconfl_hashes)
		debug << "    " << iconfl_hash << endl;
	debug.close();
#endif
}

void loadcollfile ()	//Expects collisions.csv to be in ff8/tonberry directory
{
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
	ofstream debug(gcontext_debug_file, fstream::out | fstream::app);
	debug << endl;
	debug << "Collisions:" << endl;
#endif

	ifstream collfile;
	collfile.open ("tonberry\\collisions.csv", ifstream::in);
	string line;
	if (collfile.is_open())
	{
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
		debug << "  Reading collisions.csv...";
		debug.flush();
#endif
		while ( getline(collfile, line) ) //~10000 total number of 128x256 texture blocks in ff8
		{
			int comma = line.find(",");
			string field = line.substr(0, comma);
			string valuestr = line.substr(comma + 1, line.length()).c_str();
			uint64_t value = ToNumber<uint64_t>(valuestr);
			collmap.insert(pair<uint64_t, string>(value, field)); //key, value for unique names, value, key for unique hashvals
		}
		collfile.close();
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
		debug << " found " << collmap.size() << " hashes." << endl;
#endif
	}

#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
	debug.close();
#endif
}

//-------------------------------------------------------------
// converts string to unsigned long long
uint64_t StringToUint64(string s)
{
	uint64_t sum = 0;

	for (int i = 0; i<s.length(); i++)
		sum = (sum * 10) + (s[i] - '0');

	return sum;
}

void loadcoll2file ()	//Expects hash2map.csv to be in ff8/tonberry directory
{
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
	ofstream debug(gcontext_debug_file, fstream::out | fstream::app);
	debug << endl;
	debug << "Hash2:" << endl;
#endif

	ifstream coll2file;
	coll2file.open ("tonberry\\hash2map.csv", ifstream::in);
	string line;
	if (coll2file.is_open())
	{
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
		debug << "  Reading hash2map.csv...";
		debug.flush();
#endif
		while ( getline(coll2file, line) ) //~10000 total number of 128x256 texture blocks in ff8
		{
			if (line.empty()) continue;
			if (line[0] == '*') continue;						// textures starting with * should be ignored

			bool persist = false;
			if (line[0] == '!') {								// textures starting with ! should be persistent
				persist = true;
				line = line.substr(1);
			}

			int comma = line.find(",");
			string field = line.substr(0, comma);
			string valuestr = line.substr(comma + 1, line.length()).c_str();
			//BigInteger value = BigInteger(valuestr);
			//value = ToNumber<BigInt>(valuestr);
			uint64_t value = StringToUint64(valuestr);
			hash2map.insert(pair<BigInteger, uint64_t>(valuestr, value));
			coll2map.insert(pair<string, string>(valuestr, field)); //key, value for unique names, value, key for unique hashvals

			if (persist) persistent_hashes.insert(value);
		}
		coll2file.close();
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
		debug << " found " << hash2map.size() << " hashes." << endl;
#endif
	}

#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
	debug.close();
#endif
}

//Mod Jay
void loadobjfile ()	//Expects objmap.csv to be in ff8/tonberry directory
{
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
	ofstream debug(gcontext_debug_file, fstream::out | fstream::app);
	debug << endl;
	debug << "Objects:" << endl;
#endif
	fs::path hashpath("tonberry/objmap");
	if (!fs::exists(hashpath)) {
		ofstream err;								//Error reporting file
		err.open("tonberry/error.txt", ofstream::out | ofstream::app);
		err << "Error: objmap folder doesn't exist\n";
		err.close();
	} else {
		fs::directory_iterator end_it;
		for (fs::directory_iterator it(hashpath); it != end_it; it++) {
			if (fs::is_regular_file(it->status())) {	//if we got a regular file
				if (it->path().extension() == ".csv") {	//we check its extension, if .csv file:
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
					debug << "  Reading " << it->path().filename().string() << "...";
					debug.flush();
					size_t size_before = objmap.size();
#endif

					ifstream objfile;
					objfile.open(it->path().string(), ifstream::in);
					string line;
					if (objfile.is_open()) {
						while (getline(objfile, line)) {
							if (line.empty()) continue;
							if (line[0] == '*') continue;						// textures starting with * should be ignored

							bool persist = false;
							if (line[0] == '!') {								// textures starting with ! should be persistent
								persist = true;
								line = line.substr(1);
							}

							int comma = line.find(",");
							string obj = line.substr(0, comma);
							string valuestr = line.substr(comma + 1, line.length()).c_str();
							uint64_t value = ToNumber<uint64_t>(valuestr);
							objmap.insert(pair<uint64_t, string>(value, obj)); //key, value for unique names, value, key for unique hashvals

							if (persist) persistent_hashes.insert(value);
						}
						objfile.close();
					}

#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
					debug << " found " << (objmap.size() - size_before) << " hashes." << endl;
#endif
				}
			}
		}
	}

#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
	debug << "  objmap.size() = " << objmap.size() << endl;
	debug.close();
#endif
}

int m;

void GlobalContext::Init ()
{
	//------------
	/*_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
	_CrtSetReportMode( _CRT_ERROR, _CRTDBG_MODE_FILE );
	HANDLE hLogFile;
	hLogFile = CreateFile("c:\\log.txt", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	_CrtSetReportFile( _CRT_ERROR, hLogFile );
	_RPT0(_CRT_ERROR,"file message\n");
	CloseHandle(hLogFile);*/
	//------------
#if ATTACH_DEBUGGER
	::MessageBox(NULL, "Attach debugger now", "ATTACH", MB_OK);
#endif

#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
	ofstream debug(gcontext_debug_file, fstream::out | fstream::trunc);
	debug << "Initialized" << endl;
	debug.close();
#endif

    Graphics.Init();
	loadprefs();
	loadhashfile();
	loadcollfile();
	loadcoll2file();
	loadobjfile();
	initCache();//has to be called after loadprefs() so we get the propper cache_size!

	m = 0; // debug
}

void Hash_Algorithm_1 (BYTE* pData, UINT pitch, int width, int height)	//hash algorithm that preferences top and left sides
{
	int blocksize = 16;
	UINT x, y;
	int pix = 0;
	int toppix = 0;
	int botpix = 0;
	for (x = 0; x < 8; x++) //pixvals 0->31
	{
		for (y = 0; y < 4; y++)
		{
			if (x*blocksize < width && y*blocksize < height) //respect texture sizes
			{
				RGBColor* CurRow = (RGBColor*)(pData + (y*blocksize) * pitch);
				RGBColor Color = CurRow[x*blocksize];
				pixval[pix] = (Color.r + Color.g + Color.b) / 3;
				if (y*blocksize < 128) { if (toppix < 32) { objval[toppix] = pixval[pix]; toppix++; } }
				else if (botpix < 32) { objval[botpix + 32] = pixval[pix]; botpix++; }
			} else { pixval[pix] = 0; } //out of bounds
			pix++;
		}				
	}
	for (x = 0; x < 2; x++) //pixvals 32->55
	{
		for (y = 4; y < 16; y++)
		{
			if (x*blocksize < width && y*blocksize < height) //respect texture sizes
			{
				RGBColor* CurRow = (RGBColor*)(pData + (y*blocksize) * pitch);
				RGBColor Color = CurRow[x*blocksize];
				pixval[pix] = (Color.r + Color.g + Color.b) / 3;
				if (y*blocksize < 128) { if (toppix < 32) { objval[toppix] = pixval[pix]; toppix++; } }
				else if (botpix < 32) { objval[botpix + 32] = pixval[pix]; botpix++; }
			} else { pixval[pix] = 0; } //out of bounds
			pix++;
		}				
	}
	for (x = 3; x < 7; x+=3) //pixvals 56->63, note +=3
	{
		for (y = 5; y < 15; y+=3)	//note +=3
		{
			if (x*blocksize < width && y*blocksize < height) //respect texture sizes
			{
				RGBColor* CurRow = (RGBColor*)(pData + (y*blocksize) * pitch);
				RGBColor Color = CurRow[x*blocksize];
				pixval[pix] = (Color.r + Color.g + Color.b) / 3;
				if (y*blocksize < 128) { if (toppix < 32) { objval[toppix] = pixval[pix]; toppix++; } }
				else if (botpix < 32) { objval[botpix + 32] = pixval[pix]; botpix++; }
			} else { pixval[pix] = 0; } //out of bounds
			pix++;
		}				
	}
}

void Hash_Algorithm_2 (BYTE* pData, UINT pitch, int width, int height)	//hash algorithm that chooses unique pixels selected by hand
{
	int pix = 0;
	UINT x0, y0;
	UINT x[76] = {44, 0, 17, 25, 15, 111, 35, 25, 3, 46, 112, 34, 21, 1, 72, 80, 25, 32, 15, 4, 123, 16, 47, 14, 110, 78, 3, 66, 0, 86, 58, 27, 39, 4, 6, 49, 7, 71, 121, 17, 22, 16, 84, 115, 118, 119, 126, 59, 96, 88, 64, 1, 21, 31, 107, 92, 73, 116, 118, 58, 47, 18, 93, 78, 97, 106, 107, 77, 99, 13, 100, 125, 12, 33, 53, 61};
	UINT y[76] = {243, 0, 2, 19, 35, 24, 0, 12, 23, 7, 5, 0, 4, 0, 2, 218, 30, 2, 20, 23, 4, 4, 2, 8, 7, 7, 25, 0, 1, 0, 11, 15, 2, 0, 0, 1, 15, 15, 16, 7, 7, 0, 244, 245, 245, 245, 253, 203, 135, 184, 9, 15, 80, 81, 244, 245, 249, 255, 238, 237, 216, 218, 240, 216, 116, 164, 244, 247, 236, 245, 21, 59, 25, 8, 16, 108};
	for (int i = 0; i < 76; i++) //pixvals 0->75
	{
		if (x[i] < width && y[i] < height) //respect texture sizes
		{
			RGBColor* CurRow = (RGBColor*)(pData + (y[i]/*blocksize*/) * pitch); //blocksize already included
			RGBColor Color = CurRow[x[i]/*blocksize*/];
			pixval2[pix] = (Color.r + Color.g + Color.b) / 3;
		} else { pixval2[pix] = 0; } //out of bounds
		pix++;
	}

	for (x0 = 0; x0 < 44; x0+=4) //pixvals 76->97, note +=4
	{
		for (y0 = 7; y0 < 16; y0+=8) //note +=8
		{
			if (x0 < width && y0 < height) //respect texture sizes
			{
				RGBColor* CurRow = (RGBColor*)(pData + (y0/*blocksize*/) * pitch); //blocksize already included
				RGBColor Color = CurRow[x0/*blocksize*/];
				pixval2[pix] = (Color.r + Color.g + Color.b) / 3;
			} else { pixval2[pix] = 0; } //out of bounds
			pix++;
		}				
	}
}

Matchtype getsysfld (BYTE* pData, UINT pitch, int width, int height, string & sysfld)	//Exception method for sysfld00 and sysfld01
{
	UINT x = 177;
	UINT y = 155;

	if (width <= x || height <= y) {
		return Matchtype::NOMATCH;
	}

	string tempstr = sysfld;
	RGBColor* CurRow = (RGBColor*)(pData + (y) * pitch);
	RGBColor Color = CurRow[x];
	int sysval = (Color.r + Color.g + Color.b) / 3;
	string syspage = "13";
	switch (sysval) {
		case 43: syspage = "13"; break;
		case 153: syspage = "14"; break;
		case 150: syspage = "15"; break;
		case 101: syspage = "16"; break;
		case 85: syspage = "17"; break;
		case 174: syspage = "18"; break;
		case 170: syspage = "19"; break;
		case 255: syspage = "20"; break;
		default: syspage = "13"; break;
	}
	sysfld = tempstr.substr(0, 9) + syspage;
	return Matchtype::MATCH;
}

Matchtype geticonfl (BYTE* pData, UINT pitch, int width, int height, string & iconfl)	//Exception method for iconfl00, iconfl01, iconfl02, iconfl03, iconflmaster
{
	UINT x = 0;
	UINT y = 0;
	string tempstr = iconfl;
	if (iconfl == "iconfl00_13") { x = 82; y = 150; }
	else if (iconfl == "iconfl01_13") { x = 175; y = 208; }
	else if (iconfl == "iconfl02_13") { x = 216; y = 108; }
	else if (iconfl == "iconfl03_13") { x = 58; y = 76; }
	else if (iconfl == "iconflmaster_13") { x = 215; y = 103; }

	if (width <= x || height <= y) {
		return Matchtype::NOMATCH;
	}

	RGBColor* CurRow = (RGBColor*)(pData + (y) * pitch);
	RGBColor Color = CurRow[x];
	int colR = Color.r;
	int colG = Color.g;
	int colB = Color.b;
	if (colR == 0) { colR++; }
	if (colG == 0) { colG++; }
	if (colB == 0) { colB++; }
	int icval = colR * colG * colB;

	string icpage = "13";
	switch (icval) {
		case 65025: icpage = "13"; break;
		case 605160: icpage = "14"; break;
		case 1191016: icpage = "15"; break;
		case 189: icpage = "16"; break;
		case 473304: icpage = "17"; break;
		case 20992: icpage = "18"; break;
		case 859625: icpage = "19"; break;
		case 551368: icpage = "20"; break;
		case 1393200: icpage = "21"; break;
		case 931500: icpage = "22"; break;
		case 1011240: icpage = "23"; break;
		case 1395640: icpage = "24"; break;
		case 1018024: icpage = "25"; break;
		case 411864: icpage = "26"; break;
		case 80064: icpage = "27"; break;
		case 4410944: icpage = "28"; break;
		default: icpage = "13"; break;
	}
	iconfl = tempstr.substr(0, iconfl.size()-2) + icpage;
	return Matchtype::MATCH;
}

Matchtype getobj (uint64_t & hash, string & texname) //if previously unmatched, searches through object map for objects in top left/bottom left memory quarters, finally NO_MATCH is returned
{
	objtop = 0;
	objbot = 0;
	int lastpixel = objval[63];
    for (int i = 0; i < 64; i++)
    {
		if (i < 32) { objtop *= 2; }
		else { objbot *= 2; }
		if ((objval[i] - lastpixel) >= 0)
		{
			if (i < 32) { objtop++; }
			else { objbot++; }
		}
		lastpixel = objval[i];
    }
	it = objmap.find(objtop);
	if (it != objmap.end()) { 
		hash = objtop;
		texname = it->second; 
		return Matchtype::MATCH ;
	}
	it = objmap.find(objbot);
	if (it != objmap.end()) { 
		hash = objbot;
		texname = it->second; 
		return Matchtype::MATCH;
	}
	hash = 0;
	return Matchtype::NOMATCH;
}

Matchtype getfield (uint64_t & hash, string & texname)	//simple sequential bit comparisons
{
	ofstream checkfile;
	//checkfile.open("tonberry/tests/hashcache_test.txt", ofstream::out| ofstream::app);
	hashval = 0;
	int lastpixel = pixval[63];
    for (int i = 0; i < 64; i++)
    {
        hashval *= 2;
		if ((pixval[i] - lastpixel) >= 0) {	hashval++; }
		lastpixel = pixval[i];
    }

	it = hashmap.find(hashval);
	if (it != hashmap.end()) { 
		hash = hashval;
		texname = it->second; 
		return Matchtype::MATCH;
	}
	else {
		it = collmap.find(hashval);
		if (it != collmap.end()) {
			hash = hashval;
			return Matchtype::COLLISION; 
		}
	}

	return getobj(hash, texname);
}

Matchtype getfield2 (uint64_t & hash, string & texname)	//simple sequential bit comparisons, algorithm 2
{
	hashval2 = 0;
	int lastpixel = pixval2[97];
    for (int i = 0; i < 98; i++)
    {
        hashval2 *= 2;
		if ((pixval2[i] - lastpixel) >= 0) {	hashval2 += 1; }
		lastpixel = pixval2[i];
    }
	it2 = coll2map.find(hashval2.getNumber());
	if (it2 != coll2map.end()) {
		texname = it2->second; 
		hash = hash2map[hashval2];
		return Matchtype::MATCH;
	}
	return Matchtype::NOMATCH;
}

uint64_t parseiconfl(const string & texname){ 
//that crappy quick-fix function will allow us to identify the ic textures until the new hashing is ready.
	uint64_t hash = 0;
	string token;
	ofstream check;

	token = texname.substr(6, 2);
	if (token == "ma") {//iconflmaster_XX
		hash += 11111111111111100000;
	} else if (token == "00") {
		hash += 2222222220000000000;
	} else if (token == "01") {
		hash += 3333333330000000000;
	} else if (token == "02") {
		hash += 4444444440000000000;
	} else if (token == "03") {
		hash += 5555555550000000000;
	}

	token = texname.substr(texname.find("_") + 1);
	int num;
	try {
		num = stoi(token);
	} catch (exception&) {
		return hash;
	}

	hash += (num != 15) ? num : 99;

	return hash;
}

uint64_t parsesysfld(const string & texname) {
	uint64_t hash = 0;
	string token;
	ofstream check;

	token = texname.substr(6, 2);
	if (token == "00") {
		hash += 6666666660000000000;
	} else if (token == "01") {
		hash += 7777777770000000000;
	}

	token = texname.substr(texname.find("_") + 1);
	int num;
	try {
		num = stoi(token);
	} catch (exception&) {
		return hash;
	}

	hash += num;

	return hash;
}

//Final unlockrect
void GlobalContext::UnlockRect (D3DSURFACE_DESC &Desc, Bitmap &BmpUseless, HANDLE Handle) //note BmpUseless
{
    IDirect3DTexture9* pTexture = (IDirect3DTexture9*)Handle;   

    String debugtype = String("error");

#if GCONTEXT_DEBUG
	ofstream debug(gcontext_debug_file, fstream::out | fstream::app);
	debug << endl << m << " (" << Handle << "): " << endl;
#endif
	
    if (pTexture && Desc.Width < 640 && Desc.Height < 480 && Desc.Format == D3DFORMAT::D3DFMT_A8R8G8B8 && Desc.Pool == D3DPOOL::D3DPOOL_MANAGED)    // 640x480 are video
    {
        D3DLOCKED_RECT Rect;
        pTexture->LockRect(0, &Rect, NULL, 0);
        UINT pitch = (UINT)Rect.Pitch;
        BYTE* pData = (BYTE*)Rect.pBits;

        uint64_t hash;
		string texturename;
		Matchtype match;
		bool persist = false;
        Hash_Algorithm_1(pData, pitch, Desc.Width, Desc.Height);				// Run Hash_Algorithm_1
        match = getfield(hash, texturename);
		persist = persistent_hashes.count(hash);

#if GCONTEXT_DEBUG
		debug << "  getfield - ";
		debug.flush();
		switch (match) {
		case Matchtype::MATCH: debug << "MATCH:"; break;
		case Matchtype::NOMATCH: debug << "NOMATCH:"; break;
		case Matchtype::COLLISION: debug << "COLLISION:"; break;
		}
		debug << " " << hash << endl;
#endif

        if (sysfld_hashes.count(hash)) {										// Exception for sysfld00 and sysfld01
			if ((match = getsysfld(pData, pitch, Desc.Width, Desc.Height, texturename)) == Matchtype::MATCH) 
				hash = parsesysfld(texturename);
#if GCONTEXT_DEBUG
			debug << "  getsysfld - ";
			debug.flush();
			switch (match) {
			case Matchtype::MATCH: debug << "MATCH:"; break;
			case Matchtype::NOMATCH: debug << "NOMATCH:"; break;
			case Matchtype::COLLISION: debug << "COLLISION?"; break;
			}
			debug << " " << hash << endl;
#endif
		} else if (iconfl_hashes.count(hash)) {									// Exception for iconfl00, iconfl01, iconfl02, iconfl03, iconflmaster
			if ((match = geticonfl(pData, pitch, Desc.Width, Desc.Height, texturename)) == Matchtype::MATCH)
				hash = parseiconfl(texturename);
#if GCONTEXT_DEBUG
			debug << "  geticonfl - ";
			debug.flush();
			switch (match) {
			case Matchtype::MATCH: debug << "MATCH:"; break;
			case Matchtype::NOMATCH: debug << "NOMATCH:"; break;
			case Matchtype::COLLISION: debug << "COLLISION?"; break;
			}
			debug << " " << hash << endl;
#endif
		} 
        
		if (match == Matchtype::NOMATCH) {										// Handle inv�lido, lo borro, pero no su posible textura asociada.
			texcache->erase(Handle);
			debugtype = String("nomatch");
		} else {																// Texture FOUND in Hash_Algorithm_1 OR is a COLLISION
			if (match == Matchtype::COLLISION) {								// Run Hash_Algorithm_2
				Hash_Algorithm_2(pData, pitch, Desc.Width, Desc.Height);
				match = getfield2(hash, texturename);
				persist = persistent_hashes.count(hash);
				if (match == Matchtype::NOMATCH) {
					texcache->erase(Handle);
					debugtype = String("nomatch2");
				}

#if GCONTEXT_DEBUG
				debug << "  getfield2 - ";
				debug.flush();
				switch (match) {
				case Matchtype::MATCH: debug << "MATCH:"; break;
				case Matchtype::NOMATCH: debug << "NOMATCH:"; break;
				case Matchtype::COLLISION: debug << "COLLISION?"; break;
				}
				debug << " " << hash << endl;
#endif
			}

			if (match == Matchtype::MATCH) {
				debugtype = String("noreplace");
				if (!texcache->update(Handle, hash)) {							// directly updated if it succeeds we just end unlockrect cycle.
					string filename = texdir + "textures\\" + texturename.substr(0, 2) + "\\" + texturename.substr(0, texturename.rfind("_")) + "\\" + texturename + ".png";

#if GCONTEXT_DEBUG
					debug << "  Reading " << filename << "...";
					debug.flush();
#endif

					ifstream ifile(filename);
					if (!ifile || ifile.fail()) {											// No file, allow normal SetTexture
						texcache->erase(Handle);
#if GCONTEXT_DEBUG
						debug << " failed." << endl;
#endif
					} else {																// Load texture into cache
#if GCONTEXT_DEBUG
						debug << " succeeded." << endl;
#endif
#if CREATE_TEXTURE_DEBUG && !GCONTEXT_DEBUG
						ofstream debug(gcontext_debug_file, fstream::out | fstream::app);
						debug << endl << m << " (" << Handle << "):" << endl;
#endif
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
						debug << "  Creating new texture...";
#if CREATE_TEXTURE_DEBUG
						debug << endl;
#endif
#endif

						Bitmap Bmp;
						Bmp.LoadPNG(String(filename.c_str()));

						size_t new_width = resize_factor * (float)Desc.Width;
						size_t new_height = resize_factor * (float)Desc.Height;
						if (new_width >= Bmp.Width() && new_height >= Bmp.Height()) {		// replaced HANDLE is large enough for the replacement
#if CREATE_TEXTURE_DEBUG
							debug << "    texture appropriate for replacement; " << Desc.Width << "x" << Desc.Height << " vs " << Bmp.Width() << "x" << Bmp.Height() << endl;
#endif
							LPDIRECT3DDEVICE9 Device = g_Context->Graphics.Device();
							IDirect3DTexture9* newtexture;
							HRESULT result = Device->CreateTexture(new_width, new_height, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &newtexture, NULL);

							if (SUCCEEDED(result)) {										// newtexture successfully created
#if CREATE_TEXTURE_DEBUG
								debug << "    newtexture created; HRESULT = " << result << endl;
#endif

								D3DLOCKED_RECT newRect;
								result = newtexture->LockRect(0, &newRect, NULL, 0);

								if (SUCCEEDED(result)) {									// newtexture successfully locked
#if CREATE_TEXTURE_DEBUG
									debug << "    newtexture locked; HRESULT = " << result << endl;
#endif

									BYTE* newData = (BYTE *)newRect.pBits;

									for (UINT y = 0; y < Bmp.Height(); y++) {
										RGBColor* CurRow = (RGBColor *)(newData + y * newRect.Pitch);
										for (UINT x = 0; x < Bmp.Width(); x++) {			// works for textures of any size (e.g. 4-bit indexed)
											RGBColor Color = Bmp[Bmp.Height() - y - 1][x];  // must flip image
											CurRow[x] = RGBColor(Color.b, Color.g, Color.r, Color.a);
										}
									}
									result = newtexture->UnlockRect(0);						// Texture loaded

									if (SUCCEEDED(result)) {
#if CREATE_TEXTURE_DEBUG
										debug << "    newtexture loaded and unlocked; HRESULT = " << result << endl;
#endif

										HANDLE newhandle = (HANDLE)newtexture;
										texcache->insert(Handle, hash, newhandle, persist);
										debugtype = String("replaced");
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
										debug << "  succeeded. newhandle = " << newhandle << "." << endl;
#endif
									} else {
										texcache->erase(Handle);
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
										debug << "    ERROR: could not unlock newtexture; HRESULT = " << result << endl;
#endif
									}
								} else {
									texcache->erase(Handle);
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
									debug << "    ERROR: could not lock newtexture; HRESULT = " << result << endl;
#endif
								}
							} else {
								texcache->erase(Handle);
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
								debug << "    ERROR: could not create newtexture; HRESULT = " << result << endl;
#endif
							}
						} else {
							texcache->erase(Handle);
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
							debug << "    ERROR: texture too small for replacement; " << Desc.Width << "x" << Desc.Height << " vs " << Bmp.Width() << "x" << Bmp.Height() << endl;
#endif
						}
					}
				}
#if GCONTEXT_DEBUG
				else {
					debug << "  Found existing newhandle." << endl;
				}
#endif
			}
		}
		pTexture->UnlockRect(0);												// Finished reading pTextures bits
	} else {																	// Video textures/improper format
		// this is the beauty of your solution; you replaced that whole O(n^2) loop bullshit with one line ;)
		texcache->erase(Handle);
		debugtype = String("unsupported");
#if GCONTEXT_DEBUG
		debug << "  Unsupported." << endl;
#endif
	}
	// debug
	if (debugmode) {
		String debugfile = String("tonberry\\debug\\") + debugtype + String("\\") + String::ZeroPad(String(m), 3) + String(".bmp");
#if GCONTEXT_DEBUG
		debug << "  Saving debugmode texture to " << debugfile << "... ";
		debug.flush();
#endif
		HRESULT result = D3DXSaveTextureToFile(debugfile.CString(), D3DXIFF_BMP, pTexture, NULL);
		m++; // debug
#if GCONTEXT_DEBUG
		debug << (SUCCEEDED(result) ? "succeeded." : "failed.") << endl;
#endif
	}
#if GCONTEXT_DEBUG || CREATE_TEXTURE_DEBUG
	else m++;
#endif
#if GCONTEXT_DEBUG
	debug.close();
#endif
}

//Final settex

bool GlobalContext::SetTexture(DWORD Stage, HANDLE* SurfaceHandles, UINT SurfaceHandleCount)
{
#if DEBUG
	StartCounter();
#endif
	for (int j = 0; j < SurfaceHandleCount; j++) {
		IDirect3DTexture9* newtexture;
        if (SurfaceHandles[j] && (newtexture = (IDirect3DTexture9*)texcache->at(SurfaceHandles[j]))) {
			HRESULT result = g_Context->Graphics.Device()->SetTexture(Stage, newtexture);
#if DEBUG
	debug << GetCounter() << endl;
#endif
            return true;
		} // Texture replaced!
	}
#if DEBUG
	debug << GetCounter() << endl;
#endif
	return false;
}

//Unused functions
void GlobalContext::UpdateSurface(D3DSURFACE_DESC &Desc, Bitmap &Bmp, HANDLE Handle) {}
void GlobalContext::Destroy(HANDLE Handle) {}
void GlobalContext::CreateTexture (D3DSURFACE_DESC &Desc, Bitmap &Bmp, HANDLE Handle, IDirect3DTexture9** ppTexture) {}
void GlobalContext::BeginScene () {}