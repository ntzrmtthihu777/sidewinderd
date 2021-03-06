/**
 * Copyright (c) 2014 - 2016 Tolga Cakir <tolga@cevel.net>
 *
 * This source file is part of Sidewinder daemon and is distributed under the
 * MIT License. For more information, see LICENSE file.
 */

#include <cstdio>
#include <ctime>
#include <iostream>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <tinyxml2.h>
#include <unistd.h>

#include <linux/hidraw.h>
#include <linux/input.h>

#include <sys/ioctl.h>
#include <sys/stat.h>

#include "keyboard.hpp"

void Keyboard::featureRequest(unsigned char data) {
	unsigned char buf[2];
	/* buf[0] is Report ID, buf[1] is value */
	buf[0] = data;
	ioctl(fd_, HIDIOCSFEATURE(sizeof(buf)), buf);
}

void Keyboard::setupPoll() {
	fds[0].fd = fd_;
	fds[0].events = POLLIN;
	/* ignore second fd for now */
	fds[1].fd = -1;
	fds[1].events = POLLIN;
}

void Keyboard::toggleMacroPad() {
	macroPad_ ^= 1;
	featureRequest();
}

void Keyboard::switchProfile() {
	profile_ = (profile_ + 1) % MAX_PROFILE;
	featureRequest();
}

/* TODO: interrupt and exit play_macro when any macro_key has been pressed */
void Keyboard::playMacro(std::string macroPath, VirtualInput *virtInput) {
	tinyxml2::XMLDocument xmlDoc;
	xmlDoc.LoadFile(macroPath.c_str());

	if(!xmlDoc.ErrorID()) {
		tinyxml2::XMLElement* root = xmlDoc.FirstChildElement("Macro");

		for (tinyxml2::XMLElement* child = root->FirstChildElement(); child; child = child->NextSiblingElement()) {
			if (child->Name() == std::string("KeyBoardEvent")) {
				bool isPressed;
				int key = std::atoi(child->GetText());
				child->QueryBoolAttribute("Down", &isPressed);
				virtInput->sendEvent(EV_KEY, key, isPressed);
			} else if (child->Name() == std::string("DelayEvent")) {
				int delay = std::atoi(child->GetText());
				struct timespec request, remain;
				/*
				 * value is given in milliseconds, so we need to split it into
				 * seconds and nanoseconds. nanosleep() is interruptable and saves
				 * the remaining sleep time.
				 */
				request.tv_sec = delay / 1000;
				delay = delay - (request.tv_sec * 1000);
				request.tv_nsec = 1000000L * delay;
				nanosleep(&request, &remain);
			}
		}
	}
}

struct KeyData Keyboard::pollDevice(nfds_t nfds) {
	/*
	 * poll() checks the device for any activities and blocks the loop. This
	 * leads to a very efficient polling mechanism.
	 */
	poll(fds, nfds, -1);
	struct KeyData keyData = getInput();

	return keyData;
}

void Keyboard::listen() {
	struct KeyData keyData = pollDevice(1);
	handleKey(&keyData);
}

Keyboard::Keyboard(struct sidewinderd::DeviceData *deviceData, struct sidewinderd::DevNode *devNode, libconfig::Config *config, struct passwd *pw) {
	config_ = config;
	pw_ = pw;
	deviceData_ = deviceData;
	devNode_ = devNode;
	virtInput_ = new VirtualInput(deviceData_, devNode_, pw);
	profile_ = 0;
	autoLed_ = 0;
	recordLed_ = 0;
	macroPad_ = 0;

	for (int i = MIN_PROFILE; i < MAX_PROFILE; i++) {
		std::stringstream profileFolderPath;
		profileFolderPath << "profile_" << i + 1;
		mkdir(profileFolderPath.str().c_str(), S_IRWXU);
	}

	/* open file descriptor with root privileges */
	seteuid(0);
	fd_ = open(devNode->hidraw.c_str(), O_RDWR | O_NONBLOCK);
	seteuid(pw_->pw_uid);

	/* TODO: throw exception if interface can't be accessed, call destructor */
	if (fd_ < 0) {
		std::cout << "Can't open hidraw interface" << std::endl;
	}

	featureRequest();
	setupPoll();
}

Keyboard::~Keyboard() {
	delete virtInput_;
	recordLed_ = 0;
	featureRequest(0);
	close(fd_);
}
