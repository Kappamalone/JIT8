#pragma once
#include <stdio.h>
#include <chip8.h>
#include <jitcommon.h>

//TODO: use this naming in all files
#define getidentifier(op) (((op) & 0xf000) >> 12)
#define getaddr(op) ((op) & 0xfff)
#define getkk(op) ((op) & 0xff)
#define getx(op) (((op) & 0x0f00) >> 8)
#define gety(op) (((op) & 0x00f0) >> 4)
#define getn(op) (((op) & 0x000f) >> 0)

class Chip8;

class Chip8Dynarec {
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
		auto jumpOccured = false;

		// Function prologue
		code.mov(r8, dynarecPC); // DEBUG: store pc in block to lookup in a decompiler
		code.push(rbp);
		code.mov(rbp, (uintptr_t)&core); //Load cpu state
		code.sub(rsp, 40); //permanently align stack for all function calls in block

		while (true) {
			auto instr = core.read<uint16_t>(dynarecPC);
			dynarecPC += 2;
			++cycles;

			switch (getidentifier(instr)) {
			case 0x0:
				switch (getaddr(instr)) {
				case 0x0E0: emitCLS(core, instr);                    break;
				case 0x0EE: emitRET(core, instr); jumpOccured = true; break;
				default:
					printf("Unimplemented instr - %04X\n", instr);
					//exit(1);
				}

				break;
			case 0x1: emitJP(core, instr); jumpOccured = true;                    break;
			case 0x2: emitCALL(core, instr, cycles * 2); jumpOccured = true;      break;
			case 0x3: emitSEVxByte(core, instr, cycles * 2); jumpOccured = true;  break;
			case 0x4: emitSNEVxByte(core, instr, cycles * 2); jumpOccured = true; break;
			case 0x5: emitSEVxVy(core, instr, cycles * 2); jumpOccured = true;    break;
			case 0x6: emitLDVxByte(core, instr);                                  break;
			case 0x7: emitADDVxByte(core, instr);                                 break;
			case 0x8:
				switch (instr & 0xf) {
				case 0x0: emitLDVxVy(core, instr);   break;
				case 0x1: emitORVxVy(core, instr);   break;
				case 0x2: emitANDVxVy(core, instr);  break;
				case 0x3: emitXORVxVy(core, instr);  break;
				case 0x4: emitADDVxVy(core, instr);  break;
				case 0x5: emitSUBVxVy(core, instr);  break;
				case 0x6: emitSHRVxVy(core, instr);  break;
				case 0x7: emitSUBNVxVy(core, instr); break;
				case 0xE: emitSHLVxVy(core, instr);  break;
				default:
					printf("Unimplemented instr - %04X\n", instr);
					exit(1);
				}

				break;
			case 0x9: emitSNEVxVy(core, instr, cycles * 2); jumpOccured = true; break;
			case 0xA: emitLDI(core, instr);                                     break;
			case 0xB: emitJPV0(core, instr); jumpOccured = true;                break;
			case 0xC: emitRNDVxByte(core, instr);                               break;
			case 0xD: emitFallback(Chip8Interpreter::DXYN, core, instr);        break;
			case 0xE:
				switch (instr & 0xff) {
				case 0x9E: emitSKPVx(core, instr, cycles * 2);  jumpOccured = true; break;
				case 0xA1: emitSKNPVx(core, instr, cycles * 2); jumpOccured = true; break;
				default:
					printf("Unimplemented instr - %04X\n", instr);
					//exit(1);
				}

				break;
			case 0xF:
				switch (instr & 0xff) {
				case 0x07: emitLDVxDT(core, instr);                   break;
				case 0x0A: emitLDVxK(core, instr, cycles * 2); jumpOccured = true; break;
				case 0x15: emitLDDTVx(core, instr);                   break;
				case 0x18: emitLDSTVx(core, instr);                   break;
				case 0x1E: emitADDIVx(core, instr);                   break;
				case 0x29: emitLDFVx(core, instr);                    break;
				case 0x33:
					emitFallback(Chip8Interpreter::LDBVx, core, instr);
					code.mov(rax, (uintptr_t)Chip8CachedInterpreter::invalidateRange);
					code.mov(ecx, word[rbp + getOffset(core, &core.index)]);
					code.mov(edx, word[rbp + getOffset(core, &core.index)]);
					code.add(edx, 2);
					code.call(rax);
					//emitInvalidateRange(core, 2);
					break;
				case 0x55: {
					emitLDIVx(core, instr);
					code.mov(rax, (uintptr_t)Chip8CachedInterpreter::invalidateRange);
					code.mov(ecx, word[rbp + getOffset(core, &core.index)]);
					code.mov(edx, word[rbp + getOffset(core, &core.index)]);
					code.add(edx, ((instr & 0x0f00) >> 8));
					code.call(rax);
					break;
				}
				case 0x65: emitLDVxI(core, instr); break;
				default:
					printf("Unimplemented instr - %04X\n", instr);
					//exit(1);
				}

				break;
			default:
				printf("Unimplemented instr - %04X\n", instr);
				exit(1);
			}

			if ((dynarecPC & (pageSize - 1)) == 0 || jumpOccured) { //If we exceed the page boundary, dip
				break;
			}
		}

