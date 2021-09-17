#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <gui.h>
#include <chip8.h>
#include <chip8interpreter.h>
#include <chip8cachedinterpreter.h>
#include <chip8dynarec.h>
#include <chip8aot.h>

Chip8::Chip8(GUI* gui, int speed) {
	this->gui = gui;
	this->speed = speed;

	pc = 0x200;
	ram.fill(0);
	stack.fill(0);
	gpr.fill(0);
	keyState.fill(0);
	display.fill(0);

	//loadRom("../../roms/testroms/test_opcode.ch8");
	loadRom("../../roms/invaders");
	loadFonts();
};

Chip8::~Chip8() {
	dumpCodeCache();

	//maaaybe i should've made the code emitters and page tables global...
	for (auto& i : Chip8CachedInterpreter::blockPageTable) {
		delete[] i;
	}

	for (auto& i : Chip8Dynarec::blockPageTable) {
		delete[] i;
	}

	for (auto& i : Chip8AOT::blockPageTable) {
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
		assert(addr <= 0xfff);
		return ram[addr];
	}
	else if (std::is_same<T, uint16_t>::value) {
		assert(addr < 0xfff);
		return ((uint16_t)ram[addr] << 8) | (uint16_t)ram[(addr + 1)];
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

void Chip8::dumpCodeCache() {
	//TODO: only dump if recompiling
	std::ofstream file("emittedcode.bin", std::ios::binary);
	file.write((const char*)Chip8Dynarec::code.getCode(), Chip8Dynarec::code.getSize());
}

void Chip8::runFrame() {
	sf::Clock deltaClock;
	sf::Time elapsedTime;
	sf::Time frameTime;
	int fps = 0;

	//TODO: move this to ctor
	static auto cpuExecuteFunc = Chip8AOT::executeFunc;
	if (cpuExecuteFunc == Chip8AOT::executeFunc) {
		Chip8AOT::recompileAllBlocks(*this);
		printf("Finished AOT recompiling all blocks!\n");
	}

	while (gui->window.isOpen()) {
		//waitForPing();

		//execute one frame's worth of instuctions
		auto cyclesRan = 0;
		while (cyclesRan < (speed / 60)) {
			cyclesRan += cpuExecuteFunc(*this);
		}

		//Calulate fps
		frameTime = deltaClock.restart();
		elapsedTime += frameTime;
		if (elapsedTime.asSeconds() >= 1) [[unlikely]] {
			gui->window.setTitle("JIT8 | FPS: " + std::to_string(fps));
			fps = 0;
			elapsedTime = sf::Time::Zero;
		}

		//Software framelimiter
		//Literally costs around 1-1.5 million fps to have this :(
		//Comment out for max speed
		if (gui->isFrameLimited) {
			//printf("Frame time: %dms\n", frameTime.asMilliseconds());
			//printf("Sleep duration: %dms\n", (sf::milliseconds(17) - frameTime).asMilliseconds());
			sf::sleep(sf::milliseconds(17) - frameTime);
			elapsedTime += deltaClock.restart(); // restart deltaclock so next frame isn't affected
		}

		++fps;

		//pingGuiThread();
	}

	return;
}