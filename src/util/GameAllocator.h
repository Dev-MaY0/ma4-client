#pragma once

// thanks mojang for introducing a custom global mem allocator for zero reason :)

// claude slop explanation:
// Since 26.60 Minecraft routes operator new/delete through its own global allocator
// (Bedrock::Memory::IMemoryAllocator, backed by MallocRedirect.dll). Memory crosses the
// boundary between Ma4 and the game all the time (std::string, std::vector, shared_ptr
// control blocks, ...), so Ma4's global operator new/delete are replaced in
// GameAllocator.cpp to allocate from the same allocator as the game.
namespace GameAllocator {
    const char* backendName();
}
