/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "Rendering/GL/myGL.h"

#include "InfoConsole.h"
#include "InputReceiver.h"
#include "GuiHandler.h"
#include "Rendering/Fonts/glFont.h"
#include "System/EventHandler.h"
#include "System/Config/ConfigHandler.h"
#include "System/Log/LogSinkHandler.h"

#define border 7

CONFIG(int, InfoMessageTime).defaultValue(10).description("Timeout till old messages disappear from the ingame console.");
CONFIG(std::string, InfoConsoleGeometry).defaultValue("0.26 0.96 0.41 0.205");

CInfoConsole* infoConsole = nullptr;


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

const size_t CInfoConsole::maxRawLines   = 1024;
const size_t CInfoConsole::maxLastMsgPos = 10;

CInfoConsole::CInfoConsole()
	: CEventClient("InfoConsole", 999, false)
	, enabled(true)
	, lastMsgIter(lastMsgPositions.begin())
	, newLines(0)
	, rawId(0)
	, fontScale(1.0f)
	, maxLines(1)
{
	lifetime = configHandler->GetInt("InfoMessageTime");

	const std::string geo = configHandler->GetString("InfoConsoleGeometry");

	if (sscanf(geo.c_str(), "%f %f %f %f", &xpos, &ypos, &width, &height) != 4) {
		xpos = 0.26f;
		ypos = 0.96f;
		width = 0.41f;
		height = 0.205f;
	}

	if (width == 0.0f || height == 0.0f)
		enabled = false;

	fontSize = fontScale * smallFont->GetSize();

	logSinkHandler.AddSink(this);
	eventHandler.AddClient(this);
}

CInfoConsole::~CInfoConsole()
{
	logSinkHandler.RemoveSink(this);
	eventHandler.RemoveClient(this);
}

void CInfoConsole::Draw()
{
	if (!enabled) return;
	if (!smallFont) return;
	if (data.empty()) return;

	std::lock_guard<spring::recursive_mutex> scoped_lock(infoConsoleMutex);

	if (guihandler != nullptr && !guihandler->GetOutlineFonts()) {
		// draw a black background when not using outlined font
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(0.2f, 0.2f, 0.2f, CInputReceiver::guiAlpha);

		glBegin(GL_TRIANGLE_STRIP);
			glVertex3f(xpos,         ypos,          0);
			glVertex3f(xpos + width, ypos,          0);
			glVertex3f(xpos,         ypos - height, 0);
			glVertex3f(xpos + width, ypos - height, 0);
		glEnd();
	}

	const int fontOptions = FONT_NORM | (FONT_OUTLINE * (guihandler != nullptr && guihandler->GetOutlineFonts())) | FONT_BUFFERED;
	const float fontHeight = fontSize * smallFont->GetLineHeight() * globalRendering->pixelY;

	float curX = xpos + border * globalRendering->pixelX;
	float curY = ypos - border * globalRendering->pixelY;

	smallFont->SetColors(); // default

	for (int i = 0; i < std::min(data.size(), maxLines); ++i) {
		smallFont->glPrint(curX, curY -= fontHeight, fontSize, fontOptions, data[i].text);
	}

	smallFont->DrawBufferedGL4();
}


void CInfoConsole::Update()
{
	std::lock_guard<spring::recursive_mutex> scoped_lock(infoConsoleMutex);
	if (data.empty())
		return;

	// pop old messages after timeout
	if (data[0].timeout <= spring_gettime())
		data.pop_front();

	if (smallFont == nullptr)
		return;

	// if we have more lines then we can show, remove the
	// oldest and make sure the others are shown long enough
	const float  maxHeight = (height * globalRendering->viewSizeY) - (border * 2);
	const float fontHeight = smallFont->GetLineHeight();

	// height=0 will likely be the case on HEADLESS only
	maxLines = (fontHeight > 0.0f)? math::floor(maxHeight / (fontSize * fontHeight)): 1;

	for (size_t i = data.size(); i > maxLines; i--) {
		data.pop_front();
	}
}

void CInfoConsole::PushNewLinesToEventHandler()
{
	if (newLines == 0)
		return;

	std::deque<RawLine> newRawLines;

	{
		std::lock_guard<spring::recursive_mutex> scoped_lock(infoConsoleMutex);

		const int count = (int)rawData.size();
		const int start = count - newLines;
		for (int i = start; i < count; i++) {
			newRawLines.push_back(rawData[i]);
		}
		newLines = 0;
	}

	for (const RawLine& rawLine: newRawLines) {
		eventHandler.AddConsoleLine(rawLine.text, rawLine.section, rawLine.level);
	}
}


int CInfoConsole::GetRawLines(std::deque<RawLine>& lines)
{
	std::lock_guard<spring::recursive_mutex> scoped_lock(infoConsoleMutex);
	lines = rawData;
	const int tmp = newLines;
	newLines = 0;
	return tmp;
}


void CInfoConsole::RecordLogMessage(int level, const std::string& section, const std::string& text)
{
	std::lock_guard<spring::recursive_mutex> scoped_lock(infoConsoleMutex);

	if (rawData.size() > maxRawLines)
		rawData.pop_front();

	if (newLines < maxRawLines)
		++newLines;

	rawData.emplace_back(text, section, level, rawId++);

	if (!smallFont)
		return;

	// NOTE
	//   do not remove elements from `data` here, ::Draw iterates over it
	//   and can call LOG() which will end up back in ::RecordLogMessage
	const float maxWidth = (width * globalRendering->viewSizeX) - (2 * border);
	const std::string& wrappedText = smallFont->Wrap(text, fontSize, maxWidth);

	std::deque<std::string> lines = std::move(smallFont->SplitIntoLines(toustring(wrappedText)));

	for (auto& line: lines) {
		// add the line to the console
		data.emplace_back();
		InfoLine& l = data.back();
		l.text    = std::move(line);
		l.timeout = spring_gettime() + spring_secs(lifetime);
	}
}


void CInfoConsole::LastMessagePosition(const float3& pos)
{
	if (lastMsgPositions.size() >= maxLastMsgPos)
		lastMsgPositions.pop_back();

	lastMsgPositions.push_front(pos);

	// reset the iterator when a new msg comes in
	lastMsgIter = lastMsgPositions.begin();
}

const float3& CInfoConsole::GetMsgPos(const float3& defaultPos)
{
	if (lastMsgPositions.empty())
		return defaultPos;

	// advance the position
	const float3& p = *(lastMsgIter++);

	// wrap around
	if (lastMsgIter == lastMsgPositions.end())
		lastMsgIter = lastMsgPositions.begin();

	return p;
}
