/**
 * Copyright (c) 2014 - 2015 Tolga Cakir <tolga@cevel.net>
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

/* constants */
const int MAX_BUF = 8;
const int MIN_PROFILE = 0;
const int MAX_PROFILE = 3;
/* media keys */
const int EXTRA_KEY_GAMECENTER = 0x10;
const int EXTRA_KEY_RECORD = 0x11;
const int EXTRA_KEY_PROFILE = 0x14;

void Keyboard::featureRequest() {
	unsigned char buf[2];
	/* buf[0] is Report ID, buf[1] is the control byte */
	buf[0] = 0x6;
	buf[1] = 0x10 << profile_;
	buf[1] |= recordLed_ << 7;
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

void Keyboard::setProfile(int profile) {
	profile_ = profile;
	featureRequest();
}

void Keyboard::switchProfile() {
	profile_ = (profile_ + 1) % MAX_PROFILE;
	featureRequest();
}

/*
 * get_input() checks, which keys were pressed. The macro keys are packed in a
 * 5-byte buffer, media keys (including Bank Switch and Record) use 8-bytes.
 */
/*
 * TODO: only return latest pressed key, if multiple keys have been pressed at
 * the same time.
 */
struct KeyData Keyboard::getInput() {
	struct KeyData keyData = KeyData();
	int key, nBytes;
	unsigned char buf[MAX_BUF];
	nBytes = read(fd_, buf, MAX_BUF);

#if 0
	std::cout << "Bytes read: " << nBytes << std::endl;
	std::cout << "Buffer: ";

	for (int i = 0; i < nBytes; i++) {
		std::cout << std::hex << (int) buf[i] << " ";
	}

	std::cout << std::endl;
#endif

	if (nBytes == 4 && buf[0] == 0x03) {
		/*
		 * cutting off buf[0], which is used to differentiate between macro and
		 * media keys. Our task is now to translate the buffer codes to
		 * something we can work with. Here is a table, where you can look up
		 * the keys and buffer, if you want to improve the current method:
		 *
		 * G1	0x03 0x01 0x00 0x00 - buf[1]
		 * G2	0x03 0x02 0x00 0x00 - buf[1]
		 * G3	0x03 0x04 0x00 0x00 - buf[1]
		 * G4	0x03 0x08 0x00 0x00 - buf[1]
		 * G5	0x03 0x10 0x00 0x00 - buf[1]
		 * G6	0x03 0x20 0x00 0x00 - buf[1]
		 * M1	0x03 0x00 0x10 0x00 - buf[2]
		 * M2	0x03 0x00 0x20 0x00 - buf[2]
		 * M3	0x03 0x00 0x40 0x00 - buf[2]
		 * MR	0x03 0x00 0x80 0x00 - buf[2]
		 */
		if (buf[2] == 0) {
			key = (static_cast<int>(buf[1]));
			key = ffs(key);
			keyData.index = key;
			keyData.type = KeyData::KeyType::Macro;
		} else if (buf[1] == 0) {
			key = (static_cast<int>(buf[2])) >> 4;
			key = ffs(key);
			keyData.index = key;
			keyData.type = KeyData::KeyType::Extra;
		}
	}

	return keyData;
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

/*
 * Macro recording captures delays by default. Use the configuration to disable
 * capturing delays.
 */
void Keyboard::recordMacro(std::string path) {
	struct timeval prev;
	struct KeyData keyData;
	prev.tv_usec = 0;
	prev.tv_sec = 0;
	std::cout << "Start Macro Recording on " << deviceData_->devNode.inputEvent << std::endl;
	seteuid(0);
	evfd_ = open(deviceData_->devNode.inputEvent.c_str(), O_RDONLY | O_NONBLOCK);
	seteuid(pw_->pw_uid);

	if (evfd_ < 0) {
		std::cout << "Can't open input event file" << std::endl;
	}

	/* additionally monitor /dev/input/event* with poll */
	fds[1].fd = evfd_;
	tinyxml2::XMLDocument doc;
	tinyxml2::XMLNode* root = doc.NewElement("Macro");
	/* start root element "Macro" */
	doc.InsertFirstChild(root);

	bool isRecordMode = true;

	while (isRecordMode) {
		keyData = pollDevice(2);

		if (keyData.index == 4 && keyData.type == KeyData::KeyType::Extra) {
			recordLed_ = 0;
			featureRequest();
			isRecordMode = false;
		}

		struct input_event inev;
		read(evfd_, &inev, sizeof(struct input_event));

		if (inev.type == EV_KEY && inev.value != 2) {
			/* only capturing delays, if capture_delays is set to true */
			if (prev.tv_usec && config_->lookup("capture_delays")) {
				long int diff = (inev.time.tv_usec + 1000000 * inev.time.tv_sec) - (prev.tv_usec + 1000000 * prev.tv_sec);
				int delay = diff / 1000;
				/* start element "DelayEvent" */
				tinyxml2::XMLElement* DelayEvent = doc.NewElement("DelayEvent");
				DelayEvent->SetText(delay);
				root->InsertEndChild(DelayEvent);
			}

			/* start element "KeyBoardEvent" */
			tinyxml2::XMLElement* KeyBoardEvent = doc.NewElement("KeyBoardEvent");

			if (inev.value) {
				KeyBoardEvent->SetAttribute("Down", true);
			} else {
				KeyBoardEvent->SetAttribute("Down", false);
			}

			KeyBoardEvent->SetText(inev.code);
			root->InsertEndChild(KeyBoardEvent);
			prev = inev.time;
		}
	}

	/* write XML document */
	if (doc.SaveFile(path.c_str())) {
		std::cout << "Error XML SaveFile" << std::endl;
	}

	std::cout << "Exit Macro Recording" << std::endl;
	/* remove event file from poll fds */
	fds[1].fd = -1;
	close(evfd_);
}

void Keyboard::handleKey(struct KeyData *keyData) {
	if (keyData->index != 0) {
		if (keyData->type == KeyData::KeyType::Macro) {
			Key key(keyData);
			std::string macroPath = key.getMacroPath(profile_);
			std::thread thread(playMacro, macroPath, virtInput_);
			thread.detach();
		} else if (keyData->type == KeyData::KeyType::Extra) {
			if (keyData->index == 1) {
				/* M1 key */
				setProfile(0);
			} else if (keyData->index == 2) {
				/* M2 key */
				setProfile(1);
			} else if (keyData->index == 3) {
				/* M3 key */
				setProfile(2);
			} else if (keyData->index == 4) {
				/* MR key */
				handleRecordMode();
			}
		}
	}
}

void Keyboard::handleRecordMode() {
	bool isRecordMode = true;
	recordLed_ = 1;
	featureRequest();

	while (isRecordMode) {
		struct KeyData keyData = pollDevice(1);

		if (keyData.index != 0) {
			if (keyData.type == KeyData::KeyType::Macro) {
				recordLed_ = 1;
				featureRequest();
				isRecordMode = false;
				Key key(&keyData);
				recordMacro(key.getMacroPath(profile_));
			} else if (keyData.type == KeyData::KeyType::Extra) {
				/* deactivate Record LED */
				recordLed_ = 0;
				featureRequest();
				isRecordMode = false;

				if (keyData.index != 4) {
					handleKey(&keyData);
				}
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

void Keyboard::disableGhostInput() {
	/* we need to zero out the report, so macro keys don't emit numbers */
	unsigned char buf[13] = {};
	/* buf[0] is Report ID */
	buf[0] = 0x9;

	ioctl(fd_, HIDIOCSFEATURE(sizeof(buf)), buf);
}

Keyboard::Keyboard(struct sidewinderd::DeviceData *deviceData, libconfig::Config *config, struct passwd *pw) {
	config_ = config;
	pw_ = pw;
	deviceData_ = deviceData;
	virtInput_ = new VirtualInput(deviceData_, pw);
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
	fd_ = open(deviceData_->devNode.hidraw.c_str(), O_RDWR | O_NONBLOCK);
	seteuid(pw_->pw_uid);

	/* TODO: throw exception if interface can't be accessed, call destructor */
	if (fd_ < 0) {
		std::cout << "Can't open hidraw interface" << std::endl;
	}

	disableGhostInput();
	featureRequest();
	setupPoll();
}

Keyboard::~Keyboard() {
	delete virtInput_;
	recordLed_ = 0;
	featureRequest();
	close(fd_);
}
