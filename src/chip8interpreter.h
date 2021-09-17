#pragma once
#include <cassert>
#include <stdio.h>
#include <chip8.h>

#define getidentifier(op) (((op) & 0xf000) >> 12)
#define getaddr(op) ((op) & 0xfff)
#define getkk(op) ((op) & 0xff)
#define getx(op) (((op) & 0x0f00) >> 8)
#define gety(op) (((op) & 0x00f0) >> 4)
#define getn(op) (((op) & 0x000f) >> 0)

class Chip8;

class Chip8Interpreter {
public:
	//Returns amount of cycles it took to execute
	//On an interpreter, this is always 1
	static int executeFunc(Chip8& core) {
		//printf("%04X\n", core.pc);
		auto instr = core.read<uint16_t>(core.pc);
		core.pc += 2;

		switch (getidentifier(instr)) {
		case 0x0:
			switch (getaddr(instr)) {
			case 0x0E0: Chip8Interpreter::CLS(core, instr); break;
			case 0x0EE: Chip8Interpreter::RET(core, instr); break;
			default:
				printf("Unimplemented Instruction - %04X\n", instr);
				exit(1);
			}

			break;
		case 0x1: Chip8Interpreter::JP(core, instr);        break;
		case 0x2: Chip8Interpreter::CALL(core, instr);      break;
		case 0x3: Chip8Interpreter::SEVxByte(core, instr);  break;
		case 0x4: Chip8Interpreter::SNEVxByte(core, instr); break;
		case 0x5: Chip8Interpreter::SEVxVy(core, instr);    break;
		case 0x6: Chip8Interpreter::LDVxByte(core, instr);  break;
		case 0x7: Chip8Interpreter::ADDVxByte(core, instr); break;
		case 0x8:
			switch (getn(instr)) {
			case 0x0: Chip8Interpreter::LDVxVy(core, instr);   break;
			case 0x1: Chip8Interpreter::ORVxVy(core, instr);   break;
			case 0x2: Chip8Interpreter::ANDVxVy(core, instr);  break;
			case 0x3: Chip8Interpreter::XORVxVy(core, instr);  break;
			case 0x4: Chip8Interpreter::ADDVxVy(core, instr);  break;
			case 0x5: Chip8Interpreter::SUBVxVy(core, instr);  break;
			case 0x6: Chip8Interpreter::SHRVxVy(core, instr);  break;
			case 0x7: Chip8Interpreter::SUBNVxVy(core, instr); break;
			case 0xE: Chip8Interpreter::SHLVxVy(core, instr);  break;
			default:
				printf("Unimplemented Instruction - %04X\n", instr);
				//exit(1);
			}

			break;
		case 0x9: Chip8Interpreter::SNEVxVy(core, instr);   break;
		case 0xA: Chip8Interpreter::LDI(core, instr);       break;
		case 0xB: Chip8Interpreter::JPV0(core, instr);      break;
		case 0xC: Chip8Interpreter::RNDVxByte(core, instr); break;
		case 0xD: Chip8Interpreter::DXYN(core, instr);      break;
		case 0xE:
			switch (getkk(instr)) {
			case 0x9E: Chip8Interpreter::SKPVx(core, instr);  break;
			case 0xA1: Chip8Interpreter::SKNPVx(core, instr); break;
			default:
				printf("Unimplemented Instruction - %04X\n", instr);
				//exit(1);
			}

			break;
		case 0xF:
			switch (getkk(instr)) {
			case 0x07: Chip8Interpreter::LDVxDT(core, instr); break;
			case 0x0A: Chip8Interpreter::LDVxK(core, instr);  break;
			case 0x15: Chip8Interpreter::LDDTVx(core, instr); break;
			case 0x18: Chip8Interpreter::LDSTVx(core, instr); break;
			case 0x1E: Chip8Interpreter::ADDIVx(core, instr); break;
			case 0x29: Chip8Interpreter::LDFVx(core, instr);  break;
			case 0x33: Chip8Interpreter::LDBVx(core, instr);  break;
			case 0x55: Chip8Interpreter::LDIVx(core, instr);  break;
			case 0x65: Chip8Interpreter::LDVxI(core, instr);  break;
			default:
				printf("Unimplemented Instruction - %04X\n", instr);
				//exit(1);
			}

			break;
		default:
			printf("Unimplemented Instruction - %04X\n", instr);
			//exit(1);
		}
		return 1;
	};

	static void CLS(Chip8& core, uint16_t instr) { //0x00E0
		//core.framebuffer.fill(0);
		core.display.fill(0);
	}

	static void RET(Chip8& core, uint16_t instr) { //0x00EE (post-increment)
		core.pc = core.stack[--core.sp];
	}

	static void JP(Chip8& core, uint16_t instr) { //0x1nnn
		core.pc = getaddr(instr);
	}

	static void CALL(Chip8& core, uint16_t instr) { //0x2nnn (post-increment)
		core.stack[core.sp++] = core.pc;
		core.pc = getaddr(instr);
	}

	static void SEVxByte(Chip8& core, uint16_t instr) { //0x3xkk
		if (core.gpr[getx(instr)] == getkk(instr)) {
			core.pc += 2;
		}
	}

	static void SNEVxByte(Chip8& core, uint16_t instr) { //0x4xkk
		if (core.gpr[getx(instr)] != getkk(instr)) {
			core.pc += 2;
		}
	}

	static void SEVxVy(Chip8& core, uint16_t instr) { //0x5xy0
		if (core.gpr[getx(instr)] == core.gpr[gety(instr)]) {
			core.pc += 2;
		}
	}

	static void LDVxByte(Chip8& core, uint16_t instr) { //0x6xkk
		core.gpr[getx(instr)] = getkk(instr);
	}

