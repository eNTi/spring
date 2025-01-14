/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef _GAME_CONTROLLER_H_
#define _GAME_CONTROLLER_H_

#include <string>

#include "ConsoleHistory.h"
#include "GameControllerTextInput.h"

class CGameController
{
public:
	virtual ~CGameController();

	virtual bool Draw() { return true; }
	virtual bool Update() { return true; }

	// This adapter makes guarantees backward compatiblity for GameControllers, which haven�t implement KeyPressedSC and KeyReleasedSC
	// = fall back to KeyPressed if KeyPressedSC is not implemented/overriden in derived class
	virtual int KeyPressedSC(int keyScanCode, int keySym, bool isRepeat) {
		return KeyPressed(keySym, isRepeat);
	} 
	virtual int KeyPressed(int keySym, bool isRepeat) { return 0; }
	virtual int KeyReleasedSC(int keyScanCode, int keySym) {
		return KeyReleased(keySym);
	}
	virtual int KeyReleased(int keySym) { return 0; }

	virtual int TextInput(const std::string& utf8Text) { return 0; }
	virtual int TextEditing(const std::string& utf8Text, unsigned int start, unsigned int length) { return 0; }
	virtual void ResizeEvent() {}
};

extern CGameController* activeController;

#endif // _GAME_CONTROLLER_H_
