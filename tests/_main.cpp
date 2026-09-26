// QindieGL_Tests: CPU unit tests plus black-box OpenGL tests of a built
// QindieGL opengl32.dll.
//
// Usage: QindieGL_Tests.exe [--dll <path to opengl32.dll>] [--pause]
//
// QindieGL reads QindieGL.ini from the working directory when it is loaded,
// so every INI configuration runs in its own child process and directory
// (gl-tests\<configuration>\ next to the executable, which also receives that
// run's QindieGL.log). A crash in QindieGL fails that configuration only.

#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include "tests.h"
#include "gl_harness.h"

static int tests_total = 0;
static int tests_ok = 0;

extern void do_texgen_tests();
extern void do_vbo_tests();
extern void do_multitexture_tests();
extern void do_lighting_tests();
extern void do_extension_availability_tests( bool expectBufferObjects );

namespace {

struct GLConfiguration
{
	const char *name;
	const char *ini;
	bool bufferObjects;	// run VBO tests; otherwise verify the extension is hidden
};

const char kCommonExtensions[] =
	"GL_ARB_multitexture = 1\r\n"
	"GL_EXT_draw_range_elements = 1\r\n"
	"GL_EXT_secondary_color = 1\r\n";

const GLConfiguration kConfigurations[] = {
	{ "vbo-copy-path",
		"[Settings]\r\nLogLevel = 1\r\nDrawCallFastPath = 0\r\n\r\n"
		"[Extensions]\r\nGL_ARB_vertex_buffer_object = 1\r\n", true },
	{ "vbo-fast-path",
		"[Settings]\r\nLogLevel = 1\r\nDrawCallFastPath = 1\r\n\r\n"
		"[Extensions]\r\nGL_ARB_vertex_buffer_object = 1\r\n", true },
	// Mirrors the You Are Empty profile: VBO is exposed by the YAE switch while
	// the global extension setting stays disabled.
	{ "yae-profile",
		"[Settings]\r\nLogLevel = 1\r\nDrawCallFastPath = 1\r\nProjectionFix = 1\r\n\r\n"
		"[game.QindieGL_Tests]\r\nyae_fallback_compatibility = 1\r\nyae_compile_arb_programs = 1\r\n\r\n"
		"[Extensions]\r\nGL_ARB_vertex_buffer_object = 0\r\n", true },
	{ "vbo-disabled",
		"[Settings]\r\nLogLevel = 1\r\n\r\n"
		"[Extensions]\r\nGL_ARB_vertex_buffer_object = 0\r\n", false },
};

std::string ExecutablePath()
{
	char path[MAX_PATH];
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	return path;
}

std::string DirectoryOf( const std::string &path )
{
	const size_t slash = path.find_last_of("\\/");
	return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

bool FileExists( const std::string &path )
{
	const DWORD attributes = GetFileAttributesA(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// Default: the ReleaseNoRemixMods build found by walking up from the executable.
std::string FindQindieGLDll( const char *explicitPath )
{
	if (explicitPath) {
		char full[MAX_PATH];
		GetFullPathNameA(explicitPath, MAX_PATH, full, nullptr);
		return full;
	}
	std::string directory = DirectoryOf(ExecutablePath());
	for (int level = 0; level < 5; ++level) {
		const std::string candidate = directory + "\\bin\\ReleaseNoRemixMods\\opengl32.dll";
		if (FileExists(candidate)) return candidate;
		directory = DirectoryOf(directory);
	}
	return std::string();
}

bool WriteTextFile( const std::string &path, const char *text )
{
	FILE *file = nullptr;
	if (fopen_s(&file, path.c_str(), "wb") || !file) return false;
	fwrite(text, 1, strlen(text), file);
	fclose(file);
	return true;
}

// Returns the child's exit code: its failed assertion count, or a crash code.
DWORD RunConfiguration( const GLConfiguration &configuration, const std::string &dll )
{
	const std::string root = DirectoryOf(ExecutablePath()) + "\\gl-tests";
	const std::string directory = root + "\\" + configuration.name;
	CreateDirectoryA(root.c_str(), nullptr);
	CreateDirectoryA(directory.c_str(), nullptr);

	std::string ini = configuration.ini;
	ini += kCommonExtensions;
	if (!WriteTextFile(directory + "\\QindieGL.ini", ini.c_str())) {
		printf("[%s] cannot write QindieGL.ini\n", configuration.name);
		return 1;
	}

	std::string commandLine = "\"" + ExecutablePath() + "\" --gl-child \"" + dll + "\" " + configuration.name;
	STARTUPINFOA startup = { sizeof(startup) };
	PROCESS_INFORMATION process = {};
	if (!CreateProcessA(nullptr, &commandLine[0], nullptr, nullptr, FALSE, 0, nullptr,
		directory.c_str(), &startup, &process)) {
		printf("[%s] CreateProcess failed (%lu)\n", configuration.name, GetLastError());
		return 1;
	}
	DWORD exitCode = 1;
	if (WaitForSingleObject(process.hProcess, 120000) == WAIT_TIMEOUT) {
		TerminateProcess(process.hProcess, 0xDEAD);
		printf("[%s] timed out\n", configuration.name);
	}
	GetExitCodeProcess(process.hProcess, &exitCode);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return exitCode;
}

int RunGLChild( const char *dll, const char *configurationName )
{
	const GLConfiguration *configuration = nullptr;
	for (const GLConfiguration &candidate : kConfigurations)
		if (!strcmp(candidate.name, configurationName)) configuration = &candidate;
	if (!configuration) {
		printf("[%s] unknown configuration\n", configurationName);
		return 1;
	}

	std::string error;
	if (!Harness_Init(dll, 64, 64, error)) {
		printf("[%s] harness initialisation failed: %s\n", configurationName, error.c_str());
		return 1;
	}
	if (configuration->bufferObjects) {
		do_extension_availability_tests(true);
		do_lighting_tests();
		do_vbo_tests();
		do_multitexture_tests();
	} else {
		do_extension_availability_tests(false);
	}
	Harness_Shutdown();

	printf("[%s] %d/%d checks passed\n", configurationName, tests_ok, tests_total);
	return tests_total - tests_ok;
}

} // namespace

int main( int argc, char **argv )
{
	if (argc >= 4 && !strcmp(argv[1], "--gl-child"))
		return RunGLChild(argv[2], argv[3]);

	const char *dllArgument = nullptr;
	bool pause = false;
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--dll") && i + 1 < argc) dllArgument = argv[++i];
		else if (!strcmp(argv[i], "--pause")) pause = true;
	}

	printf("QindieGL Tests\n");
	do_texgen_tests();

	// Crashes are reported through the child's exit code, not a WER dialog.
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	const std::string dll = FindQindieGLDll(dllArgument);
	if (dll.empty()) {
		printf("QindieGL opengl32.dll not found; pass --dll <path>\n");
		assert(!"QindieGL opengl32.dll found");
	} else {
		printf("Testing %s\n", dll.c_str());
		for (const GLConfiguration &configuration : kConfigurations) {
			const DWORD exitCode = RunConfiguration(configuration, dll);
			char label[128];
			sprintf_s(label, "GL configuration %s (child exit code 0x%lX)", configuration.name, exitCode);
			xassert_str(exitCode == 0, label, __func__, __LINE__, __FILE__);
		}
	}

	printf("Tests results: %d/%d\n", tests_ok, tests_total);
	if (pause) system("pause");
	return tests_ok == tests_total ? 0 : 1;
}

void xassert_str(int success, const char* expression, const char* function, unsigned line, const char* file)
{
    tests_total++;
    if (success) tests_ok++;
    else
    {
        const char* fn = strrchr(file, '\\');
        if (!fn) fn = strrchr(file, '/');
        if (!fn) fn = "fnf";

        printf("assert failed: %s in %s:%d %s\n", expression, function, line, fn);
    }
}

void xassert_int(int success, int printval, const char* function, unsigned line, const char* file)
{
    tests_total++;
    if (success) tests_ok++;
    else
    {
        const char* fn = strrchr(file, '\\');
        if (!fn) fn = strrchr(file, '/');
        if (!fn) fn = "fnf";

        printf("assert failed: 0x%x in %s:%d %s\n", printval, function, line, fn);
    }
}

int ispasswd(int val)
{
    return isalnum(val) || ispunct(val);
}

void random_init()
{
    static int initialised = 0;
    if (initialised == 0)
    {
        unsigned int seed = (unsigned int)time(NULL);
        srand(seed);
        initialised = 1;
        printf("seed: %d\n", seed);
    }
}

void random_bytes(uc8_t* out, int size)
{
    random_init();

    int i;
    for (i = 0; i < size; i++)
    {
        out[i] = rand() % 0x100;
    }
}

void random_text(uc8_t* out, int size)
{
    random_init();

    int i;
    for (i = 0; i < size; )
    {
        int val = rand();
        uc8_t* itr = (uc8_t*)&val;
        for (int j = 0; j < sizeof(val); j++, itr++)
        {
            if (ispasswd(*itr))
            {
                out[i] = *itr;
                i++;
                break;
            }
        }
    }
}
