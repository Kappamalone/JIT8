//TODO: define registers with relevant names and make compatible with system-v abi
#pragma once
#include <xbyak/xbyak.h>

using namespace Xbyak::util;
using fp = int(*)();
using interpreterfp = void(*)(Chip8&, uint16_t);

//The entire code emitter. God bless xbyak
constexpr int cacheSize = 16 * 1024;
constexpr int cacheLeeway = 1024; // If currentCacheSize + cacheLeeway > cacheSize, reset cache
uint8_t cache[cacheSize]; // emitted code cache
class x64Emitter : public Xbyak::CodeGenerator {
public:
	x64Emitter() : CodeGenerator(sizeof(cache), cache) { // Initialize emitter and memory
		setProtectMode(PROTECT_RWE); // Mark emitter memory as readadable/writeable/executable
	}
};

constexpr int pageSize = 8; // size of cache pages
constexpr int pageShift = 3; // shift required to get page froma given address
//TODO: ctz