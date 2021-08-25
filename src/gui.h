#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <SFML/Window.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <chip8.h>

class GUI {
private:
	sf::RenderWindow window;
	sf::Texture texture;
	sf::Sprite sprite;

	std::thread emu_thread;
	Chip8 core;

public:
	bool runFrame;
	std::mutex mRunFrame;
	std::condition_variable cvRunFrame;

	GUI() : window(sf::VideoMode(640, 320), "JIT8"), core(this, 600) {
		emu_thread = std::thread([this]() {
			core.runFrame();
			});
		emu_thread.detach(); //fly free, emu thread...

		runFrame = false;

		//Initialise SFML stuff
		texture.create(64, 32);
		sprite.setTexture(texture);
		sprite.setScale(sf::Vector2f(10, 10));

		window.setFramerateLimit(60);
	}
	~GUI() = default;

	//There's no real point to the way i handle multithreading in this application,
	//Since the gui thread barely does anything. However it was pretty cool
	//learn, so I'll stick with it

	//Instead of having each thread synced like this, its also possible to let
	//The emu thread just do its thing separately. Dunno how that'd work though overall


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
			pingEmuthread();

			sf::Event event;
			while (window.pollEvent(event))
			{
				// "close requested" event: we close the window
				if (event.type == sf::Event::Closed)
					window.close();
			}

			texture.update(core.framebuffer.data()); //Draw framebuffer to screen
			window.draw(sprite);
			window.display();

			waitForEmuThread();
		}
	}
};