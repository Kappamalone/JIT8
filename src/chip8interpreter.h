#pragma once
#include <stdio.h>
#include <chip8.h>

#define identifier(op) (((op) & 0xf000) >> 12)
#define addr(op) ((op) & 0xfff)
#define kk(op) ((op) & 0xff)
#define x(op) (((op) & 0x0f00) >> 8)
#define y(op) (((op) & 0x00f0) >> 4)
#define n(op) (((op) & 0x000f) >> 0)

class Chip8;

class Chip8Interpreter {
public:
	//Returns amount of cycles it took to execute
	//On an interpreter, this is always 1
	static int executeFunc(Chip8& core) {
		auto instr = core.read<uint16_t>(core.pc);
		core.pc += 2;

		switch (identifier(instr)) {
		case 0x0:
			switch (kk(instr)) {
			case 0xE0: Chip8Interpreter::CLS(core, instr); break;
			case 0xEE: Chip8Interpreter::RET(core, instr); break;
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
			switch (n(instr)) {
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
				exit(1);
			}

			break;
		case 0x9: Chip8Interpreter::SNEVxVy(core, instr); break;
		case 0xA: Chip8Interpreter::LDI(core, instr);     break;
		case 0xD: Chip8Interpreter::DXYN(core, instr);    break;
		case 0xF:
			switch (kk(instr)) {
			case 0x33: Chip8Interpreter::LDBVx(core, instr); break;
			case 0x55: Chip8Interpreter::LDIVx(core, instr); break;
			case 0x65: Chip8Interpreter::LDVxI(core, instr); break;
			default:
				printf("Unimplemented Instruction - %04X\n", instr);
				exit(1);
			}

			break;
		default:
			printf("Unimplemented Instruction - %04X\n", instr);
			exit(1);
		}
		return 1;
	};

	static void CLS(Chip8& core, uint16_t instr) { //0x00E0
		core.display.fill(0);
	}

	static void RET(Chip8& core, uint16_t instr) { //0x00EE (post-increment)
		core.pc = core.stack[--core.sp];
	}

	static void JP(Chip8& core, uint16_t instr) { //0x1nnn
		core.pc = addr(instr);
	}

	static void CALL(Chip8& core, uint16_t instr) { //0x2nnn (post-increment)
		core.stack[core.sp++] = core.pc;
		core.pc = addr(instr);
	}

	static void SEVxByte(Chip8& core, uint16_t instr) { //0x3xkk
		if (core.gpr[x(instr)] == kk(instr)) {
			core.pc += 2;
		}
	}

	static void SNEVxByte(Chip8& core, uint16_t instr) { //0x4xkk
		if (core.gpr[x(instr)] != kk(instr)) {
			core.pc += 2;
		}
	}

	static void SEVxVy(Chip8& core, uint16_t instr) { //0x5xy0
		if (core.gpr[x(instr)] == core.gpr[y(instr)]) {
			core.pc += 2;
		}
	}

	static void LDVxByte(Chip8& core, uint16_t instr) { //0x6xkk
		core.gpr[x(instr)] = kk(instr);
	}

	static void ADDVxByte(Chip8& core, uint16_t instr) { //0x7xkk
		core.gpr[x(instr)] += kk(instr);
	}

	static void LDVxVy(Chip8& core, uint16_t instr) { //0x8xy0
		core.gpr[x(instr)] = core.gpr[y(instr)];
	}

	static void ORVxVy(Chip8& core, uint16_t instr) { //0x8xy1
		core.gpr[x(instr)] |= core.gpr[y(instr)];
	}

	static void ANDVxVy(Chip8& core, uint16_t instr) { //0x8xy2
		core.gpr[x(instr)] &= core.gpr[y(instr)];
	}

	static void XORVxVy(Chip8& core, uint16_t instr) { //0x8xy3
		core.gpr[x(instr)] ^= core.gpr[y(instr)];
	}

	static void ADDVxVy(Chip8& core, uint16_t instr) { //0x8xy4
		core.gpr[0xf] = ((uint16_t)core.gpr[x(instr)] + (uint16_t)core.gpr[y(instr)]) > 0xff;
		core.gpr[x(instr)] += core.gpr[y(instr)];
	}

	static void SUBVxVy(Chip8& core, uint16_t instr) { //0x8xy5
		core.gpr[0xf] = core.gpr[x(instr)] > core.gpr[y(instr)];
		core.gpr[x(instr)] -= core.gpr[y(instr)];
	}

	static void SHRVxVy(Chip8& core, uint16_t instr) { //0x8xy6
		core.gpr[0xf] = core.gpr[x(instr)] & 1;
		core.gpr[x(instr)] >>= 1;
	}

	static void SUBNVxVy(Chip8& core, uint16_t instr) { //0x8xy7
		core.gpr[0xf] = core.gpr[y(instr)] > core.gpr[x(instr)];
		core.gpr[x(instr)] = core.gpr[y(instr)] - core.gpr[x(instr)];
	}

	static void SHLVxVy(Chip8& core, uint16_t instr) { //0x8xyE
		core.gpr[0xf] = core.gpr[x(instr)] & 0x80;
		core.gpr[x(instr)] <<= 1;
	}

	static void SNEVxVy(Chip8& core, uint16_t instr) { //0x9xy0
		if (core.gpr[x(instr)] != core.gpr[y(instr)]) {
			core.pc += 2;
		}
	}

	static void LDI(Chip8& core, uint16_t instr) { //0xAnnn
		core.index = addr(instr);
	}

	static void DXYN(Chip8& core, uint16_t instr) { //0xDxyn
		auto xcoord = core.gpr[x(instr)] % 64;
		auto ycoord = core.gpr[y(instr)] % 32;
		core.gpr[0xf] = 0;

		for (int i = 0; i < n(instr); i++) {
			const auto byteData = core.read<uint8_t>(core.index + i);
			for (int j = 0; j < 8; j++) {
				//if (xcoord >= WIDTH || ycoord >= HEIGHT) break;
				const auto pixel = &core.display[xcoord + WIDTH * ycoord];
				const auto bitData = (byteData & (1 << (7 - j))) >> (7 - j);
				*pixel ^= (bool)bitData;

				if (bitData && !*pixel) {
					core.gpr[0xf] = 1;
				}
				++xcoord;
			}
			xcoord -= 8;
			++ycoord;
		}
	}

	static void LDBVx(Chip8& core, uint16_t instr) { //0xFx33
		auto gpr = core.gpr[x(instr)];
		core.ram[core.index] = gpr / 100;
		core.ram[core.index + 1] = (gpr / 10) % 10;
		core.ram[core.index + 2] = gpr % 10;
	}

	static void LDIVx(Chip8& core, uint16_t instr) { //0xFx55
		// for (auto i = 0; i <= x(instr); i++) {
		// 	core.ram[core.index + i] = core.gpr[i];
		// }
		memcpy(core.ram.data() + core.index, core.gpr.data(), x(instr) + 1);
	}

	static void LDVxI(Chip8& core, uint16_t instr) { //0xFx65
		// for (auto i = 0; i <= x(instr); i++) {
		// 	core.gpr[i] = core.ram[core.index + i];
		// }
		memcpy(core.gpr.data(), core.ram.data() + core.index, x(instr) + 1);
	}
};

// Because I'm dumb and naming a macro 'x' in the global namespace breaks things
#undef identifier
#undef addr
#undef kk
#undef x
#undef y
#undef n