	static void ADDVxByte(Chip8& core, uint16_t instr) { //0x7xkk
		core.gpr[getx(instr)] += getkk(instr);
	}

	static void LDVxVy(Chip8& core, uint16_t instr) { //0x8xy0
		core.gpr[getx(instr)] = core.gpr[gety(instr)];
	}

	static void ORVxVy(Chip8& core, uint16_t instr) { //0x8xy1
		core.gpr[getx(instr)] |= core.gpr[gety(instr)];
	}

	static void ANDVxVy(Chip8& core, uint16_t instr) { //0x8xy2
		core.gpr[getx(instr)] &= core.gpr[gety(instr)];
	}

	static void XORVxVy(Chip8& core, uint16_t instr) { //0x8xy3
		core.gpr[getx(instr)] ^= core.gpr[gety(instr)];
	}

	static void ADDVxVy(Chip8& core, uint16_t instr) { //0x8xy4
		core.gpr[0xf] = ((uint16_t)core.gpr[getx(instr)] + (uint16_t)core.gpr[gety(instr)]) > 0xff;
		core.gpr[getx(instr)] += core.gpr[gety(instr)];
	}

	static void SUBVxVy(Chip8& core, uint16_t instr) { //0x8xy5
		core.gpr[0xf] = core.gpr[getx(instr)] > core.gpr[gety(instr)];
		core.gpr[getx(instr)] -= core.gpr[gety(instr)];
	}

	static void SHRVxVy(Chip8& core, uint16_t instr) { //0x8xy6
		core.gpr[0xf] = core.gpr[getx(instr)] & 1;
		core.gpr[getx(instr)] >>= 1;
	}

	static void SUBNVxVy(Chip8& core, uint16_t instr) { //0x8xy7
		core.gpr[0xf] = core.gpr[gety(instr)] > core.gpr[getx(instr)];
		core.gpr[getx(instr)] = core.gpr[gety(instr)] - core.gpr[getx(instr)];
	}

	static void SHLVxVy(Chip8& core, uint16_t instr) { //0x8xyE
		core.gpr[0xf] = (core.gpr[getx(instr)] & 0x80) >> 7;
		core.gpr[getx(instr)] <<= 1;
	}

	static void SNEVxVy(Chip8& core, uint16_t instr) { //0x9xy0
		if (core.gpr[getx(instr)] != core.gpr[gety(instr)]) {
			core.pc += 2;
		}
	}

	static void LDI(Chip8& core, uint16_t instr) { //0xAnnn
		core.index = getaddr(instr);
	}

	static void JPV0(Chip8& core, uint16_t instr) { //0xBnnn
		core.pc = (uint16_t)core.gpr[0] + getaddr(instr);
	}

	static void RNDVxByte(Chip8& core, uint16_t instr) { //0xCxkk
		core.gpr[getx(instr)] = (rand() % 256) & getkk(instr);
	}

	static void DXYN(Chip8& core, uint16_t instr) { //0xDxyn
		const auto startX = core.gpr[getx(instr)] & 63;
		const auto startY = core.gpr[gety(instr)] & 31;
		core.gpr[0xf] = 0;

		for (auto y = 0; y < getn(instr); y++) {
			if (startY + y == HEIGHT) return;

			uint64_t spriteLine = (uint64_t)core.ram[core.index + y];
			spriteLine = (spriteLine << 56) >> startX;
			uint64_t& displayLine = core.display[startY + y];
			core.gpr[0xf] |= (displayLine & spriteLine) != 0;
			displayLine ^= spriteLine;
		}
	}

	static void SKPVx(Chip8& core, uint16_t instr) { //Ex9E
		if (core.keyState[core.gpr[getx(instr)]]) {
			core.pc += 2;
		}
	}

	static void SKNPVx(Chip8& core, uint16_t instr) { //ExA1
		if (!core.keyState[core.gpr[getx(instr)]]) {
			core.pc += 2;
		}
	}

	static void LDVxDT(Chip8& core, uint16_t instr) { //0xFx07
		core.gpr[getx(instr)] = core.delay;
	}

	static void LDVxK(Chip8& core, uint16_t instr) { //0xFx0A
		for (auto i = 0; i < core.keyState.size(); i++) {
			if (core.keyState[i]) {
				core.gpr[getx(instr)] = i;
				return;
			}
		}
		core.pc -= 2;
	}

	static void LDDTVx(Chip8& core, uint16_t instr) { //0xFx15
		core.delay = core.gpr[getx(instr)];
	}

	static void LDSTVx(Chip8& core, uint16_t instr) { //0xFx18
		core.sound = core.gpr[getx(instr)];
	}

	static void ADDIVx(Chip8& core, uint16_t instr) { //0xFx1E
		core.index += core.gpr[getx(instr)];
	}

	static void LDFVx(Chip8& core, uint16_t instr) { //0xFx29
		core.index = (uint16_t)core.gpr[getx(instr)] * 0x5;
	}

	static void LDBVx(Chip8& core, uint16_t instr) { //0xFx33
		auto gpr = core.gpr[getx(instr)];
		core.ram[core.index] = gpr / 100;
		core.ram[core.index + 1] = (gpr / 10) % 10;
		core.ram[core.index + 2] = gpr % 10;
	}

	static void LDIVx(Chip8& core, uint16_t instr) { //0xFx55
		memcpy(core.ram.data() + core.index, core.gpr.data(), getx(instr) + 1);
	}

	static void LDVxI(Chip8& core, uint16_t instr) { //0xFx65
		memcpy(core.gpr.data(), core.ram.data() + core.index, getx(instr) + 1);
	}
};