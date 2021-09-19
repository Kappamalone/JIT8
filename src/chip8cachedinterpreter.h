#pragma once
#include <stdio.h>
#include <chip8.h>
#include <jitcommon.h>

class Chip8;

class Chip8CachedInterpreter {
public:
	inline static fp* blockPageTable[4096 >> pageShift]; //TODO: array of unique ptrs?
	inline static x64Emitter code;

	// Get offset from a variable to the cpu core
	static uintptr_t constexpr inline getOffset(Chip8& core, void* variable) {
		return (uintptr_t)variable - (uintptr_t)&core;
	}

	static int executeFunc(Chip8& core) {
		//printf("%04X\n", core.pc);

		auto& page = blockPageTable[core.pc >> pageShift];
		if (!page) {  // if page hasn't been allocated yet, allocate
			page = new fp[pageSize](); //blocks could be half the size, but I'm not sure about alignment
		}

		auto& block = page[core.pc & (pageSize - 1)];
		if (!block) { // if recompiled block doesn't exist, recompile
			block = recompileBlock(core);
		}

		auto cyclesTakenByBlock = (*block)(); //each block returns cycles taken in eax

		return cyclesTakenByBlock;
	}

	static fp recompileBlock(Chip8& core) {
		checkCodeCache();
		auto emittedCode = (fp)code.getCurr();
		auto cycles = 0;
		auto dynarecPC = core.pc;

		// Function prologue
		code.push(rbp);
		code.mov(rbp, (uintptr_t)&core); //Load cpu state
		code.sub(rsp, 40); //permanently align stack for all function calls in block
		auto addPCPointer = code.getSize(); //get pointer to cache position to overwrite later
		code.add(word[rbp + getOffset(core, &core.pc)], 0);

		while (true) {
			auto instr = core.read<uint16_t>(dynarecPC);
			dynarecPC += 2;
			auto jumpOccured = false;

			switch (((instr) & 0xf000) >> 12) {
			case 0x0:
				switch (instr & 0xfff) {
				case 0x0E0: emitFallback(Chip8Interpreter::CLS, core, instr);                    break;
				case 0x0EE: emitFallback(Chip8Interpreter::RET, core, instr); jumpOccured = true; break;
				default:
					printf("Unimplemented Instruction - %04X\n", instr);
					//exit(1);
				}

				break;
			case 0x1: emitFallback(Chip8Interpreter::JP, core, instr);        jumpOccured = true; break;
			case 0x2: emitFallback(Chip8Interpreter::CALL, core, instr);      jumpOccured = true; break;
			case 0x3: emitFallback(Chip8Interpreter::SEVxByte, core, instr);  jumpOccured = true; break;
			case 0x4: emitFallback(Chip8Interpreter::SNEVxByte, core, instr); jumpOccured = true; break;
			case 0x5: emitFallback(Chip8Interpreter::SEVxVy, core, instr);    jumpOccured = true; break;
			case 0x6: emitFallback(Chip8Interpreter::LDVxByte, core, instr);                     break;
			case 0x7: emitFallback(Chip8Interpreter::ADDVxByte, core, instr);                    break;
			case 0x8:
				switch (instr & 0xf) {
				case 0x0: emitFallback(Chip8Interpreter::LDVxVy, core, instr);   break;
				case 0x1: emitFallback(Chip8Interpreter::ORVxVy, core, instr);   break;
				case 0x2: emitFallback(Chip8Interpreter::ANDVxVy, core, instr);  break;
				case 0x3: emitFallback(Chip8Interpreter::XORVxVy, core, instr);  break;
				case 0x4: emitFallback(Chip8Interpreter::ADDVxVy, core, instr);  break;
				case 0x5: emitFallback(Chip8Interpreter::SUBVxVy, core, instr);  break;
				case 0x6: emitFallback(Chip8Interpreter::SHRVxVy, core, instr);  break;
				case 0x7: emitFallback(Chip8Interpreter::SUBNVxVy, core, instr); break;
				case 0xE: emitFallback(Chip8Interpreter::SHLVxVy, core, instr);  break;
				default:
					printf("Unimplemented Instruction - %04X\n", instr);
					//exit(1);
				}

				break;
			case 0x9: emitFallback(Chip8Interpreter::SNEVxVy, core, instr); jumpOccured = true; break;
			case 0xA: emitFallback(Chip8Interpreter::LDI, core, instr);                        break;
			case 0xB: emitFallback(Chip8Interpreter::JPV0, core, instr);    jumpOccured = true; break;
			case 0xC: emitFallback(Chip8Interpreter::RNDVxByte, core, instr); break;
			case 0xD: emitFallback(Chip8Interpreter::DXYN, core, instr);                       break;
			case 0xE:
				switch (instr & 0xff) {
				case 0x9E: emitFallback(Chip8Interpreter::SKPVx, core, instr);  jumpOccured = true; break;
				case 0xA1: emitFallback(Chip8Interpreter::SKNPVx, core, instr); jumpOccured = true; break;
				default:
					printf("Unimplemented Instruction - %04X\n", instr);
					//exit(1);
				}

				break;
			case 0xF:
				switch (instr & 0xff) {
				case 0x07: emitFallback(Chip8Interpreter::LDVxDT, core, instr);                   break;
				case 0x0A: emitFallback(Chip8Interpreter::LDVxK, core, instr); jumpOccured = true; break;
				case 0x15: emitFallback(Chip8Interpreter::LDDTVx, core, instr);                   break;
				case 0x18: emitFallback(Chip8Interpreter::LDSTVx, core, instr);                   break;
				case 0x1E: emitFallback(Chip8Interpreter::ADDIVx, core, instr);                   break;
				case 0x29: emitFallback(Chip8Interpreter::LDFVx, core, instr);                    break;
				case 0x33:
					emitFallback(Chip8Interpreter::LDBVx, core, instr);
					code.mov(rax, (uintptr_t)Chip8CachedInterpreter::invalidateRange);
					code.mov(ecx, word[rbp + getOffset(core, &core.index)]);
					code.mov(edx, word[rbp + getOffset(core, &core.index)]);
					code.add(edx, 2);
					code.call(rax);
					break;
				case 0x55: {
					emitFallback(Chip8Interpreter::LDIVx, core, instr);
					code.mov(rax, (uintptr_t)Chip8CachedInterpreter::invalidateRange);
					code.mov(ecx, word[rbp + getOffset(core, &core.index)]);
					code.mov(edx, word[rbp + getOffset(core, &core.index)]);
					code.add(edx, ((instr & 0x0f00) >> 8));
					code.call(rax);
					break;
				}
				case 0x65: emitFallback(Chip8Interpreter::LDVxI, core, instr); break;
				default:
					printf("Unimplemented Instruction - %04X\n", instr);
					//exit(1);
				}

				break;
			default:
				printf("Unimplemented Instruction - %04X\n", instr);
				//exit(1);
			}

			++cycles;
			if ((dynarecPC & (pageSize - 1)) == 0 || jumpOccured) { //If we exceed the page boundary, dip
				break;
			}
		}

		//Function epilogue

		// Set cycles taken by block retroactively in prologue
		auto returnPointer = code.getSize();
		code.setSize(addPCPointer);
		code.add(word[rbp + getOffset(core, &core.pc)], cycles * 2);
		code.setSize(returnPointer);

		code.add(rsp, 40); // restore stack to original position
		code.pop(rbp);
		code.mov(eax, cycles); // set return value as cycles taken in block
		code.ret();

		return emittedCode;
	}

	// Check if code cache is close to being exhausted
	static void checkCodeCache() {
		if (code.getSize() + cacheLeeway > cacheSize) { //We've nearly exhausted code cache, so throw it out
			code.reset();
			memset(blockPageTable, 0, sizeof(blockPageTable));
			printf("Code Cache Exhausted!!\n");
		}
	}

	static void emitFallback(interpreterfp fallback, Chip8& core, uint16_t instr) {
		//code.add(dword[rbp + getOffset(core, &core.pc)], 2);
		code.mov(rax, (uintptr_t)fallback);
		code.mov(rcx, (uintptr_t)&core);
		code.mov(edx, instr);
		code.call(rax);
	}

	//TODO: handle wrapping
	// Invalidates all blocks from an inclusive startAddress and endAddress
	static void invalidateRange(uint16_t startAddress, uint16_t endAddress) {
		const auto startPage = startAddress >> pageShift;
		const auto endPage = endAddress >> pageShift;
		memset(&blockPageTable[startPage], 0, (endPage - startPage + 1) * sizeof(fp));
	}
};