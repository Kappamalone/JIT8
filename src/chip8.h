#pragma once
#include <array>
#include <vector>
#include <thread>
#include <stdint.h>
#include <gui.h>

class GUI;

static constexpr int WIDTH = 64;
static constexpr int HEIGHT = 32;

class Chip8 {
private:
	GUI* gui;

	//Config
	int speed; //how many cycles executed in a second

	//Memory
	std::array<uint8_t, 4096> ram;
	std::array<uint16_t, 16> stack;

	//Registers
	uint16_t pc = 0; //program counter
	uint8_t sp = 0; //stack pointer
	uint16_t index = 0; //index register
	uint8_t delay = 0; //delay timer
	uint8_t sound = 0; //sound timer
	std::array<uint8_t, 16> gpr; //16 registers from V0 - VF

public:
	friend class Chip8Interpreter;
	friend class Chip8CachedInterpreter;
	friend class Chip8Dynarec;

	
	alignas(32) std::array<uint32_t, WIDTH* HEIGHT> framebuffer;
	std::array<bool, 16> keyState; //input

	Chip8(GUI* gui, int speed);
	~Chip8();
	void waitForPing();
	void pingGuiThread();
	void runFrame();
	void loadRom(const char* path);
	void loadFonts();

	//Utility stuff
	template <typename T>
	T read(uint16_t addr);

	void dumpCodeCache();
};