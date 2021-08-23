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

class Chip8Dynarec {
public:
	inline static fp* blockPageTable[4096 / 8]; //TODO: array of unique ptrs?
	inline static x64Emitter code;
	inline static int cyclesTakenByBlock;

	static int executeFunc(Chip8& core) {
		auto& page = blockPageTable[core.pc >> 3];
		if (!page) {  // if page hasn't been allocated yet, allocate
			page = new fp[8](); //blocks could be size 4 instead of 8, but I'm not sure about alignment
		}

		auto& block = page[core.pc & 0x7];
		if (!block) { // if recompiled block doesn't exist, recompile
			block = recompileBlock(core, core.pc >> 3);
		}

		(*block)();

		return cyclesTakenByBlock; //Each block has an epilogue that updates cyclesTakenByBlock
	}

	static fp recompileBlock(Chip8& core, uint16_t initialPage) {
		checkCodeCache();
		auto emittedCode = (fp)code.getCurr();
		auto cycles = 0;

		while (true) {
			auto instr = core.read<uint16_t>(core.pc);
			auto isJump = false;

			switch (((instr) & 0xf000) >> 12) {
			case 0x0:

				switch (instr & 0xff) {

				case 0xE0: interpreterFallback(Chip8Interpreter::CLS, core, instr); break;
				default:
					printf("Unimplemented Instruction - %04X\n", instr);
					exit(1);
				}

				break;
			case 0x1: interpreterFallback(Chip8Interpreter::JP, core, instr); isJump = true;     break;
			case 0x6: interpreterFallback(Chip8Interpreter::LDVxByte, core, instr);              break;
			case 0x7: interpreterFallback(Chip8Interpreter::ADDVxByte, core, instr);             break;
			case 0xA: interpreterFallback(Chip8Interpreter::LDI, core, instr);                   break;
			case 0xD: interpreterFallback(Chip8Interpreter::DXYN, core, instr);                  break;
			default:
				printf("Unimplemented Instruction - %04X\n", instr);
				exit(1);
			}

			++cycles;
			core.pc += 2;
			if (core.pc >> 3 != initialPage || isJump) { //If we exceed the page boundary, dip
				break;
			}
		}

		//Function epilogue
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

	static void interpreterFallback(interpreterfp fallback, Chip8& core, uint16_t instr) {
		code.mov(rax, (uintptr_t)fallback);
		code.mov(rcx, (uintptr_t)&core);
		code.mov(rdx, instr);
		code.sub(rsp, 40);
		code.call(rax);
		code.add(rsp, 40);
	}
};