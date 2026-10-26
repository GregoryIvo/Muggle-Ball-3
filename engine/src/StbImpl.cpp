// The single translation unit that compiles the stb implementations.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_GIF
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#if defined(__vita__)
// Thread-local storage is emulated on the Vita (newlib has no real TLS section for it), which stb's thread-local
// error-message buffer does not need: nothing here decodes images from more than one thread.
#define STBI_NO_THREAD_LOCALS
#endif
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
