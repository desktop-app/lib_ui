/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QWindow;

namespace Ui {
namespace Platform {

class WaylandIntegration {
public:
	static WaylandIntegration *Instance();
	bool showWindowMenu(QWindow *window);

private:
	WaylandIntegration();
};

} // namespace Platform
} // namespace Ui
