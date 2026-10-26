/// UzEngine stb_image Implementation
///
/// Compiles stb_image implementation into the runtime library.
/// This file MUST be compiled exactly once — do NOT include
/// STB_IMAGE_IMPLEMENTATION in any other translation unit.
///
/// # Decoder allow-list
/// `stbi_load*` picks a decoder from the file's content, not its extension,
/// so every compiled-in decoder is reachable from any texture a project ships.
/// Only the formats the engine accepts are compiled:
/// - PNG, JPEG — scene, material, cloud and VFX textures; screenshot diffs.
/// - HDR (Radiance) — environment maps via `TextureLoader::LoadHDR*`.
/// - TGA — `TextureImporter` advertises `.tga` and `content/vfx` ships a TGA
///   flipbook. TGA has no signature, so it is probed last.
///
/// GIF, PSD, PIC, PNM and BMP stay compiled out. The engine never loads them,
/// the GIF decoder carries unfixed upstream memory-safety bugs
/// (CVE-2023-45661, -45664, -45666, -45667), the PIC decoder carries
/// CVE-2023-43898, and PSD/PNM/BMP are unused attack surface. The TGA decoder
/// still carries CVE-2023-45663 (uninitialized heap bytes returned as pixels).

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_HDR
#define STBI_ONLY_TGA
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