		//Function epilogue
		if (!jumpOccured) {
			code.add(word[rbp + getOffset(core, &core.pc)], cycles * 2);
		}

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
		//code.add(word[rbp + getOffset(core, &core.pc)], 2);
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

	// Invalidates all blocks from an inclusive startAddress and endAddress
	// Only works with index relative stuff
	static void emitInvalidateRange(Chip8& core, uint16_t size) {
	}

	// Recompilation

	static void emitCLS(Chip8& core, uint16_t instr) { //0x00E0
		// framebuffer = 64 * 32 * 4 = 8192 bytes
		// ymmword = 256 bits = 32 bytes
		// so 8192/32 = 256 ymmword stores
		code.xorps(xmm0, xmm0);
		for (auto i = 0; i < 256; i++) {
			//code.vmovaps(yword[rbp + getOffset(core, core.framebuffer.data()) + i * 32], ymm0);
			code.vmovdqa(yword[rbp + getOffset(core, core.framebuffer.data()) + i * 32], ymm0);
		}
		code.vzeroupper(); //TODO: learn about AVX context
	}

	static void emitRET(Chip8& core, uint16_t instr) { //0x00EE (post-increment)
		//TODO: block linking?
		code.dec(byte[rbp + getOffset(core, &core.sp)]);

		code.movzx(rcx, byte[rbp + getOffset(core, &core.sp)]); //load stack pointer
		code.mov(dx, word[rbp + getOffset(core, core.stack.data()) + rcx * sizeof(uint16_t)]);
		code.mov(word[rbp + getOffset(core, &core.pc)], dx);
	}

	static void emitJP(Chip8& core, uint16_t instr) { //1nnn
		//TODO: block linking?
		code.mov(word[rbp + getOffset(core, &core.pc)], getaddr(instr));
	}

	static void emitCALL(Chip8& core, uint16_t instr, uint16_t PCIncrement) { //0x2nnn (post-increment)
		//TODO: block linking?
		code.mov(cx, word[rbp + getOffset(core, &core.pc)]);
		code.add(cx, PCIncrement); //update pc before storing it

		code.movzx(rdx, byte[rbp + getOffset(core, &core.sp)]); //load stack pointer

		code.mov(word[rbp + getOffset(core, core.stack.data()) + rdx * sizeof(uint16_t)], cx);
		code.inc(word[rbp + getOffset(core, &core.sp)]);
		code.mov(word[rbp + getOffset(core, &core.pc)], getaddr(instr));
	}

