#include <stdio.h>
#include <stdint.h>
#include <gui.h>
#include <thread>
//#include <xbyak/xbyak.h>
//
//using namespace Xbyak::util;
//
//typedef void(*fp)();

int main()
{
	auto gui = GUI();
	gui.run();

	return 0;
}
