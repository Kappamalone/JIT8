#pragma once
#include <math.h>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <SFML/Window.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>
#include <chip8.h>

static constexpr int SAMPLES = 44100;
static constexpr int SAMPLERATE = 44100;

//TODO: sound, debug only stuff, cl arguments and cleanup
class GUI {
private:
	// rendering
	sf::Texture texture;
	sf::Sprite sprite;
	std::array<uint32_t, WIDTH * HEIGHT> framebuffer;

	// audio
	sf::Sound sound;
	sf::SoundBuffer soundBuffer;
	std::array<int16_t, SAMPLES> rawBuffer;
	uint32_t amplitude;

	// chip8 and threading
	std::thread emu_thread;
	Chip8 core;
public:
	sf::RenderWindow window;

	//config
	bool isFrameLimited = true; //controls frame limiting
	void toggleFramelimiter() {
		isFrameLimited ^= true;
	}

	// thread syncing stuff isn't used, but might be useful for future projects
	bool runFrame;
	std::mutex mRunFrame;
	std::condition_variable cvRunFrame;

	GUI() : window(sf::VideoMode(640, 320), "JIT8"), core(this, 600) {
		emu_thread = std::thread([this]() {
			core.runFrame();
			});
		//TODO: what does .detach actually do?
		//emu_thread.detach(); //fly free, emu thread... 

		runFrame = false;
		window.setFramerateLimit(60);
		window.setTitle("JIT8 | FPS: " + std::to_string(60));

		//Initialise SFML stuff
		texture.create(64, 32);
		sprite.setTexture(texture);
		sprite.setScale(sf::Vector2f(10, 10));
		initAudio();
		framebuffer.fill(0);
		rawBuffer.fill(0);
	}

	~GUI() = default;

	void pingEmuthread() {
		runFrame = true;
		cvRunFrame.notify_one();
	}

	void waitForEmuThread() {
		std::unique_lock<std::mutex> lock(mRunFrame);
		cvRunFrame.wait(lock, [this] {
			return !runFrame;
			});
		//printf("End Frame!\n");
	}

	void run() {
		while (window.isOpen()) {
			//pingEmuthread();

			handleInput();

			// Handle timers
			if (core.delay) --core.delay;
			if (core.sound) {
				sound.play();
				--core.sound;
				if (!core.sound) {
					sound.pause();
				}
			}

			//Draw framebuffer to screen
			drawToFramebuffer();
			texture.update((uint8_t*)framebuffer.data());
			window.clear();
			window.draw(sprite);
			window.display();

			//waitForEmuThread();
		}

		emu_thread.join();
	}

	void drawToFramebuffer() {
		for (auto i = 0; i < HEIGHT; i++) {
			const auto line = core.display[i];
			for (auto j = 0; j < WIDTH; j++) {
				const auto bit = (line >> (63 - j)) & 1;
				if (bit) {
					framebuffer[j + i * WIDTH] = 0xffffffff;
				} else {
					framebuffer[j + i * WIDTH] = 0;
				}
			}
		}
	}

	void handleInput() {
		static std::unordered_map <sf::Keyboard::Key, int> keyMappings = {
		   {sf::Keyboard::Num1, 0x1},
		   {sf::Keyboard::Num2, 0x2},
		   {sf::Keyboard::Num3, 0x3},
		   {sf::Keyboard::Num4, 0xC},
		   {sf::Keyboard::Q, 0x4},
		   {sf::Keyboard::W, 0x5},
		   {sf::Keyboard::E, 0x6},
		   {sf::Keyboard::R, 0xD},
		   {sf::Keyboard::A, 0x7},
		   {sf::Keyboard::S, 0x8},
		   {sf::Keyboard::D, 0x9},
		   {sf::Keyboard::F, 0xE},
		   {sf::Keyboard::Z, 0xA},
		   {sf::Keyboard::X, 0x0},
		   {sf::Keyboard::C, 0xB},
		   {sf::Keyboard::V, 0xF},
		};

		sf::Event event;

		while (window.pollEvent(event)) //Handle input
		{
			switch (event.type) {
			case sf::Event::Closed:
				window.close();
				break;
			case sf::Event::KeyPressed:
				if (event.key.code == sf::Keyboard::I) {
					toggleFramelimiter();
				}
				core.keyState[keyMappings[event.key.code]] = true;
				break;
			case sf::Event::KeyReleased:
				core.keyState[keyMappings[event.key.code]] = false;
				break;
			}
		}
	}

	// fills audio buffer with sin wave
	void initAudio() {
		//generateSquareWaveSamples();
		generateSineWaveSamples();
		soundBuffer.loadFromSamples((sf::Int16*)rawBuffer.data(), SAMPLES, 1, SAMPLERATE);
		sound.setBuffer(soundBuffer);
		sound.setLoop(true);
	}
	
	// fills sample buffer with square wave
	void generateSquareWaveSamples() {
		constexpr uint32_t amplitude = 30000;              // how loud/quiet the sound is
		constexpr double twoPI = 6.28318;
		constexpr double frequency = 440;                  // the pitch of the sound
		// As I understand it, we need to fit frequency amount of waves inside the 
		// buffer of size samplerate. So i * increment stretches the wave to accomplish
		// this.
		constexpr double increment = frequency/SAMPLERATE;
		for (auto i = 0; i < SAMPLES; i++) {
			rawBuffer[i] = amplitude * (sin(i * increment * twoPI) > 0);
		}
	}

	// fills sample buffer with sine wave
	void generateSineWaveSamples() {
		constexpr uint32_t amplitude = 30000;
		constexpr double twoPI = 6.28318;
		constexpr double frequency = 440;
		constexpr double increment = frequency/SAMPLERATE;
		for (unsigned i = 0; i < SAMPLES; i++) {
			rawBuffer[i] = amplitude * sin(i * increment * twoPI);
		}
	}
};