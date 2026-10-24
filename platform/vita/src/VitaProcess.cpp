// The three memory globals every Vita executable needs. Newlib's heap is one fixed block taken at start-up
// (default 128 MiB), and the main thread's and a std::thread's default stacks come from outside it (defaults
// 256 KiB and 32 KiB, which Jolt's workers and the render test overflow); newlib references these weakly, so
// a static library linking them would not pull them in, and every Vita executable (the hello, the render
// test, and later the game) lists this file in its own sources. Same values and comment as the hello
// (platform/vita/hello/main.cpp), which keeps its own copy since it does not link mb_engine.
extern "C" {
unsigned int _newlib_heap_size_user = 160 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 1024 * 1024;
unsigned int _pthread_stack_default_user = 512 * 1024;
}
