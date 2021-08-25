#pragma once
#include <stdio.h>
#include <chip8.h>
#include <xbyak/xbyak.h>

class Chip8;

using namespace Xbyak::util;
using fp = void(*)();
using interpreterfp = void(*)(Chip8&, uint16_t);

//The entire code emitter. God bless xbyak
static constexpr int cacheSize = 16 * 1024;
static constexpr int cacheLeeway = 1024; // If currentCacheSize + cacheLeeway > cacheSize, reset cache
static uint8_t cache[cacheSize]; // emitted code cache
class x64Emitter : public Xbyak::CodeGenerator {
public:
	x64Emitter() : CodeGenerator(sizeof(cache), cache) { // Initialize emitter and memory
		setProtectMode(PROTECT_RWE); // Mark emitter memory as readadable/writeable/executable
	}
};

static constexpr int pageSize = 8; // size of cache pages
static constexpr int pageShift = 3; // shift required to get page from address
//TODO: ctz

class Chip8Dynarec {
public:
	inline static fp* blockPageTable[4096 >> pageShift]; //TODO: array of unique ptrs?
	inline static x64Emitter code;
	inline static int cyclesTakenByBlock;

	static int executeFunc(Chip8& core) {
		auto& page = blockPageTable[core.pc >> pageShift];
		if (!page) {  // if page hasn't been allocated yet, allocate
			page = new fp[pageSize](); //blocks could be half the size, but I'm not sure about alignment
		}

		auto& block = page[core.pc & (pageSize - 1)];
		if (!block) { // if recompiled block doesn't exist, recompile
			block = recompileBlock(core, core.pc >> pageShift);
		}

		(*block)();

		return cyclesTakenByBlock; //Each block has an epilogue that updates cyclesTakenByBlock
	}

	static fp recompileBlock(Chip8& core, uint16_t initialPage) {
		checkCodeCache();
		auto emittedCode = (fp)code.getCurr();
		auto cycles = 0;

		// Function prologue
		code.sub(rsp, 40); //permanently align stack for all function calls in block

		while (true) {
			auto instr = core.read<uint16_t>(core.pc);
			core.pc += 2;
			auto breakCache = false;

			switch (((instr) & 0xf000) >> 12) {
			case 0x0:
				switch (instr & 0xff) {
				case 0xE0: interpreterFallback(Chip8Interpreter::CLS, core, instr);                    break;
				case 0xEE: interpreterFallback(Chip8Interpreter::RET, core, instr); breakCache = true; break;
				default:
					printf("Unimplemented Instruction - %04X\n", instr);
					exit(1);
				}

				break;
			case 0x1: interpreterFallback(Chip8Interpreter::JP, core, instr);        breakCache = true; break;
			case 0x2: interpreterFallback(Chip8Interpreter::CALL, core, instr);      breakCache = true; break;
			case 0x3: interpreterFallback(Chip8Interpreter::SEVxByte, core, instr);  breakCache = true; break;
			case 0x4: interpreterFallback(Chip8Interpreter::SNEVxByte, core, instr); breakCache = true; break;
			case 0x5: interpreterFallback(Chip8Interpreter::SEVxVy, core, instr);    breakCache = true; break;
			case 0x6: interpreterFallback(Chip8Interpreter::LDVxByte, core, instr);                     break;
			case 0x7: interpreterFallback(Chip8Interpreter::ADDVxByte, core, instr);                    break;
			case 0x8:
				switch (instr & 0xf) {
				case 0x0: interpreterFallback(Chip8Interpreter::LDVxVy, core, instr);   break;
				case 0x1: interpreterFallback(Chip8Interpreter::ORVxVy, core, instr);   break;
				case 0x2: interpreterFallback(Chip8Interpreter::ANDVxVy, core, instr);  break;
				case 0x3: interpreterFallback(Chip8Interpreter::XORVxVy, core, instr);  break;
				case 0x4: interpreterFallback(Chip8Interpreter::ADDVxVy, core, instr);  break;
				case 0x5: interpreterFallback(Chip8Interpreter::SUBVxVy, core, instr);  break;
				case 0x6: interpreterFallback(Chip8Interpreter::SHRVxVy, core, instr);  break;
				case 0x7: interpreterFallback(Chip8Interpreter::SUBNVxVy, core, instr); break;
				case 0xE: interpreterFallback(Chip8Interpreter::SHLVxVy, core, instr);  break;
				default:
					printf("Unimplemented Instruction - %04X\n", instr);
					exit(1);
				}

				break;
			case 0x9: interpreterFallback(Chip8Interpreter::SNEVxVy, core, instr); breakCache = true; break;
			case 0xA: interpreterFallback(Chip8Interpreter::LDI, core, instr);     break;
			case 0xD: interpreterFallback(Chip8Interpreter::DXYN, core, instr);    break;
			case 0xF:
				switch (instr & 0xff) {
				case 0x29: interpreterFallback(Chip8Interpreter::LDFVx, core, instr); break;
				case 0x33:
					interpreterFallback(Chip8Interpreter::LDBVx, core, instr);
					invalidateRange(core.pc >> pageShift, core.index, core.index + 2);
					breakCache = true;
					break;
				case 0x55: {
					interpreterFallback(Chip8Interpreter::LDIVx, core, instr);
					invalidateRange(core.pc >> pageShift, core.index, core.index + ((instr & 0x0f00) >> 8));
					breakCache = true;
					break;
				}
				case 0x65: interpreterFallback(Chip8Interpreter::LDVxI, core, instr); break;
				default:
					printf("Unimplemented Instruction - %04X\n", instr);
					exit(1);
				}

				break;
			default:
				printf("Unimplemented Instruction - %04X\n", instr);
				exit(1);
			}

			++cycles;
			if (core.pc >> pageShift != initialPage || breakCache) { //If we exceed the page boundary, dip
				break;
			}
		}

		//Function epilogue
		code.add(rsp, 40); // restore stack to original position
		code.mov(rax, (uintptr_t)&cyclesTakenByBlock);
		code.mov(dword[rax], cycles);
		code.ret();

		return emittedCode;
	}

	// Check if code cache is close to being exhausted
	static void checkCodeCache() {
		if (code.getSize() + cacheLeeway > cacheSize) { //We've exhausted code cache, all hell breaks loose
			code.reset();
			memset(blockPageTable, 0, sizeof(blockPageTable));
			printf("Code Cache Exhausted!!\n");
		}
	}

	static void invalidateRange(uint16_t initialPage, uint16_t startAddress, uint16_t endAddress) {
		auto startPage = startAddress >> pageShift;
		auto endPage = endAddress >> pageShift;
		/*if (initialPage >= startPage && initialPage <= endPage) {
			throw "self modifying block!";
		}*/
		memset(&blockPageTable[startPage], 0, endPage - startPage + 1);
	}

	static void interpreterFallback(interpreterfp fallback, Chip8& core, uint16_t instr) {
		code.mov(rax, (uintptr_t)fallback);
		code.mov(rcx, (uintptr_t)&core);
		code.mov(rdx, instr);
		code.call(rax);
	}
};