	static void emitSEVxByte(Chip8& core, uint16_t instr, uint16_t PCIncrement) { //0x3xkk
		code.mov(cx, PCIncrement);
		code.mov(dx, PCIncrement + 2); // +2 to skip next instruction
		code.cmp(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], getkk(instr));
		code.cmove(cx, dx); // add instruction skip if cmp = 0
		code.add(word[rbp + getOffset(core, &core.pc)], cx);
	}

	static void emitSNEVxByte(Chip8& core, uint16_t instr, uint16_t PCIncrement) { //0x4xkk
		code.mov(cx, PCIncrement);
		code.mov(dx, PCIncrement + 2); // +2 to skip next instruction
		code.cmp(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], getkk(instr));
		code.cmovne(cx, dx); // add instruction skip if cmp != 0
		code.add(word[rbp + getOffset(core, &core.pc)], cx);
	}

	static void emitSEVxVy(Chip8& core, uint16_t instr, uint16_t PCIncrement) { //0x5xy0
		code.mov(cx, PCIncrement);
		code.mov(dx, PCIncrement + 2); // +2 to skip next instruction
		code.mov(r8b, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.cmp(r8b, byte[rbp + getOffset(core, &core.gpr[gety(instr)])]);
		code.cmove(cx, dx); // add instruction skip if cmp = 0
		code.add(word[rbp + getOffset(core, &core.pc)], cx);
	}

	static void emitLDVxByte(Chip8& core, uint16_t instr) { //6xkk
		code.mov(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], getkk(instr));
	}

	static void emitADDVxByte(Chip8& core, uint16_t instr) { //7xkk
		code.add(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], getkk(instr));
	}

	static void emitLDVxVy(Chip8& core, uint16_t instr) { //0x8xy0
		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[gety(instr)])]);
		code.mov(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], cl);
	}

	static void emitORVxVy(Chip8& core, uint16_t instr) { //0x8xy1
		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[gety(instr)])]);
		code.or_(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], cl);
	}

	static void emitANDVxVy(Chip8& core, uint16_t instr) { //0x8xy2
		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[gety(instr)])]);
		code.and_(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], cl);
	}

	static void emitXORVxVy(Chip8& core, uint16_t instr) { //0x8xy3
		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[gety(instr)])]);
		code.xor_(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], cl);
	}

	static void emitADDVxVy(Chip8& core, uint16_t instr) { //0x8xy4
		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[gety(instr)])]);
		code.add(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], cl);
		code.setc(byte[rbp + getOffset(core, &core.gpr[0xf])]); // set carry
	}

	static void emitSUBVxVy(Chip8& core, uint16_t instr) { //0x8xy5
		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[gety(instr)])]);
		code.sub(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], cl);
		code.setg(byte[rbp + getOffset(core, &core.gpr[0xf])]); // set not borrow
	}

	static void emitSHRVxVy(Chip8& core, uint16_t instr) { //0x8xy6
		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.shr(cl, 1);
		code.setc(byte[rbp + getOffset(core, &core.gpr[0xf])]); //set lsb
		code.mov(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], cl);
	}

	static void emitSUBNVxVy(Chip8& core, uint16_t instr) { //0x8xy7
		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.mov(dl, byte[rbp + getOffset(core, &core.gpr[gety(instr)])]);
		code.sub(dl, cl); //sub y from x
		code.setg(byte[rbp + getOffset(core, &core.gpr[0xf])]); //set not borrow
		code.mov(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], dl); //store into x
	}

	static void emitSHLVxVy(Chip8& core, uint16_t instr) { //0x8xyE
		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.shl(cl, 1);
		code.setc(byte[rbp + getOffset(core, &core.gpr[0xf])]); //set carry
		code.mov(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], cl);
	}

	static void emitSNEVxVy(Chip8& core, uint16_t instr, uint16_t PCIncrement) { //0x9xy0
		code.mov(cx, PCIncrement);
		code.mov(dx, PCIncrement + 2); // +2 to skip next instruction
		code.mov(r8b, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.cmp(r8b, byte[rbp + getOffset(core, &core.gpr[gety(instr)])]);
		code.cmovne(cx, dx); // add instruction skip if cmp != 0
		code.add(word[rbp + getOffset(core, &core.pc)], cx);
	}

	static void emitLDI(Chip8& core, uint16_t instr) { //0xAnnn
		code.mov(word[rbp + getOffset(core, &core.index)], instr & 0xfff);
	}

	static void emitJPV0(Chip8& core, uint16_t instr) { //0xBnnn
		//TODO: block linking?
		code.movzx(cx, byte[rbp + getOffset(core, &core.gpr[0])]);
		code.add(cx, getaddr(instr));
		code.mov(word[rbp + getOffset(core, &core.pc)], cx);
	}

	static void emitRNDVxByte(Chip8& core, uint16_t instr) { //Cxkk
		code.mov(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], (rand() % 256) & getkk(instr));
	}

	static void emitSKPVx(Chip8& core, uint16_t instr, uint16_t PCIncrement) { ////0xEx9E
		code.mov(cx, PCIncrement);
		code.mov(dx, PCIncrement + 2); // +2 to skip next instruction
		code.movzx(r8, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.cmp(byte[rbp + getOffset(core, &core.keyState) + r8], 1);
		code.cmove(cx, dx); // add instruction skip if cmp = 0
		code.add(word[rbp + getOffset(core, &core.pc)], cx);
	}

	static void emitSKNPVx(Chip8& core, uint16_t instr, uint16_t PCIncrement) { //0xExA1
		code.mov(cx, PCIncrement);
		code.mov(dx, PCIncrement + 2); // +2 to skip next instruction
		code.movzx(r8, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.cmp(byte[rbp + getOffset(core, &core.keyState) + r8], 1);
		code.cmovne(cx, dx); // remove instruction skip if cmp = 0
		code.add(word[rbp + getOffset(core, &core.pc)], cx);
	}

	static void emitLDVxDT(Chip8& core, uint16_t instr) { //0xFx07
		code.mov(cl, byte[rbp + getOffset(core, &core.delay)]);
		code.mov(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], cl);
	}

	static void emitLDDTVx(Chip8& core, uint16_t instr) { //0xFx15
		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.mov(word[rbp + getOffset(core, &core.delay)], cl);
	}

	static void emitLDSTVx(Chip8& core, uint16_t instr) { //0xFx18
		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.mov(word[rbp + getOffset(core, &core.sound)], cl);
	}

	static void emitADDIVx(Chip8& core, uint16_t instr) { //0xFx1E
		code.movzx(cx, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.add(word[rbp + getOffset(core, &core.index)], cx);
	}

	static void emitLDFVx(Chip8& core, uint16_t instr) { //0xFx29
		code.movzx(cx, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		//multiply cx by 5
		code.mov(dx, cx);
		code.shl(cx, 2);
		code.add(cx, dx);
		code.mov(word[rbp + getOffset(core, &core.index)], cx);
	}

	//TODO: invalidate range function in assembly

	static void emitLDVxK(Chip8& core, uint16_t instr, uint16_t PCIncrement) { //0xFx0A
		// for (auto i = 0; i < core.keyState.size(); i++) {
		// 	if (core.keyState[i]) {
		// 		core.gpr[getx(instr)] = i;
		// 		return;
		// 	}
		// }
		// core.pc -= 2;

	}

	static void emitLDBVx(Chip8& core, uint16_t instr) { //0xFx33
		// code.mov(cl, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		// code.mov(byte[rbp + getOffset(core, &core.ram[core.index])], cl / 100);
		// code.mov(byte[rbp + getOffset(core, &core.ram[core.index + 1])], (cl / 10) % 100);
		// code.mov(byte[rbp + getOffset(core, &core.ram[core.index + 2])], cl % 10);
	}

	static void emitLDIVx(Chip8& core, uint16_t instr) { //0xFx55
		// rcx: first holds core.index, then counter
		// rdx: pointer to core.ram.data() + core.index
		// r8: pointer to core.gpr.data()
		// r9b: byte data
		code.movzx(rcx, word[rbp + getOffset(core, &core.index)]); //load index pointer
		code.lea(rdx, byte[rbp + getOffset(core, core.ram.data()) + rcx]);
		code.lea(r8, byte[rbp + getOffset(core, core.gpr.data())]);

		code.xor_(ecx, ecx); //zero out rcx

		//TODO: inspect this in ida, might need a local label
		//TODO: you idiot, use a for loop
		Xbyak::Label loop;
		code.L(loop);
		code.mov(r9b, byte[r8 + rcx]); // load byte from gpr[counter]
		code.mov(byte[rdx + rcx], r9b); // write byte to ram[index + counter]
		code.inc(rcx);
		code.cmp(rcx, getx(instr) + 1);
		code.jne(loop);
	}

	// same thing above but with pointers switched
	static void emitLDVxI(Chip8& core, uint16_t instr) { //0xFx65
		// rcx: first holds core.index, then counter
		// rdx: pointer to core.gpr.data()  
		// r8: pointer to core.ram.data() + core.index
		// r9b: byte data
		code.movzx(rcx, word[rbp + getOffset(core, &core.index)]); //load index pointer
		code.lea(rdx, byte[rbp + getOffset(core, core.gpr.data())]);
		code.lea(r8, byte[rbp + getOffset(core, core.ram.data()) + rcx]);

		code.xor_(ecx, ecx); //zero out ecx

		Xbyak::Label loop;
		code.L(loop);
		code.mov(r9b, byte[r8 + rcx]); // load byte from gpr[counter]
		code.mov(byte[rdx + rcx], r9b); // set byte in ram[index + counter]
		code.inc(rcx);
		code.cmp(rcx, getx(instr) + 1);
		code.jne(loop);
	}
};