#pragma once
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <SFML/Window.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <chip8.h>

//TODO: sound, LDBVx and DYXN and cleanup between files
class GUI {
public:
	sf::RenderWindow window;
	sf::Texture texture;
	sf::Sprite sprite;
	std::array<uint32_t, WIDTH * HEIGHT> framebuffer;

	std::thread emu_thread;
	Chip8 core;

	//config
	bool isFrameLimited = true; //controls frame limiting
	void toggleFramelimiter() {
		isFrameLimited ^= true;
	}

public:
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
		framebuffer.fill(0);
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
			if (core.sound) --core.sound;

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
};