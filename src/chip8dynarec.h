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
		if (!page) [[unlikely]] {  // if page hasn't been allocated yet, allocate
			page = new fp[pageSize](); //blocks could be half the size, but I'm not sure about alignment
		}

		auto& block = page[core.pc & (pageSize - 1)];
		if (!block) [[unlikely]] { // if recompiled block doesn't exist, recompile
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
				case 0x0E0: emitCLS(core, instr);                     break;
				case 0x0EE: emitRET(core, instr); jumpOccured = true; break;
				default:
					printf("Unimplemented instr - %04X\n", instr);
					exit(1);
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
			//case 0xD: emitFallback(Chip8Interpreter::DXYN, core, instr);        break;
			case 0xD: emitDXYN(core, instr); break;
			//case 0xD: emitOldDXYN(core, instr); break;
			case 0xE:
				switch (instr & 0xff) {
				case 0x9E: emitSKPVx(core, instr, cycles * 2);  jumpOccured = true; break;
				case 0xA1: emitSKNPVx(core, instr, cycles * 2); jumpOccured = true; break;
				default:
					printf("Unimplemented instr - %04X\n", instr);
					exit(1);
				}

				break;
			case 0xF:
				switch (instr & 0xff) {
				case 0x07: emitLDVxDT(core, instr);                                break;
				case 0x0A: emitLDVxK(core, instr, cycles * 2); jumpOccured = true; break;
				case 0x15: emitLDDTVx(core, instr);                                break;
				case 0x18: emitLDSTVx(core, instr);                                break;
				case 0x1E: emitADDIVx(core, instr);                                break;
				case 0x29: emitLDFVx(core, instr);                                 break;
				case 0x33:
					emitLDBVx(core, instr);
					emitInvalidateRange(core, 3);
					break;
				case 0x55: {
					emitLDIVx(core, instr);
					emitInvalidateRange(core, getx(instr) + 1);
					break;
				}
				case 0x65: emitLDVxI(core, instr); break;
				default:
					printf("Unimplemented instr - %04X\n", instr);
					exit(1);
				}

				break;
			default:
				printf("Unimplemented instr - %04X\n", instr);
				exit(1);
			}

			//This won't work on unaligned PC's
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
		if (code.getSize() + cacheLeeway > cacheSize) [[unlikely]] { //We've nearly exhausted code cache, so throw it out
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

	// Invalidates all blocks from an inclusive startAddress and endAddress
	// Only works with index relative stuff
	// I also don't know if this actually works with self modifying code
	static void emitInvalidateRange(Chip8& core, uint16_t numElementsWritten) {
		// rax: counter
		// rcx: start page
		// rdx: end page, then page count
		// r8: pointer to blockPageTable[start page]

		code.movzx(rcx, word[rbp + getOffset(core, &core.index)]);
		code.mov(rdx, rcx);
		code.mov(r8, (uintptr_t)&blockPageTable);

		code.shr(rcx, pageShift);          //rcx >>= pageShift
		code.add(rdx, numElementsWritten); //rdx = core.index + numElementsWritten
		code.shr(rdx, pageShift);		   //rdx >>= pageShift
		code.sub(rdx, rcx);				   //rdx = rdx - rcx
		code.mov(rax, rdx);				   //rax = rdx
		code.inc(rax);                     // loop endpage-startpage+1 times

		code.lea(r8, ptr[r8 + rcx * sizeof(fp)]);

		Xbyak::Label loop;
		code.L(loop);
		code.mov(qword[r8 + rax * sizeof(fp)], 0);
		code.dec(eax);
		code.jnz(loop);
	}

	// Recompilation

	static void emitCLS(Chip8& core, uint16_t instr) { //0x00E0
		//display = 8 * 32 bytes
		//ymmword = 32 bytes
		//therefore 8 * 32 / 32 = 8 stores required
		code.xorps(xmm0, xmm0);
		for (auto i = 0; i < 8; i++) {
			code.vmovdqa(yword[rbp + getOffset(core, core.display.data()) + i * 32], ymm0);
		}
		code.vzeroupper(); //TODO: learn about AVX context
	}

	static void emitRET(Chip8& core, uint16_t instr) { //0x00EE (post-increment)
		code.dec(byte[rbp + getOffset(core, &core.sp)]);

		code.movzx(rcx, byte[rbp + getOffset(core, &core.sp)]); //load stack pointer
		code.mov(dx, word[rbp + getOffset(core, core.stack.data()) + rcx * sizeof(uint16_t)]);
		code.mov(word[rbp + getOffset(core, &core.pc)], dx);
	}

	static void emitJP(Chip8& core, uint16_t instr) { //1nnn
		code.mov(word[rbp + getOffset(core, &core.pc)], getaddr(instr));
	}

	static void emitCALL(Chip8& core, uint16_t instr, uint16_t PCIncrement) { //0x2nnn (post-increment)
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

	//TODO: fix and make random byte generated at runtime, not compile time
	static void emitRNDVxByte(Chip8& core, uint16_t instr) { //Cxkk
		code.mov(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], (rand() % 256) & getkk(instr));
	}

	// Final boss
	static void emitDXYN(Chip8& core, uint16_t instr) { //Dxyn
		// doesn't check if we're drawing past line 31, but eh
		// rax: temp
		// rcx: startX
		// rdx: startY
		// r8 : pointer to core.ram[core.index]
		// r9 : pointer to core.display[startY]

		// ymm0: spritelines
		// ymm1: displaylines

		auto lines = getn(instr); // how many lines we're drawing
		auto index = 0;           // to index into core.ram and core.display

		code.movzx(ecx, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]); // load startX
		code.movzx(edx, byte[rbp + getOffset(core, &core.gpr[gety(instr)])]); // load startY
		code.and_(ecx, 63); // startX &= 63
		code.and_(edx, 31); // startY &= 31
		code.mov(byte[rbp + getOffset(core, &core.gpr[0xf])], 0);  // core.gpr[0xf] = 0

		code.movzx(eax, word[rbp + getOffset(core, &core.index)]); // load core.index
		code.lea(r8, ptr[rbp + getOffset(core, core.ram.data()) + rax]);
		code.lea(r9, ptr[rbp + getOffset(core, core.display.data()) + rdx * sizeof(uint64_t)]);

		while (lines >= 4) {
			code.vpmovzxbq(ymm0, dword[r8 + index]); // load and zero extend 4 spritelines(bytes) into ymm0
			code.vpsllq(ymm0, ymm0, 56);             // shift packed 64 bit integers by 56
			code.movd(xmm2, ecx);
			code.vpsrlq(ymm0, ymm0, ymm2);           // shift packed 64 bit integers by startX

			code.vmovdqu(ymm1, yword[r9 + index * sizeof(uint64_t)]);  // load ymm1 with 4 displaylines
			code.vptest(ymm0, ymm1);                                   // test for collisions
			code.setnz(al);
			code.or_(byte[rbp + getOffset(core, &core.gpr[0xf])], al); // set on collision

			code.vpxor(ymm1, ymm1, ymm0); // 4 displaylines ^= 4 spritelines
			code.vmovdqu(yword[r9 + index * sizeof(uint64_t)], ymm1); // write back 4 displaylines
			index += 4; // increment index by 4 as 4 display lines have been drawn to screen
			lines -= 4;
		}

		while (lines >= 2) {
			code.vpmovzxbq(xmm0, word[r8 + index]); // load and zero extend 2 spritelines(bytes) into xmm0
			code.vpsllq(xmm0, xmm0, 56);            // shift packed 64 bit integers by 56
			code.movd(xmm2, ecx);                   
			code.vpsrlq(xmm0, xmm0, xmm2);		    // shift packed 64 bit integers by startX

			code.vmovdqu(xmm1, xword[r9 + index * sizeof(uint64_t)]);  // load xmm1 with 2 displaylines
			code.vptest(xmm0, xmm1);                                   // test for collisions
			code.setnz(al);                          
			code.or_(byte[rbp + getOffset(core, &core.gpr[0xf])], al); // set on collision

			code.vpxor(xmm1, xmm1, xmm0); // 2 displaylines ^= 2 spritelines
			code.vmovdqu(xword[r9 + index * sizeof(uint64_t)], xmm1); // write back 2 displaylines
			index += 2; // increment index by 2 as 2 display lines have been drawn to screen
			lines -= 2;
		}

		code.vzeroupper();

		if (lines > 0) { //compensate for odd line
			code.movzx(edx, byte[r8 + index]);
			code.shl(rdx, 56);
			code.shr(rdx, cl);

			code.test(qword[r9 + index * sizeof(uint64_t)], rdx);
			code.setnz(al);
			code.or_(byte[rbp + getOffset(core, &core.gpr[0xf])], al);

			code.xor_(qword[r9 + index * sizeof(uint64_t)], rdx);
		}
	}

	static void emitOldDXYN(Chip8& core, uint16_t instr) { //Dxyn
		// rax: collision detection
		// rcx: startX
		// rdx: startY
		// r8 : pointer to core.ram[core.index]
		// r9 : pointer to core.display[startY]
		code.push(rsi);

		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.movzx(rdx, byte[rbp + getOffset(core, &core.gpr[gety(instr)])]);
		code.and_(cl, 63);
		code.and_(rdx, 31);
		code.mov(byte[rbp + getOffset(core, &core.gpr[0xf])], 0);

		code.movzx(rax, word[rbp + getOffset(core, &core.index)]);
		code.lea(r8, ptr[rbp + getOffset(core, core.ram.data()) + rax]);
		code.lea(r9, ptr[rbp + getOffset(core, core.display.data()) + rdx * sizeof(uint64_t)]);

		for (auto y = 0; y < getn(instr); y++) {
			code.movzx(rsi, byte[r8 + y]);
			code.shl(rsi, 56);
			code.shr(rsi, cl);
			code.test(qword[r9 + y * sizeof(uint64_t)], rsi);
			code.setnz(al);
			code.or_(byte[rbp + getOffset(core, &core.gpr[0xf])], al);
			code.xor_(qword[r9 + y * sizeof(uint64_t)], rsi);
		}

		code.pop(rsi);
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
		code.mov(byte[rbp + getOffset(core, &core.delay)], cl);
	}

	static void emitLDSTVx(Chip8& core, uint16_t instr) { //0xFx18
		code.mov(cl, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.mov(byte[rbp + getOffset(core, &core.sound)], cl);
	}

	static void emitADDIVx(Chip8& core, uint16_t instr) { //0xFx1E
		code.movzx(cx, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.add(word[rbp + getOffset(core, &core.index)], cx);
	}

	static void emitLDFVx(Chip8& core, uint16_t instr) { //0xFx29
		code.movzx(ecx, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.lea(ecx, ptr[ecx * 4 + ecx]); // multiply cx by 5
		code.mov(word[rbp + getOffset(core, &core.index)], cx);
	}

	//TODO: invalidate range function in assembly

	static void emitLDVxK(Chip8& core, uint16_t instr, uint16_t PCIncrement) { //0xFx0A
		Xbyak::Label label1;
		Xbyak::Label label2;
		Xbyak::Label loop;

		code.mov(cx, PCIncrement - 2);
		code.xor_(r8d, r8d);
		code.jmp(loop);

		// takes dl: index
		code.L(label1);
		code.mov(byte[rbp + getOffset(core, &core.gpr[getx(instr)])], dl);
		code.add(cx, 2); // resume execution by removing pc-2
		code.jmp(label2);

		code.L(loop);
		code.cmp(byte[rbp + getOffset(core, core.keyState.data()) + r8], 1); //if (core.keyState[i])
		code.mov(dl, r8b); //move index into dl
		code.je(label1);
		code.inc(r8); //increment counter
		code.cmp(r8, core.keyState.size()); // for(auto i = 0; i < size; i++)
		code.jne(loop);

		code.L(label2);
		code.add(word[rbp + getOffset(core, &core.pc)], cx);
	}

	static void emitLDBVx(Chip8& core, uint16_t instr) { //0xFx33
		// code.mov(cl, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		// code.mov(byte[rbp + getOffset(core, &core.ram[core.index])], cl / 100);
		// code.mov(byte[rbp + getOffset(core, &core.ram[core.index + 1])], (cl / 10) % 100);
		// code.mov(byte[rbp + getOffset(core, &core.ram[core.index + 2])], cl % 10);
		//emitFallback(Chip8Interpreter::LDBVx, core, instr);
		// eax: gpr / 100;
		// ecx: gpr / 10 % 10
		// edx: gpr % 10
		// r8: core.index
		// r9d: divisor
		code.movzx(r8, word[rbp + getOffset(core, &core.index)]);
		code.mov(r9d, 10);

		code.movzx(eax, byte[rbp + getOffset(core, &core.gpr[getx(instr)])]);
		code.xor_(edx, edx); //clear high dword
		code.div(r9d); // gpr / 10 in eax, gpr % 10 in edx
		code.mov(byte[rbp + getOffset(core, core.ram.data()) + r8 + 2], dl); // write gpr % 10 into ram[index + 2]
		code.xor_(edx, edx); //clear high dword
		code.div(r9d); // gpr / 100 in eax, (gpr / 10) % 10 in edx
		code.mov(byte[rbp + getOffset(core, core.ram.data()) + r8], al); // write gpr % 10 into ram[index + 2]
		code.mov(byte[rbp + getOffset(core, core.ram.data()) + r8 + 1], dl); // write gpr % 10 into ram[index + 2]
	}

	static void emitLDIVx(Chip8& core, uint16_t instr) { //0xFx55
		// rcx: core.index
		// rdx: pointer to core.ram.data() + core.index
		// r8:  pointer to core.gpr.data()
		// r9b: byte data
		code.movzx(rcx, word[rbp + getOffset(core, &core.index)]); //load index pointer
		code.lea(rcx, byte[rbp + getOffset(core, core.ram.data()) + rcx]);
		code.lea(r8, byte[rbp + getOffset(core, core.gpr.data())]);

		for (auto i = 0; i < getx(instr) + 1; i++) {
			code.mov(r9b, byte[r8 + i]); // load byte from gpr[counter]
			code.mov(byte[rcx + i], r9b); // write byte to ram[index + counter]
		}
	}

	// same thing above but with pointers switched
	static void emitLDVxI(Chip8& core, uint16_t instr) { //0xFx65
		// rcx: core.index
		// rdx: pointer to core.ram.data() + core.index
		// r8:  pointer to core.gpr.data()
		// r9b: byte data
		code.movzx(rcx, word[rbp + getOffset(core, &core.index)]); //load index pointer
		code.lea(rcx, byte[rbp + getOffset(core, core.ram.data()) + rcx]);
		code.lea(r8, byte[rbp + getOffset(core, core.gpr.data())]);

		for (auto i = 0; i < getx(instr) + 1; i++) {
			code.mov(r9b, byte[rcx + i]); // load byte from ram[index + counter]
			code.mov(byte[r8 + i], r9b); // write byte to gpr[counter] 
		}
	}
};