// Wraps tests/render_test.cpp (built with -Dmain=render_test_main) into a Vita executable: the 19 render
// tests have no console on the device, so this redirects stdout and stderr to a results file under
// ux0:data/MuggleBall3/render-test/ (which MB3_TEST_OUTPUT_DIR also points the test's own save(), the
// per-check PNGs, at: see platform/vita/render_test/CMakeLists.txt), runs the test, appends its exit code
// as the file's last line, and exits. Nothing here is the test itself: that is engine-only (M4a's 19
// conformance checks), unchanged from the host build.
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

#include <cstdio>

// Not extern "C": render_test.cpp's main() (renamed to this by -Dmain=render_test_main) is an ordinary
// C++ function once it is not literally spelled "main" (that spelling is what gets the compiler's special
// no-mangling treatment), so this declaration must mangle the same way, not be unmangled.
int render_test_main();

namespace {
constexpr const char* kResultsDir = "ux0:data/MuggleBall3/render-test";
constexpr const char* kResultsPath = "ux0:data/MuggleBall3/render-test/results.txt";
}  // namespace

int main() {
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir("ux0:data/MuggleBall3", 0777);
    sceIoMkdir(kResultsDir, 0777);

    // freopen targets both stdio streams at the file render_test.cpp's std::printf calls already use, and at
    // sceKernelExitProcess time the console is sent home with the proof of what ran.
    std::freopen(kResultsPath, "w", stdout);
    std::freopen(kResultsPath, "a", stderr);

    const int exitCode = render_test_main();
    std::fprintf(stdout, "exit code: %d\n", exitCode);
    std::fflush(stdout);
    std::fflush(stderr);
    std::fclose(stdout);
    std::fclose(stderr);

    sceKernelExitProcess(exitCode);
    return exitCode;
}
