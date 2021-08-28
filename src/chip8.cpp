#include <mutex>
#include <thread>
#include <chrono>
#include <fstream>
#include <condition_variable>
#include <gui.h>
#include <chip8.h>
#include <chip8interpreter.h>
#include <chip8dynarec.h>

Chip8::Chip8(GUI* gui, int speed) {
	this->gui = gui;
	this->speed = speed;

	this->pc = 0x200;

	ram.fill(0);
	stack.fill(0);
	gpr.fill(0);
	keyState.fill(0);
	framebuffer.fill(0);

	//loadRom("../../roms/testroms/test_opcode.ch8");
	loadRom("../../roms/brix");
	loadFonts();
};

Chip8::~Chip8() {
	for (auto& i : Chip8Dynarec::blockPageTable) {
		delete[] i;
	}
}

void Chip8::waitForPing() {
	std::unique_lock<std::mutex> lock(gui->mRunFrame);
	gui->cvRunFrame.wait(lock, [this] {
		return gui->runFrame;
		});
	//printf("Start Frame!\n");
}

void Chip8::pingGuiThread() {
	gui->runFrame = false;
	gui->cvRunFrame.notify_one();
}

void Chip8::loadRom(const char* path) { //TODO: throw error if file not found
	std::ifstream file(path, std::ios::binary);
	file.read((char*)(ram.data() + 0x200), sizeof(uint8_t) * 4096 - 0x200);
}

template <typename T>
auto Chip8::read(uint16_t addr) -> T {
	if constexpr (std::is_same<T, uint8_t>::value) {
		return ram[addr];
	}
	else if (std::is_same<T, uint16_t>::value) {
		return ((uint16_t)ram[addr] << 8) | (uint16_t)ram[addr + 1];
	}
}

void Chip8::loadFonts() {
	static constexpr std::array <uint8_t, 5 * 16> fonts = {
		0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
		0x20, 0x60, 0x20, 0x20, 0x70, // 1
		0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
		0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
		0x90, 0x90, 0xF0, 0x10, 0x10, // 4
		0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
		0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
		0xF0, 0x10, 0x20, 0x40, 0x40, // 7
		0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
		0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
		0xF0, 0x90, 0xF0, 0x90, 0x90, // A
		0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
		0xF0, 0x80, 0x80, 0x80, 0xF0, // C
		0xE0, 0x90, 0x90, 0x90, 0xE0, // D
		0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
		0xF0, 0x80, 0xF0, 0x80, 0x80, // F
	};

	memcpy(ram.data(), fonts.data(), fonts.size());
}

void Chip8::runFrame() {
	while (true) {
		waitForPing();

		static auto totalCyclesRan = 0; // for debug purposes
		static auto cpuExecuteFunc = Chip8Interpreter::executeFunc;

		//Run (1/60 * speed) cycles per frame (10 by default)
		static auto cyclesToRun = speed / 600; //Just in case we allow for updating speed during runtime
		auto cyclesRan = 0;

		while (cyclesRan < cyclesToRun) {
			cyclesRan += cpuExecuteFunc(*this);
		}

		totalCyclesRan += cyclesToRun;

		if (cpuExecuteFunc == Chip8Dynarec::executeFunc && totalCyclesRan > speed * 3) {
			std::ofstream file("emittedcode.bin", std::ios::binary);
			file.write((const char*)Chip8Dynarec::code.getCode(), Chip8Dynarec::code.getSize());
			printf("Exiting...\n");
			exit(1);
		}

		// Handle timers
		if (delay) --delay;
		if (sound) --sound;

		pingGuiThread();
	}
}